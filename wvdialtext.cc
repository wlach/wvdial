/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 *
 * Stuff that wvdial prints...
 *
 * Created:	Sept 30 1997		D. Coombs
 */
#include "wvdialer.h"
#include "wvver.h"

const char wvdial_version_text[] = 
"WvDial " WVDIAL_VER_STRING
": Copyright (c) 1997-2002 Net Integration Technologies, Inc.\n";

const char wvdial_help_text[] = 
"Usage: wvdial { option || sect1 sect2 sect3 ... } \n"
"\n"
"  options:  --config configfile    use configfile instead of /etc/wvdial.conf\n"
" 				    or $HOME/.wvdial.conf\n"
"	     --chat		    used when running wvdial from pppd\n"
"  	     --help		    display this help and exit\n"
" 	     --version		    output version information and exit\n"
"	     --no-syslog	    don't send output to SYSLOG\n"
"\n"
"Optional \"sect\" arguments refer to sections in configuration file (usually)\n"
"/etc/wvdial.conf, $HOME/.wvdialrc or the file specified by --config.\n"
"Specified sections are all read, with later ones overriding previous ones.\n"
"Any options not in the listed sections are taken from [Dialer Defaults].\n"
"\n"
"Report bugs to wvdial@nit.ca\n";
