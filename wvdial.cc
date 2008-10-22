/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2005 Net Integration Technologies, Inc.
 *
 * Standalone WvDial program, for testing the WvDialer class.
 *
 * Created:	Sept 30 1997		D. Coombs
 */

#include "wvargs.h"
#include "wvdialer.h"
#include "version.h"
#include "wvlog.h"
#include "wvlogrcv.h"
#include "wvlogfile.h"
#include "wvsyslog.h"
#include "wvcrash.h"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

volatile bool want_to_die = false;


// use no prefix string for app "Modem", and an arrow for everything else.
// This makes the output of the wvdial application look nicer.
class WvDialLogger : public WvLogConsole
/**************************************/
{
public:
    WvDialLogger() : WvLogConsole(dup(2)) // log to stderr (fd 2)
        { }

protected:
    virtual void _make_prefix(time_t now);
};


void WvDialLogger::_make_prefix(time_t now)
/*******************************/
{
    WvString name = last_source;
    if(name == "WvDial Modem") 
    {
	prefix = "";
	prelen = 0;
    } 
    else 
    {
	prefix = "--> ";
	prelen = 4;
    }
}

static void signalhandler(int sig)
/***********************************/
{
    fprintf(stderr, "Caught signal %d:  Attempting to exit gracefully...\n", sig);
    want_to_die = true;
    signal(sig, SIG_DFL);
}


bool haveconfig = false;
static bool config_cb(WvStringParm value, void *userdata)
{
    WvConf *cfg = reinterpret_cast<WvConf*>(userdata);
    if (!access(value, F_OK))
    {
	cfg->load_file(value);
	haveconfig = true;
	return true;
    }
    fprintf(stderr, "Cannot read `%s'\n", value.cstr());
    return false;
}


int main(int argc, char **argv)
/********************************/
{
#if DEBUG
    free( malloc( 1 ) ); // for electric fence
#endif
    
    WvDialLogger 	rc;
    WvSyslog		*syslog = NULL;
    WvLogFile           *filelog = NULL;
    UniConfRoot         uniconf("temp:");
    WvConf              cfg(uniconf);
    WvStringList	sections;
    WvStringList	cmdlineopts;
    WvLog		log( "WvDial", WvLog::Debug );
    WvString		homedir = getenv("HOME");
    
    bool chat_mode = false;
    bool write_syslog = true;
    
    signal(SIGTERM, signalhandler);
    signal(SIGINT, signalhandler);
    signal(SIGHUP, signalhandler);

    WvArgs args;
    args.set_version("WvDial " WVDIAL_VER_STRING "\n"
		     "Copyright (c) 1997-2005 Net Integration Technologies, "
		     "Inc.");
    args.set_help_header("An intelligent PPP dialer.");
    args.set_help_footer("Optional SECTION arguments refer to sections in "
			 "configuration file (usually)\n"
			 "/etc/wvdial.conf, $HOME/.wvdialrc or the file "
			 "specified by --config.\n"
			 "Specified sections are all read, with later ones "
			 "overriding previous ones.\n"
			 "Any options not in the listed sections are taken "
			 "from [Dialer Defaults].\n"
			 "\n"
			 "Also, optional OPTION=value parameters allow you "
			 "to override options within\n"
			 "the configuration files.\n");

    args.add_option('C', "config",
		    "use configfile instead of /etc/wvdial.conf",
		    "configfile", WvArgs::ArgCallback(&config_cb), &cfg);
    args.add_set_bool_option('c', "chat",
			     "used when running wvdial from pppd", chat_mode);
    args.add_reset_bool_option('n', "no-syslog",
			       "don't send output to SYSLOG", chat_mode);
    args.add_optional_arg("SECTION", true);
    args.add_optional_arg("OPTION=value", true);

    WvStringList remaining_args;
    args.process(argc, argv, &remaining_args);

    {
	WvStringList::Iter i(remaining_args);
	for (i.rewind(); i.next(); )
	{
	    if (strchr(i(), '=' ))
		cmdlineopts.append(new WvString(i()),true);
	    else
		sections.append(new WvString("Dialer %s", i()), true);
	}
    }

    if (sections.isempty())
	sections.append(new WvString("Dialer Defaults"), true);

    if( !haveconfig)
    {
	// Load the system file first...
	WvString stdconfig("/etc/wvdial.conf");
		
	if (!access(stdconfig, F_OK))
	    cfg.load_file(stdconfig);
	
	// Then the user specific one...
	if (homedir)
        {
	    WvString rcfile("%s/.wvdialrc", homedir);
			
	    if (!access(rcfile, F_OK))
		cfg.load_file(rcfile);
	}
    }
    
    // Inject all of the command line options on into the cfg file in a new
    // section called Command-Line if there are command line options.
    if (!cmdlineopts.isempty()) 
    {
        WvStringList::Iter i(cmdlineopts);
        for (i.rewind();i.next();)
        {
            char *name = i().edit();
            char *value = strchr(name,'=');
	    
            // Value should never be null since it can't get into the list
            // if it doesn't have an = in i()
            // 
            *value = 0;
	    value++;
            name = trim_string(name);
            value = trim_string(value);
            cfg.set("Command-Line", name, value);
        }
        sections.prepend(new WvString("Command-Line"), true);
    }
    
    if(!cfg.isok()) 
    {
	return 1;
    }
    
    if (chat_mode) 
    { 
	if (write_syslog) 
	{ 
	    WvString buf("wvdial[%s]", getpid()); 
	    syslog = new WvSyslog( buf, false, WvLog::Debug2, 
				   WvLog::Debug2 ); 
	} 
	else 
	{ 
	    // Direct logging to /dev/null as otherwise WvLog hasn't any 
	    // receivers and thus will use WvLogConsole to log to stderr. 
	    // That can disturb the communication with the modem on 
	    // stdin/stdout. - Fixes a bug reported by SUSE on 04/05/04
	    filelog = new WvLogFile( "/dev/null", WvLog::Debug2 ); 
	} 
    }
    
    WvDialer dialer(cfg, &sections, chat_mode);
    
    if (!chat_mode)
	if (dialer.isok() && dialer.options.ask_password)
	    dialer.ask_password();
    
    if (dialer.dial() == false)
	return  1;
    
    while (!want_to_die && dialer.isok() 
	   && dialer.status() != WvDialer::Idle) 
    {
	dialer.select(100);
	dialer.callback();
    }
    
    int retval;
    
    if (want_to_die)
    {
	// Probably dieing from a user signal
        retval = 2;
    }
    
    if ((dialer.status() != WvDialer::Idle) || !dialer.isok()) 
    {
	retval = 1;
    } 
    else 
    {
	retval = 0;
    }
    
    dialer.hangup();
    
    WVRELEASE(filelog);
    delete syslog;

    return(retval);
}
