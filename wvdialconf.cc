/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
 *
 * WvDial configuration utility.  Generates a basic wvdial.conf file.
 */
#include "wvmodemscan.h"
#include "wvfile.h"
#include "wvconf.h"
#include <ctype.h>


void check_ppp_options()
{
    WvFile file("/etc/ppp/options", O_RDONLY);
    char *line;
    
    while ((line = file.getline(0)) != NULL)
    {
	line = trim_string(line);
	
	// comments and blank lines are ignored
	if (line[0] == '#'  ||  !line[0])
	    continue;
	
	// IP addresses are allowed
	if (strchr(line, '.') || strchr(line, ':'))
	    continue;
	
	// but baud rates and tty names are not!
	// a 'connect' line is usually bad too.
	if (isdigit(line[0])
	    || !strncmp(line, "/dev", 4)
	    || !strncmp(line, "tty",  3) 
	    || !strncmp(line, "cua",  3)
	    || !strncmp(line, "connect", 7))
	{
	    wvcon->print("\n*** WARNING!  Line \"%s\"\n"
		"   in /etc/ppp/options may conflict with wvdial!\n\n", line);
	}
    }
}


int main(int argc, char **argv)
{
#if DEBUG
    free(malloc(1));    // for electric fence
#endif	
    
    if (argc != 2 || argv[1][0]=='-')
    {
	wvcon->print("Usage: %s <configfile-name>\n"
		"\t(create/update a wvdial.conf file automatically)\n",
		argv[0]);
	return 1;
    }
    
    wvcon->print("Scanning your serial ports for a modem.\n\n");
    
    WvModemScanList l;
    while (!l.isdone())
	l.execute();
    
    if (l.count() < 1)
    {
	wvcon->print("\n\n"
	  "Sorry, no modem was detected!  "
	    "Is it in use by another program?\n"
	  "Did you configure it properly with setserial?\n\n"
		
	  "Please read the FAQ at http://open.nit.ca/wvdial/\n\n"
		
	  "If you still have problems, send mail to "
	    "wvdial-list@lists.nit.ca.\n");
	return 1;
    }
    
    WvModemScanList::Iter i(l);
    
    i.rewind(); i.next();
    WvModemScan &m = *i;
    WvString fn = m.filename(), init = m.initstr();
    
    wvcon->print("\nFound %s on %s",
        m.is_isdn() ? "an ISDN TA" :
        strncmp("/dev/ttyACM",fn,11) ? "a modem" : "an USB modem", (const char *)fn);
    if (m.use_modem_link) {
        wvcon->print(", using link /dev/modem in config.\n");
        fn = "/dev/modem";
    } else {
        wvcon->print(".\n");    
    }
    WvConf cfg(argv[1],0660); // Create it read/write owner and group only
    static char s[]="Dialer Defaults";
    cfg.set(s, "Modem", fn);
    cfg.setint(s, "Baud", m.maxbaud());
    cfg.set(s, "Init1", m.is_isdn() ? "AT&F" : "ATZ");
    cfg.set(s, "Init2", init);
    cfg.set(s, "ISDN",  m.use_default_asyncmap() ? "1" : "0");
    cfg.set(s, "Modem Name",  m.modem_name ? (const char *)m.modem_name : "");
    cfg.set(s, "Modem Type",  m.is_isdn() ? "ISDN Terminal Adapter" :
            strncmp("/dev/ttyACM",fn,11) ? "Analog Modem" : "USB Modem");  
 
    if (m.modem_name)
        wvcon->print("Config for %s written to %s.\n", (const char *)m.modem_name, argv[1]);
    else
        wvcon->print("Modem configuration written to %s.\n", argv[1]);

    // insert some entries to let people know what they need to edit
    if (!cfg.get(s, "Phone"))
	cfg.set(s, "; Phone", "<Target Phone Number>");
    if (!cfg.get(s, "Username"))
	cfg.set(s, "; Username", "<Your Login Name>");
    if (!cfg.get(s, "Password"))
	cfg.set(s, "; Password", "<Your Password>");
    
    check_ppp_options();
    
    return 0;
}
