/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2000 Net Integration Technologies, Inc.
 *
 * Standalone WvDial program, for testing the WvDialer class.
 *
 * Created:	Sept 30 1997		D. Coombs
 */

#include "wvdialer.h"
#include "wvver.h"
#include "wvlog.h"
#include "wvlogrcv.h"
#include "wvsyslog.h"
#include "wvconf.h"

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
    WvDialLogger() : WvLogConsole( dup( 2 ) ) // log to stderr (fd 2)
        { }
    virtual ~WvDialLogger();
protected:
    virtual void _make_prefix();
};


WvDialLogger::~WvDialLogger()
/***************************/
{
}


void WvDialLogger::_make_prefix()
/*******************************/
{
    const char * name = appname( last_source );
    if( !strcmp( name, "Modem" ) ) {
	prefix = "";
	prelen = 0;
    } else {
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

static void signalhandler( int parm )
/***********************************/
{
    printf( "Caught signal #%d!  Attempting to exit gracefully...\n", parm );
    want_to_die = true;
}


int main( int argc, char ** argv )
/********************************/
{
#if DEBUG
    free( malloc( 1 ) ); // for electric fence
#endif

    WvDialLogger 	rc;
    WvSyslog		*syslog = NULL;
    WvConf		cfg( "/etc/wvdial.conf", 0600 );
    WvStringList	*sections = new WvStringList;
    WvLog		log( "WvDial", WvLog::Debug );
    char *		homedir = getenv("HOME");

    bool chat_mode = false;
    
    signal( SIGTERM, signalhandler );
    signal( SIGINT, signalhandler );
    signal( SIGHUP, signalhandler );
    
    if( !cfg.isok() || !cfg.isclean() ) {
	return( 1 );
    }
    
    if (homedir)
    {
	WvString rcfile("%s/.wvdialrc", homedir);
	
	if (!access(rcfile, F_OK))
	    cfg.load_file(rcfile);
    }

    if( argc > 1 ) {
    	for( int i=1; i < argc; i++ ) {
	    if( !strcmp( argv[i], "--chat" ) ) {
		syslog = new WvSyslog( "WvDial", false, WvLog::Debug2, 
				       WvLog::Debug2 );
		chat_mode = true;
		continue;
	    }
	    if( !strcmp( argv[i], "--help" ) ) {
	    	print_help();
	    	return( 1 );
	    }
	    else if( !strcmp( argv[i], "--version" ) ) {
	    	print_version();
	    	return( 1 );
	    }
	    else if( argv[i][0] == '-' ) {
		print_help();
		return( 1 );
	    }
    	    sections->append( new WvString( "Dialer %s", argv[i] ), true );
    	}
    } else {
	sections->append( new WvString( "Dialer Defaults" ), true);
    }
    
    WvDialer dialer( cfg, sections, chat_mode );
	
    if( dialer.dial() == false )
	return  1;
	
    while( !want_to_die && dialer.isok() 
	  && dialer.status() != WvDialer::Idle ) {
	dialer.select( 100 );
	dialer.execute();
	
	if (dialer.weird_pppd_problem) {
	    dialer.weird_pppd_problem = false;
	    log(WvLog::Warning,
		"pppd error!  Look at files in /var/log for an "
		"explanation.\n");
	}
    }

    dialer.hangup();

    if( syslog )
	delete syslog;

    return( 0 );
}
