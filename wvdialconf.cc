/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997, 1998 Worldvisions Computer Technology, Inc.
 *
 * WvDial configuration utility.  Generates a basic wvdial.conf file.
 */
#include "wvmodemscan.h"
#include "wvconf.h"

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
	fprintf(stderr, "\n\nSorry, no modem was detected! "
		"Is it in use by another program?\n\n"
		"If your modem is correctly installed and should have "
		"been located,\nplease send mail to "
		"wvdial@worldvisions.ca.\n");
	return 1;
    }
    
    WvModemScanList::Iter i(l);
    
    i.rewind(); i.next();
    WvModemScan &m = *i.data();
    WvString fn = m.filename(), init = m.initstr();
    
    fprintf(stderr, "\nFound a modem on %s.\n", fn.str);
    
    WvConf cfg(argv[1]);
    static char s[]="Dialer Defaults";
    cfg.set(s, "Modem", fn.str);
    cfg.set(s, "Baud", m.maxbaud());
    cfg.set(s, "Init1", "ATZ");
    cfg.set(s, "Init2", init.str);

    // insert some entries to let people know what they need to edit
    if (!cfg.get(s, "Phone"))
	cfg.set(s, "; Phone", "<Target Phone Number>");
    if (!cfg.get(s, "Username"))
	cfg.set(s, "; Username", "<Your Login Name>");
    if (!cfg.get(s, "Password"))
	cfg.set(s, "; Password", "<Your Password>");
    
    return 0;
}
