/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
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

WvString isdn_init;
bool     default_asyncmap = false;

// startup at atz atq0 atv1 ate1 ats0 carrier dtr fastdial
// baudstep reinit done
static const char *commands[WvModemScan::NUM_STAGES] = {
    NULL, "Q0 V1 E1", "Z", "S0=0",
    "&C1", "&D2", "+FCLASS=0", NULL,
    NULL, "", NULL
};

static int baudcheck[6] = {
	2400,
	9600,
	115200,
	0
};

static int default_baud =   baudcheck[0];
static int isdn_speed   = 115200;

WvModemScan::WvModemScan(WvStringParm devname, bool is_modem_link)
	: debug(devname, WvLog::Debug)
{
    stage = Startup;
    memset(status, 0, sizeof(status));

    if (devname[0] == '/')
	file = devname;
    else
	file = WvString("/dev/%s", devname);
    
    use_modem_link = is_modem_link;
    baud = default_baud;
    modem = NULL;
    tries = 0;
    broken = false;
}


WvModemScan::~WvModemScan()
{
    if (isok() && isdone())
	debug(WvLog::Info, "Speed %s; init \"%s\"\n", maxbaud(), initstr());
    
    WVRELEASE(modem);
}


bool WvModemScan::use_default_asyncmap() const
{
    return default_asyncmap;
}

bool WvModemScan::isok() const
{
    return !broken;
}



