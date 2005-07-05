/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
 *
 * Standalone WvDial program, for testing the WvDialer class.
 *
 * Created:	Sept 30 1997		D. Coombs
 */

#include "wvdialer.h"
#include "wvver.h"
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
    virtual void _make_prefix();
};


void WvDialLogger::_make_prefix()
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

static void print_version()
/*************************/
{
    printf( "%s", wvdial_version_text );
}

static void print_help()
/**********************/
{
    print_version();
    printf( "\n%s", wvdial_help_text );
}

static void signalhandler(int sig)
/***********************************/
{
    fprintf(stderr, "Caught signal %d:  Attempting to exit gracefully...\n", sig);
    want_to_die = true;
    signal(sig, SIG_DFL);
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
    int			haveconfig = 0;
    int			havecmdlineopts = 0;
    
    bool chat_mode = false;
    bool write_syslog = true;
    
    signal(SIGTERM, signalhandler);
    signal(SIGINT, signalhandler);
    signal(SIGHUP, signalhandler);

    if(argc > 1) 
    {
	for(int i=1; i < argc; i++) 
	{
	    if(!strcmp(argv[i], "--config" )) 
	    {	
		if (!access(argv[++i < argc ? i : i - 1], F_OK)) 
		{
		    haveconfig = 1;
		    cfg.load_file(WvString(argv[i]));
		    continue;
		} 
		else 
		{
		    log("Error: --config requires a valid argument\n");
		    print_help();
		    return 1;			    
		}
	    }
            if(strchr(argv[i], '=' )) 
	    {
                havecmdlineopts = 1;
                cmdlineopts.append(new WvString(argv[i]),true);
                continue;
            }
	    if(!strcmp(argv[i], "--chat" )) 
	    {
		chat_mode = true;
		continue;
	    }
	    if(!strcmp( argv[i], "--no-syslog" )) 
	    {
		write_syslog = false;
		continue;
	    }
	    if( !strcmp(argv[i], "--help")) 
	    {
		print_help();
		return 1;
	    }
	    else if(!strcmp(argv[i], "--version")) 
	    {
		print_version();
		return 1;
	    }
	    else if(argv[i][0] == '-') 
	    {
		print_help();
		return 1;
	    }
	    sections.append(new WvString("Dialer %s", argv[i]), true);
	}
    } 
    else 
    {
	sections.append(new WvString("Dialer Defaults"), true);
    }
    
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
    if (havecmdlineopts == 1) 
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
        sections.append(new WvString("Command-Line"), true);
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
