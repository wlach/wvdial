/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997, 1998, 1999 Worldvisions Computer Technology, Inc.
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
	    fprintf(stderr,
		"\n*** WARNING!  Line \"%s\"\n"
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
	fprintf(stderr, "Usage: %s <configfile-name>\n"
		"\t(create/update a wvdial.conf file automatically)\n",
		argv[0]);
	return 1;
    }
    
    fprintf(stderr, "Scanning your serial ports for a modem.\n\n");
    
    WvModemScanList l;
    while (!l.isdone())
	l.execute();
    
    if (l.count() < 1)
    {
	fprintf(stderr,
	  "\n\n"
	  "Sorry, no modem was detected!  "
	    "Is it in use by another program?\n"
	  "Did you configure it properly with setserial?\n\n"
		
	  "Please read the FAQ at http://www.worldvisions.ca/wvdial/\n\n"
		
	  "If you still have problems, send mail to "
	    "wvdial-list@worldvisions.ca.\n");
	return 1;
    }
    
    WvModemScanList::Iter i(l);
    
    i.rewind(); i.next();
    WvModemScan &m = i;
    WvString fn = m.filename(), init = m.initstr();
    
    fprintf(stderr, "\nFound a modem on %s.\n", (const char *)fn);
    
    WvConf cfg(argv[1]);
    static char s[]="Dialer Defaults";
    cfg.set(s, "Modem", fn);
    cfg.setint(s, "Baud", m.maxbaud());
    cfg.set(s, "Init1", "ATZ");
    cfg.set(s, "Init2", init);

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
