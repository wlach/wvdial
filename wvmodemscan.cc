/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997, 1998 Worldvisions Computer Technology, Inc.
 *
 * Intelligent serial port scanner: try to find a port (or ports)
 * with a working modem, guess a basic startup init string, and find
 * the maximum baud rate.
 */
#include "wvmodemscan.h"
#include "wvmodem.h"
#include "strutils.h"
#include <time.h>
#include <assert.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>


// startup at atz atq0 atv1 ate1 ats0 carrier dtr fastdial
// baudstep reinit done
static char *commands[WvModemScan::NUM_STAGES] = {
    NULL, "", "Z", "Q0", "V1", "E1", "S0=0", "&C1", "&D2", "S11=55",
    "+FCLASS=0", NULL,
    NULL, "", NULL
};


WvModemScan::WvModemScan(const char *devname)
	: debug(devname, WvLog::Debug)
{
    stage = Startup;
    memset(status, 0, sizeof(status));

    if (devname[0] == '/')
	file = devname;
    else
	file = WvString("/dev/%s", devname);
    
    baud = 1200;
    modem = NULL;
    tries = 0;
    broken = false;
}


WvModemScan::~WvModemScan()
{
    if (isok() && isdone())
	debug(WvLog::Info, "Speed %s; init \"%s\"\n", maxbaud(), initstr());
    
    if (modem)
	delete modem;
}


bool WvModemScan::isok() const
{
    return !broken;
}



WvString WvModemScan::initstr() const
{
    WvString s;
    s.setsize(100);
    strcpy(s, "AT");
    
    for (int i = 0; i < NUM_STAGES; i++)
    {
	if (status[i] != Worked && status[i] != Test)
	    continue;
	if (!commands[i] || !commands[i][0])
	    continue;
	if ((commands[i][0]=='Z' || commands[i][0]=='I') && status[i] != Test)
	    continue;
	
	strcat(s, commands[i]);
	strcat(s, " ");
    }
    
    return trim_string(s);
}


void WvModemScan::execute()
{
    if (isdone() || !isok()) return;

    switch ((Stage)stage)
    {
    case Startup:
	assert(!modem);
	modem = new WvModem(file, baud);
	modem->die_fast = true;
	if (!modem->isok())
	{
	    if (modem->geterr()
		&& modem->geterr() != EIO
		&& modem->geterr() != ENOENT
		&& modem->geterr() != ENODEV)
	    {
		debug(WvLog::Info, "%s\n", modem->errstr());
	    }
	    broken = true;
	}
	else
	    stage++;
	break;
	
    case AT:
    case ATZ:
    case ATQ0:
    case ATV1:
    case ATE1:
    case ATS0:
    case Carrier:
    case DTR:
    case FastDial:
    case FCLASS:
    case Reinit:
	assert(modem);
	status[stage] = Test;
	if ( !doresult(WvString("%s\r", initstr()), stage==ATZ ? 3000 : 500)
	    || (stage <= ATZ && status[stage]==Fail) )
	{
	    tries++;
	    
	    if (tries >= 3)
	    {
		debug("nothing.\n");
		broken = true;
	    }
	    
	    // else try again shortly
	}
	else
	{
	    tries = 0;
	    stage++;
	}
	break;

    case GetIdent:
	assert(modem);
	status[stage] = Test;
	debug("Modem Identifier: ");
	if ( !doresult(WvString("ATI\r"), 500) || (status[stage]==Fail) )
	{
	    tries++;

	    if (tries >= 3)
	    {
	    	debug("nothing.\n");
	    }

	    // else try again shortly
	}
	else
	{
	    if (is_isdn())
	    	debug("Looks like an ISDN modem.\n");
	    tries = 0;
	    stage++;
	}
	
    case BaudStep:
	assert(modem);
	modem->drain();
	modem->speed(baud*2);

	// if we try 2*baud five times without success, or setting 2*baud
	// results in a lower setting than 1*baud, we have reached the
	// top speed of the modem or the serial port, respectively.
	if (tries >= 3 || modem->speed() < baud)
	{
	    // using the absolute maximum baud rate confuses many modems
	    // in obscure ways; step down one.
	    modem->speed(baud);
	    debug("Max speed is %s; ", modem->speed());
	    if (is_isdn())
	    {
	    	//if (modem->speed() != 230400)		// FIXME
	    	    modem->speed(115200);
	    	baud = modem->speed();
	    	debug ("using %s for ISDN modem.\n", baud);
	    }
	    else
	    {
		modem->speed(baud / 2);
		baud = modem->speed(); // get correct value!
		debug("using %s to be safe.\n", baud);
	    }
	    
	    stage++;
	    status[stage] = Worked;
	    break;
	}
	
	debug("Speed %s: ", modem->speed());

	if (!doresult("AT\r", 500) || status[stage] == Fail)
	{
	    tries++;
	}
	else // got a response
	{
	    baud *= 2;
	    tries = 0;
	    // next time through we try a faster speed
	}
	break;
	
    case Done:
    case NUM_STAGES:
	assert(0);
	break; // should never happen
    }
    
    if (stage == Done) // we just incremented stage number to Done
    {
	if (modem)
	    delete modem;
	modem = NULL;
    }
}