WvString WvModemScan::initstr() const
{
    char s[200];

    if (isdn_init)
	return (isdn_init);

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
    
    return WvString(trim_string(s));
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
    case ATS0:
    case Carrier:
    case DTR:
    case FCLASS:
    case Reinit:
	assert(modem);
	status[stage] = Test;
	if (!strncmp(file, "/dev/ircomm", 11)) 
	{
	    while (baudcheck[tries+1] <= 9600 && baudcheck[tries+1] != 0) 
		tries++;

	    if (baudcheck[tries] > 19200 || baudcheck[tries] == 0) 
	    {
		broken = true;
		debug("failed at 9600 and 19200 baud.\n");
		return;
	    }
	    baud = modem->speed(baudcheck[tries]);
	}
	if (!doresult(WvString("%s\r", initstr()), stage==ATZ ? 3000 : 500)
	    || ((stage <= AT || stage == Reinit) && status[stage]==Fail))
	{
	    int old_baud = baud;
	    tries++;
	    //modem->drain();
	    //modem->speed(baud*2);
	    //baud = modem->speed(baud);
	    if (baudcheck[tries] == 0) 
	    {
		broken = true;
		debug("and failed too at %s, giving up.\n",
			WvString(isdn_speed));
		// Go back to default_baud:
		modem->speed(default_baud);
	    	baud = modem->getspeed();
	    } 
	    else if (strncmp(file, "/dev/ircomm", 11))
	        debug("failed with %s baud, next try: %s baud\n",
		      old_baud,
		      baud = modem->speed(baudcheck[tries]));
	            //baud = modem->speed(baud*2));
#if 0	    
	    if (tries >= 4)
	    {
		if (baud == default_baud) 
		{
			debug("nothing at %s baud,\n", WvString(default_baud));
			// Ok, then let's try ISDN speed for ISDN TAs:
			modem->speed(isdn_speed);
	    		baud = modem->getspeed();
	    		tries = 0;
		} 
		else 
		{
			// Ok, we tried default_baud and ISDN speed, give up:
			broken = true;
			debug("nor at %s.\n", WvString(isdn_speed));
			// Go back to default_baud:
			modem->speed(default_baud);
	    		baud = modem->getspeed();
		}
	    }
#endif
	    
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
	if (!doresult(WvString("ATI\r"), 500) || (status[stage]==Fail))
	{
	    tries++;

	    if (tries >= 3)
	    	debug("nothing.\n");

	    // else try again shortly
	}
	else
	{
	    if (is_isdn())
	    	debug("Looks like an ISDN modem.\n");

	    if (!strncmp(identifier, "Hagenuk", 7)) 
	    {
	        status[stage] = Test;
		if (doresult(WvString("ATI1\r"), 500)) 
		    if (!strncmp(identifier, "Speed Dragon", 12)
		    ||  !strncmp(identifier, "Power Dragon", 12)) 
		    {
		    	isdn_init = "ATB8";
		    	modem_name = WvString("Hagenuk %s", identifier);
		    }
	        status[stage] = Worked;
	    } 
	    else if (!strncmp(identifier, "346900", 6)) 
	    {
	        status[stage] = Test;
		if (doresult(WvString("ATI3\r"), 500))
		    if (!strncmp(identifier, "3Com U.S. Robotics ISDN",23)) 
		    {
			isdn_init = "AT*PPP=1";
			modem_name = identifier;
		    }
	        status[stage] = Worked;
	    } 
	    else if (!strncmp(identifier, "SP ISDN", 7)) 
	    {
	        status[stage] = Test;
		if (doresult(WvString("ATI4\r"), 500))
		    if (!strncmp(identifier, "Sportster ISDN TA", 17)) 
		    {
			isdn_init = "ATB3";
			modem_name = identifier;
		    }
	        status[stage] = Worked;
	    } 
	    else if (!strncmp(identifier, "\"Version", 8)) 
	    {
	        status[stage] = Test;
		if (doresult(WvString("ATI6\r"), 500))
		    modem_name = identifier;
	        status[stage] = Worked;
	    } 
	    else if (!strncmp(identifier, "644", 3)) 
	    {
	        status[stage] = Test;
		if (doresult(WvString("ATI6\r"), 500))
		    if (!strncmp(identifier, "ELSA MicroLink ISDN", 19)) 
		    {
			isdn_init = "AT$IBP=HDLCP";
			modem_name = identifier;
			default_asyncmap = true;
		    }
	        status[stage] = Worked;
	    } 
	    else if (!strncmp(identifier, "643", 3)) 
	    {
	        status[stage] = Test;
		if (doresult(WvString("ATI6\r"), 500))
		    if (!strncmp(identifier, "MicroLink ISDN/TLV.34", 21)) 
		    {
			isdn_init = "AT\\N10%P1";
			modem_name = identifier;
		    }
	        status[stage] = Worked;
	    } 
	    else if (!strncmp(identifier, "ISDN TA", 6)) 
	    {
	        status[stage] = Test;
		if (doresult(WvString("ATI5\r"), 500))
		    if (strstr(identifier, ";ASU")) 
		    {
			isdn_init = "ATB40";
			modem_name = "ASUSCOM ISDNLink TA";
		    }
	        status[stage] = Worked;
	    } 
	    else if (!strncmp(identifier, "128000", 6)) 
	    {
	        status[stage] = Test;
		if (doresult(WvString("ATI3\r"), 500))
		    if (!strncmp(identifier, "Lasat Speed", 11)) 
		    {
			isdn_init = "AT\\P1&B2X3";
			modem_name = identifier;
		    }
	        status[stage] = Worked;
	    } 
	    else if (!strncmp(identifier, "28642", 5) // Elite 2864I
		     || !strncmp(identifier, "1281", 4)  // Omni TA128 USA
		     || !strncmp(identifier, "1282", 4)  // Omni TA128 DSS1
		     || !strncmp(identifier, "1283", 4)  // Omni TA128 1TR6
		     || !strncmp(identifier, "1291", 4)  // Omni.Net USA
		     || !strncmp(identifier, "1292", 4)  // Omni.Net DSS1
		     || !strncmp(identifier, "1293", 4)  // Omni.Net 1TR6
		     ) 
	    {
	        status[stage] = Test;
		if (doresult(WvString("ATI1\r"), 500))
		    if (!strncmp(identifier, "Elite 2864I", 11)
			|| !strncmp(identifier, "ZyXEL omni", 10)) 
		    {
			isdn_init = "AT&O2B40";
			if (strncmp(identifier, "ZyXEL", 5))
		            modem_name = WvString("ZyXEL %s", identifier);
			else
			    modem_name = identifier;
		    }
	        status[stage] = Worked;
	    }
	    
	    tries = 0;
	    stage++;
	}
	
    case BaudStep:
	assert(modem);
	modem->drain();
	modem->speed(baud*2);

	// if we try 2*baud three times without success, or setting 2*baud
	// results in a lower setting than 1*baud, we have reached the
	// top speed of the modem or the serial port, respectively.
	if (tries >= 3 || modem->getspeed() <= baud)
	{
	    // using the absolute maximum baud rate confuses many slower modems
	    // in obscure ways; step down one.
	    baud = modem->speed(baud);
	    debug("Max speed is %s; that should be safe.\n", baud);
	    
	    stage++;
	    status[stage] = Worked;
	    break;
	}
	
	debug("Speed %s: ", modem->getspeed());
	
	if (!doresult("AT\r", 500) || status[stage] == Fail)
	{
	    tries++;
	}
	else // got a response
	{
	    baud = modem->getspeed();
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
	WVRELEASE(modem);
    }
}


bool WvModemScan::doresult(WvStringParm _s, int msec)
{
    char buf[1024], *cptr;
    size_t len;
    WvString s(_s);
    
    modem->drain();
    usleep(50 * 1000); // delay a bit after emptying the buffer
    modem->write(s);

    debug("%s -- ", trim_string(s.edit()));
    
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

    if (!strncmp(cptr, "OK", 2))
	status[stage] = Worked;
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
    
    while (modem->select(msec, true, false))
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


const char *WvModemScan::is_isdn() const
{
    if (isdn_init)
	return isdn_init;

    if (!identifier)
    	return NULL;

    if (identifier == "3C882")		// 3Com Impact IQ
    	return identifier;
    if (identifier == "346800")		// USR ISDN TA
    	return identifier;

#if 0 // this isn't nearly unique enough...
    if (identifier == "960")		// Motorola BitSurfr
    	return identifier;
#endif
    
    return NULL;
}	


static int fileselect(const struct dirent *e)
{
    return !strncmp(e->d_name, "ttyS", 4)      	// serial
       || !strncmp(e->d_name, "ttyLT", 5)	// Lucent WinModem
       || !strncmp(e->d_name, "ttyACM", 6)      // USB acm Modems
       || !strncmp(e->d_name, "ttyUSB", 6)      // Modems on USB RS232
       || !strncmp(e->d_name, "ircomm", 6)      // Handys over IrDA
       || !strncmp(e->d_name, "ttySL", 5);	// SmartLink WinModem

	// (no internal ISDN support)   || !strncmp(e->d_name, "ttyI", 4);
}

#if defined(__GLIBC__) && __GLIBC_PREREQ(2, 10)
static int filesort(const dirent **e1, const dirent **e2)
#else
static int filesort(const void *_e1, const void *_e2)
#endif
{
#if !(defined(__GLIBC__) && __GLIBC_PREREQ(2, 10))
    dirent const * const *e1 = (dirent const * const *)_e1;
    dirent const * const *e2 = (dirent const * const *)_e2;
#endif
    const char *p1, *p2;
    int diff;
    
    for (p1=(*e1)->d_name, p2=(*e2)->d_name; *p1 || *p2; p1++, p2++)
    {
	if (!isdigit(*p1) || !isdigit(*p2))
	{
	    // Scan i (ircomm*) after t (tty*):
	    if (*p1 == 'i' && *p2 == 't')
		return(1);
	    // Scan A (ttyACM*) after S (ttyS*):
	    if (*p1 == 'A' && *p2 == 'S')
		return(1);
	    if (*p1 == 'S' && *p2 == 'A')
		return(-1);
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


WvModemScanList::WvModemScanList(WvStringParm _exception) 
    : log("Modem Port Scan", WvLog::Debug)
{
    struct dirent **namelist;
    struct stat mouse, modem;
    int num, count, mousestat, modemstat;
    WvString exception;
    
    thisline = -1;
    printed = false;
    
    mousestat = stat("/dev/mouse", &mouse);
    modemstat = stat("/dev/modem", &modem);
    num = scandir("/dev", &namelist, fileselect, filesort);
    
    if (num < 0)
	return;
    
    // there shouldn't be a /dev/
    if (!!_exception)
	exception = strrchr(_exception, '/') + 1;
    
    for (count = 0; count < num; count++)
    {
	// never search the device assigned to /dev/mouse; most mouse-using
	// programs neglect to lock the device, so we could mess up the
	// mouse response!  (We are careful to put things back when done,
	// but X seems to still get confused.)  Anyway the mouse is seldom
	// a modem.
	if (mousestat==0 && mouse.st_ino == (ino_t)namelist[count]->d_ino)
	{
	    log("\nIgnoring %s because /dev/mouse is a link to it.\n",
		       namelist[count]->d_name);
	    continue;
	}
	
	if (!!exception && !strcmp(exception, namelist[count]->d_name))
	{
	    log("\nIgnoring %s because I've been told to ignore it.\n",
		namelist[count]->d_name);
	    continue;
	}
	
	// bump /dev/modem to the top of the list, if it exists
	// and also use /dev/modem as the device name which will be used later
	// so PCMCIA can change it where it has detected a serial port and
	// wvdial will follow without the need for another wvdialconf call.
	if (modemstat==0 && modem.st_ino == (ino_t)namelist[count]->d_ino) 
	{
	    log("\nScanning %s first, /dev/modem is a link to it.\n",
		       namelist[count]->d_name);
	    prepend(new WvModemScan(WvString("%s", namelist[count]->d_name), true),
		   true);
	} 
	else
	    append(new WvModemScan(WvString("%s", namelist[count]->d_name), false),
		   true);
    }

    while (--num >= 0)
	free(namelist[num]);
    free(namelist);
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

    for (i.rewind(); i.next(); )
	if (!i().isdone()) break;

    if (!i.cur()) return; 
    
    WvModemScan &s(*i);
    
    if (!s.isok())
    {
	if (!printed)
	{
	    WvStringParm f = s.filename();
	    const char *cptr = strrchr(f, '/');
	    if (cptr)
		cptr++;
	    else
		cptr = f;

	    if (!strncmp(cptr, "tty", 3))
		cptr += 3;
	    
	    ++thisline %= 8;
	    if (!thisline)
		log("\n");

	    log("%-4s ", cptr);
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
	log("\n");
}


bool WvModemScanList::isdone()
{
    WvModemScanList::Iter i(*this);
    
    for (i.rewind(); i.next(); )
	if (!i().isdone()) return false;
    
    return true;
}