bool WvModemScan::doresult(const WvString &s, int msec)
{
    char buf[1024], *cptr;
    size_t len;
    
    modem->drain();
    usleep(50 * 1000); // delay a bit after emptying the buffer
    modem->write(s);

    debug("%s -- ", trim_string(s));
    
    len = coagulate(buf, sizeof(buf), msec);

    if (!len)
    {
	// debug("(no answer yet)\n");
	return false;
    }
    
    buf[len] = 0;
    
    cptr = trim_string(buf);
    while (strchr(cptr, '\r'))
    {
	cptr = trim_string(strchr(cptr, '\r'));
	if (stage == GetIdent && status[stage] == Test)
	{
	    char *p = strpbrk(cptr, "\n\r");
	    if (p) *p=0;
	    identifier = cptr;
	    status[stage] = Worked;
	    debug("%s\n", identifier);
	    return true;
	}
    }
    while (strchr(cptr, '\n'))
	cptr = trim_string(strchr(cptr, '\n'));
    
    debug("%s\n", cptr);

    if (!strncmp(cptr, "OK", 2)
	|| (stage <= ATV1 && *(strchr(cptr,0) - 1)=='0'))
    {
	status[stage] = Worked;
    }
    else
	status[stage] = Fail;
    
    return true;
}


size_t WvModemScan::coagulate(char *buf, size_t size, int msec)
{
    size_t len = 0, amt;
    char *cptr = buf;

    assert(modem);
    
    if (!modem->isok())
    {
	broken = true;
	return 0;
    }
    
    while (modem->select(msec))
    {
	amt = modem->read(cptr, size-1);
	cptr[amt] = 0;

	len += amt;
	size -= amt;
	cptr += amt;

	if (strstr(buf, "OK") || strstr(buf, "ERROR"))
	    break;
    }
    
    return len;
}


char *WvModemScan::is_isdn() const
{
    if (!identifier)
    	return NULL;

    if (identifier == "3C882")		// 3Com Impact IQ
    	return identifier;
    if (identifier == "960")		// Motorola BitSurfr
    	return identifier;

    return NULL;
}	


static int fileselect(const struct dirent *e)
{
    return !strncmp(e->d_name, "ttyS", 4);     // serial
       // (no internal ISDN support)   || !strncmp(e->d_name, "ttyI", 4);
}


static int filesort(const void *_e1, const void *_e2)
{
    dirent const * const *e1 = (dirent const * const *)_e1;
    dirent const * const *e2 = (dirent const * const *)_e2;
    const char *p1, *p2;
    int diff;
    
    for (p1=(*e1)->d_name, p2=(*e2)->d_name; *p1 || *p2; p1++, p2++)
    {
	if (!isdigit(*p1) || !isdigit(*p2))
	{
	    diff = *p1 - *p2;
	    if (diff) return diff;
	}
	else // both are digits
	{
	    return atoi(p1) - atoi(p2);
	}
    }
    
    return 0;
}


void WvModemScanList::setup()
{
    struct dirent **namelist;
    struct stat mouse;
    int num, count, statresponse;
    
    log = new WvLog("Port Scan", WvLog::Debug);
    thisline = -1;
    printed = false;
    
    statresponse = stat("/dev/mouse", &mouse);
    num = scandir("/dev", &namelist, fileselect, filesort);
    
    if (num < 0)
	return;
    
    for (count = 0; count < num; count++)
    {
	// never search the device assigned to /dev/mouse; most mouse-using
	// programs neglect to lock the device, so we could mess up the
	// mouse response!  (We are careful to put things back when done,
	// but X seems to still get confused.)  Anyway the mouse is seldom
	// a modem.
	if (statresponse==0 && mouse.st_ino == (ino_t)namelist[count]->d_ino)
	{
	    log->print("\nIgnoring %s because /dev/mouse is a link to it.\n",
		       namelist[count]->d_name);
	    continue;
	}
	
	append(new WvModemScan(namelist[count]->d_name), true);
    }
    
    while (--num >= 0)
	free(namelist[num]);
    free(namelist);
}


void WvModemScanList::shutdown()
{
    delete log;
}


// we used to try to scan all ports simultaneously; unfortunately, this
// caused problems when people had "noncritical" IRQ conflicts (ie. two
// serial ports with the same IRQ, but they work as long as only one port
// is used at a time).  Also, the log messages looked really confused.
//
// So now we do the scanning sequentially, which is slower.  The port
// being scanned is at the head of the list.  If the probe fails, we
// unlink the element.  If it succeeds, we have found a modem -- so,
// isdone() knows we are done when the _first_ element is done.
//
void WvModemScanList::execute()
{
    assert (!isdone());

    WvModemScanList::Iter i(*this);
    
    i.rewind();
    if (!i.next()) return; // note: the first next() is the _first_ item!
    
    WvModemScan &s(i);
    
    if (!s.isok())
    {
	if (!printed)
	{
	    const WvString &f = s.filename();
	    const char *cptr = strrchr(f, '/');
	    if (cptr)
		cptr++;
	    else
		cptr = f;

	    if (!strncmp(cptr, "tty", 3))
		cptr += 3;
	    
	    ++thisline %= 8;
	    if (!thisline)
		log->print("\n");

	    log->print("%-4s ", cptr);
	}

	i.unlink();
	printed = false;
    }
    else
    {
	s.execute();
	
	if (s.isok())
	    printed = true;
    }
	    
    if (isdone())
	log->write("\n", 1);
}


bool WvModemScanList::isdone()
{
    WvModemScanList::Iter i(*this);
    
    i.rewind();
    if (i.next())
	return i().isdone();
    else
	return true; // empty list, so we are done!
}
