/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
 *
 * Implementation of the WvDialer smart-dialer class.  
 *
 */

#include "wvdialer.h"
#include "version.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <xplc/xplc.h>

// N_() is defined here just for internationalization.
#define N_(String)  String

static const char *	init_responses[] = {
	"ok",
	"error",
	NULL
};

static const char *	dial_responses[] = {
	"connect",
	"no carrier",
	"no dialtone",
	"no dial tone",
	"busy",
	"error",
	"voice",
	"fclass",
	"no answer",
	NULL
};

static const char *	prompt_strings[] = {
	"}!}",
	"!}!",
	NULL
};

static int messagetail_pid = 0;

//**************************************************
//       WvDialer Public Functions
//**************************************************

WvDialer::WvDialer( WvConf &_cfg, WvStringList *_sect_list, bool _chat_mode )
/***************************************************************************/
: WvStreamClone( 0 ),
    cfg(_cfg), log( "WvDial", WvLog::Debug ),
    err( log.split( WvLog::Error ) ),
    modemrx( "WvDial Modem", WvLog::Debug )
{
    ppp_pipe 		 = NULL;
    pppd_log		 = NULL;
    been_online 	 = false;
    stat 		 = Idle;
    offset 		 = 0;
    prompt_tries 	 = 0;
    last_rx 		 = last_execute = 0;
    prompt_response 	 = "";
    auto_reconnect_delay = 0;
    auto_reconnect_at    = 0;
    connected_at         = 0;
    phnum_count = 0;
    phnum_max = 0;      
    // tell wvstreams we need our own subtask
    uses_continue_select = true;

    brain = NULL;
    modem = NULL;
    sect_list = _sect_list;
    chat_mode = _chat_mode;
    
    log("WvDial: Internet dialer version " WVDIAL_VER_STRING "\n");

    // Ensure all sections in sect_list actually exist, warning if not.
    WvStringList::Iter iter(*sect_list);
    for(iter.rewind(); iter.next();) 
    {
    	if(cfg[*iter] == NULL) 
        {
    	    err(WvLog::Warning,
		"Warning: section [%s] does not exist in wvdial.conf.\n",
		*iter);
    	}
    }
 
   // Ensure all inherited sections exist, warning if not.
    WvConfigSectionList::Iter iter2 (cfg);
    for (iter2.rewind(); iter2.next();) 
    { 
        WvConfigSection & sect2 = *iter2;
        WvConfigEntry * entry = sect2["Inherits"];
        if (entry) 
        {
            WvString inherits = entry->value;
            if (cfg[inherits] == NULL)
                err( WvLog::Warning,  
                     "Warning: inherited section [%s] does not exist in wvdial.conf\n",
                     inherits);
       }
    }  
 
    // Activate the brain and read configuration.
    brain = new WvDialBrain(this);

    // init_modem() reads the config options.  It MUST run here!
    
    if(!init_modem())
    {
	// init_modem() printed an error
	stat = ModemError;
	return;
    }
    
    if (options.provider.len()) 
    {
	log( WvLog::Notice, "Dialing %s %s.\n",
		options.provider,
		options.product);
    }
    if (options.homepage.len()) 
    {
        log(WvLog::Notice, "Homepage of %s: %s\n",
	    options.provider.len() ? options.provider.cstr() : "this provider",
	    options.homepage);
    }

    if(options.auto_reconnect && options.idle_seconds > 0) 
    {
	err(WvLog::Notice,
	    "Idle Seconds = %s, disabling automatic reconnect.\n",
	    options.idle_seconds);
        options.auto_reconnect = false;
    }
    
    pppd_mon.setdnstests(options.dnstest1, options.dnstest2);
    pppd_mon.setcheckdns(options.check_dns);
    pppd_mon.setcheckdfr(options.check_dfr);
}

WvDialer::~WvDialer()
/*******************/
{
    terminate_continue_select();

    WVRELEASE(ppp_pipe);
    WVRELEASE(pppd_log);
    delete brain;
}

bool WvDialer::dial()
/*******************/
// Returns false on error, or true to go asynchronous while dialing.
{
    if(stat == Online)
    	return(true);
	
    if(stat != Idle) 
    {
	// error message has already been printed elsewhere
    	return(false);
    }

    if (!options.phnum) 
    {
	err( "Configuration does not specify a valid phone number.\n" );
    	stat = OtherError;
    }

    if (!options.login) 
    {
	err( "Configuration does not specify a valid login name.\n" );
    	stat = OtherError;
    }

    if (!options.password) 
    {
	err( "Configuration does not specify a valid password.\n" );
    	stat = OtherError;
    }
    
    if( stat != Idle )
	return( false );

    phnum_max = 0;
    if(options.phnum1.len()) 
    { 
	phnum_max++;
        if(options.phnum2.len()) 
	{ 
	    phnum_max++;
            if(options.phnum3.len()) 
	    { 
		phnum_max++;
          	if(options.phnum4.len()) 
		    phnum_max++;
	    }
	}
    }

    // we need to re-init the modem if we were online before.
    if(been_online && !init_modem())
	stat = ModemError;
    else
    {
	stat = Dial;
	connect_attempts = 1;
	dial_stat = 0;
	brain->reset();
    }
    
    return(true);
}

void WvDialer::hangup()
/*********************/
{
    WVRELEASE(ppp_pipe);
    
    if( !chat_mode )
      pppd_watch( 250 );
    
    if( stat != Idle ) 
    {
	time_t 	now;
	time( &now );
	log( "Disconnecting at %s", ctime( &now ) );
	del_modem();
	stat = Idle;
    }

    if (messagetail_pid > 0) 
    {
	kill(messagetail_pid, 15);
	messagetail_pid = 0;
    }
}

void WvDialer::pre_select( SelectInfo& si )
/*******************************************/
{
    if( isok() && stat != Online && stat != Idle
	&& time( NULL ) - last_execute > 1 )
    {
	// Pretend we have "data ready," so execute() gets called.
	// select() already returns true whenever the modem is readable,
	// but when we are doing a timeout (eg. PreDial1/2) for example,
	// we need to execute() even if no modem data is incoming.
	si.msec_timeout = 0;
    } 
    else 
    {
	WvStreamClone::pre_select( si );
    }
}


bool WvDialer::post_select( SelectInfo& si )
{
    if( isok() && stat != Online && stat != Idle
	&& time( NULL ) - last_execute > 1 )
    {
	// Pretend we have "data ready," so execute() gets called.
	// select() already returns true whenever the modem is readable,
	// but when we are doing a timeout (eg. PreDial1/2) for example,
	// we need to execute() even if no modem data is incoming.
	return true;
    } 
    else 
    {
	return WvStreamClone::post_select( si );
    }
}


bool WvDialer::isok() const
/*************************/
{
    bool b = ( !modem || modem->isok() )
	&& stat != ModemError && stat != OtherError;
    /*
     if (!b)
        err("Returning not ok!!\n" );
     */
    return( b );
}

char *WvDialer::connect_status() const
/*************************************/
{
    static char msg[ 160 ];

    switch( stat ) 
    {
    case PreDial2:
    case PreDial1:
    case Dial:
    case WaitDial:
	if( dial_stat == 1 )
	    strcpy( msg, N_("Last attempt timed out.  Trying again.") );
	else if( dial_stat == 2 )
	    strcpy( msg, N_("Modem did not connect last attempt.  Trying again.") );
	else if( dial_stat == 3 )
	    strcpy( msg, N_("No dial tone last attempt.  Trying again.") );
    	else if( dial_stat == 4 )
    	    strcpy( msg, N_("Busy signal on last attempt.  Trying again.") );
    	else if( dial_stat == 5 )
    	    strcpy( msg, N_("Voice answer on last attempt.  Trying again.") );
    	else if( dial_stat == 6 )
    	    strcpy( msg, N_("Fax answer on last attempt.  Trying again.") );
	else if( dial_stat == 7 )
            strcpy( msg, N_("No answer on last attempt.  Trying again.") );
    	else
    	    return( NULL );
	break;
    case WaitAnything:
    	strcpy( msg, N_("Waiting for a response from Internet Provider.") );
    	break;
    case WaitPrompt:
    	strcpy( msg, N_("Waiting for a prompt from Internet Provider.") );
    	break;
    case AutoReconnectDelay:
    	sprintf( msg, "Next attempt in 00:%02ld:%02ld.", 
    		 ( auto_reconnect_at - time( NULL ) ) / 60,
    		 ( auto_reconnect_at - time( NULL ) ) % 60 );
    	break;
    default:
    	return( NULL );
    }
    return( msg );
}


void WvDialer::pppd_watch( int ms )
/*********************************/
{
  // see if pppd has a message, analyse it and output to log
  
    if( pppd_log != NULL && pppd_log->isok() ) 
    {
    	char *line;
    
    	while ( (line = pppd_log->blocking_getline( ms )) )
    	{
	    WvString buffer1(pppd_mon.analyse_line( line ));
	    if (!!buffer1)
	    {
	    	log("pppd: %s\n", buffer1);
    	    }
        }
    }
}


void WvDialer::execute()
/**********************/
{
    WvStreamClone::execute();
    
    // the modem object might not exist, if we just disconnected and are
    // redialing.
    if( !modem && !init_modem() )
    	return;

    last_execute = time( NULL );
    
    if( !chat_mode )
      pppd_watch( 100 );
    
    switch( stat ) 
    {
    case Dial:
    case WaitDial:
    case PreDial1:
    case PreDial2:
	async_dial();
	break;
    case WaitAnything:
	// we allow some time after connection for silly servers/modems.
	if( modem->select( 500, true, false ) ) 
	{
	    // if any data comes in at all, switch to impatient mode.
	    stat = WaitPrompt;
	    last_rx = time( NULL );
	} 
	else if( time( NULL ) - last_rx >= 30 ) 
	{
	    // timed out - do what WaitPrompt would do on a timeout.
	    stat = WaitPrompt;
	} 
	else 
	{
	    // We prod the server with a CR character every once in a while.
	    // FIXME: Does this cause problems with login prompts?
	    modem->write( "\r", 1 );
	}
	break;
    case WaitPrompt:
    	async_waitprompt();
    	break;
    case Online:
	assert( !chat_mode );
    	// If already online, we only need to make sure pppd is still there.
	if( ppp_pipe && ppp_pipe->child_exited() ) 
	{
	    int pppd_exit_status = ppp_pipe->exit_status();
	    if( ppp_pipe->child_killed() ) 
	    {
		log( WvLog::Error, "PPP was killed! (signal = %s)\n",
		      ppp_pipe->exit_status() );
	    }

	    // we must delete the WvModem object so it can be recreated
	    // later; starting pppd seems to screw up the file descriptor.
	    hangup();
	    del_modem();
	    
	    time_t call_duration = time( NULL ) - connected_at;
	    
	    if( pppd_mon.auth_failed() ) 
	    {
	      log("Authentication error.\n"
		  "We failed to authenticate ourselves to the peer.\n"
		  "Maybe bad account or password?\n" );
	    } 
	    else 
	    {
		WvString msg = "";
		switch (pppd_exit_status) 
		{
		case  2: 
		    msg = "pppd options error"; 
		    break;
		case  3: 
		    msg = "No root priv error"; 
		    break;
		case  4: 
		    msg = "No ppp module error"; 
		    break;
		case 10: 
		    msg = "PPP negotiation failed"; 
		    break;
		case 11: 
		    msg = "Peer didn't authenticatie itself"; 
		    break;
		case 12: 
		    msg = "Link idle: Idle Seconds reached."; 
		    break;
		case 13: 
		    msg = "Connect time limit reached."; 
		    break;
		case 14: 
		    msg = "Callback negotiated, call should come back.";
		    break;
		case 15: 
		    msg = "Lack of LCP echo responses"; 
		    break;
		case 17: 
		    msg = "Loopback detected"; 
		    break;
		case 19: 
		    msg = "Authentication error.\n"
			"We failed to authenticate ourselves to the peer.\n"
			"Maybe bad account or password?";
		    break;
		}
		if (msg.len()) 
		{
		    // Note: exit code = %s is parsed by kinternet:
		    log("The PPP daemon has died: %s (exit code = %s)\n",
			msg, pppd_exit_status);
		    log("man pppd explains pppd error codes in more detail.\n");
		    err(WvLog::Notice, "I guess that's it for now, exiting\n");
		    if (pppd_exit_status == 12 && options.auto_reconnect)
			err(WvLog::Notice, "Idle parameter is passed to pppd\n"
			    "If you don't want an idle timeout per default,\n"
			    "comment out the idle parameter in /etc/ppp/options\n");
		    if (pppd_exit_status == 15) 
		    {
		        log("Provider is overloaded(often the case) or line problem.\n");
		    }
		    options.auto_reconnect = false;
		}
		msg = "";
		switch (pppd_exit_status) 
		{
		case  1: 
		    msg = "Fatal pppd error"; 
		    break;
		case  5: 
		    msg = "pppd received a signal"; 
		    break;
		case  6: 
		    msg = "Serial port lock failed"; 
		    break;
		case  7: 
		    msg = "Serial port open failed"; 
		    break;
		case  8: 
		    msg = "Connect script failed"; 
		    break;
		case  9: 
		    msg = "Pty program error"; 
		    break;
		case 16: 
		    msg = "A modem hung up the phone"; 
		    break;
		case 18: 
		    msg = "The init script failed"; 
		    break;
		}
		if (msg.len()) 
		{
		    log("The PPP daemon has died: %s (exit code = %s)\n",
			msg, pppd_exit_status);
		    log("man pppd explains pppd error codes in more detail.\n");
		    log(WvLog::Notice, "Try again and look into /var/log/messages "
			"and the wvdial and pppd man pages for more information.\n");
		} else
		    log(WvLog::Notice, "The PPP daemon has died. (exit code = %s)\n",
			pppd_exit_status);
	    }
	    
	    // check to see if we're supposed to redial automatically soon.
	    if( options.auto_reconnect && isok() ) 
	    {
		if( call_duration >= 45 )
		    // Connection was more than 45 seconds, so reset the
		    // "exponential backup timer".
		    auto_reconnect_delay = 0;

		// exponentially back up...
		auto_reconnect_delay *= 2;
		if( auto_reconnect_delay == 0 )
		    auto_reconnect_delay = 5;	  // start at 5 seconds
		if( auto_reconnect_delay > 600 )
		    auto_reconnect_delay = 600;  // no longer than 10 minutes

		auto_reconnect_at = time( NULL ) + auto_reconnect_delay;

		stat = AutoReconnectDelay;
		log( WvLog::Notice, "Auto Reconnect will be attempted in %s "
				    "seconds\n", 
				    	auto_reconnect_at - time( NULL ) );
	    }
	}
	break;
    case AutoReconnectDelay:
    	// If enough time has passed after ISP disconnected us that we should
    	// redial, do it...
    	// We can only get into this state if the Auto Reconnect option is
    	// enabled, so there's no point in checking the option here.
    	if( time( NULL ) >= auto_reconnect_at ) 
	{
    	    stat = Idle;
    	    dial();
    	}
    	break;
    case Idle:
    case ModemError:
    case OtherError:
    default:
	drain();
    	break;
    }
}


//**************************************************
//       WvDialer Private Functions
//**************************************************

void WvDialer::load_options()
/***************************/
{
    OptInfo opts[] = {
    // string options:
    	{ "Modem",           &options.modem,        NULL, "/dev/modem",     0 },
    	{ "Init1",           &options.init1,        NULL, "ATZ",            0 },
    	{ "Init2",           &options.init2,        NULL, "",               0 },
    	{ "Init3",           &options.init3,        NULL, "",               0 },
    	{ "Init4",           &options.init4,        NULL, "",               0 },
    	{ "Init5",           &options.init5,        NULL, "",               0 },
    	{ "Init6",           &options.init6,        NULL, "",               0 },
    	{ "Init7",           &options.init7,        NULL, "",               0 },
    	{ "Init8",           &options.init8,        NULL, "",               0 },
    	{ "Init9",           &options.init9,        NULL, "",               0 },
    	{ "Phone",           &options.phnum,        NULL, "",               0 },
    	{ "Phone1",          &options.phnum1,       NULL, "",               0 },
    	{ "Phone2",          &options.phnum2,       NULL, "",               0 },
    	{ "Phone3",          &options.phnum3,       NULL, "",               0 },
    	{ "Phone4",          &options.phnum4,       NULL, "",               0 },
    	{ "Dial Prefix",     &options.dial_prefix,  NULL, "",               0 },
    	{ "Area Code",       &options.areacode,     NULL, "",               0 },
    	{ "Dial Command",    &options.dial_cmd,     NULL, "ATDT",           0 },
    	{ "Username",        &options.login,        NULL, "",               0 },
    	{ "Login Prompt",    &options.login_prompt, NULL, "",               0 },
    	{ "Password",        &options.password,     NULL, "",               0 },
    	{ "Password Prompt", &options.pass_prompt,  NULL, "",               0 },
    	{ "PPPD Path",       &options.where_pppd,   NULL, "/usr/sbin/pppd", 0 },
        { "PPPD Option",     &options.pppd_option,  NULL, "",		    0 },
    	{ "Force Address",   &options.force_addr,   NULL, "",               0 },
    	{ "Remote Name",     &options.remote,       NULL, "*",              0 },
    	{ "Default Reply",   &options.default_reply,NULL, "ppp",	    0 },
        { "Country",         &options.country,      NULL, "",		    0 },
        { "Provider",        &options.provider,     NULL, "",		    0 },
        { "Product",         &options.product,      NULL, "",		    0 },
        { "Homepage",        &options.homepage,     NULL, "",		    0 },
        { "DialMessage1",    &options.dialmessage1, NULL, "",		    0 },
        { "DialMessage2",    &options.dialmessage2, NULL, "",		    0 },
        { "DNS Test1",       &options.dnstest1,     NULL, "www.suse.de",    0 },
        { "DNS Test2",       &options.dnstest2,     NULL, "www.suse.com",   0 },

    // int/bool options
    	{ "Baud",            NULL, &options.baud,          "", DEFAULT_BAUD },
    	{ "Carrier Check",   NULL, &options.carrier_check, "", true         },
    	{ "Stupid Mode",     NULL, &options.stupid_mode,   "", false        },
    	{ "New PPPD",	     NULL, &options.new_pppd, 	   "", true         },
    	{ "Auto Reconnect",  NULL, &options.auto_reconnect,"", true	    },
        { "Dial Attempts",   NULL, &options.dial_attempts, "", 0            },
    	{ "Abort on Busy",   NULL, &options.abort_on_busy, "", false	    },
    	{ "Abort on No Dialtone", NULL, &options.abort_on_no_dialtone, "", true },
        { "Compuserve",      NULL, &options.compuserve,    "", false        },
        { "Tonline",         NULL, &options.tonline,       "", false        },
        { "Auto DNS",        NULL, &options.auto_dns,      "", true         },
        { "Check DNS",       NULL, &options.check_dns,     "", true         },
        { "Check Def Route", NULL, &options.check_dfr,     "", true         },
        { "Idle Seconds",    NULL, &options.idle_seconds,  "", 0            },
        { "ISDN",            NULL, &options.isdn,          "", false        },
        { "Ask Password",    NULL, &options.ask_password,  "", false        },
        { "Dial Timeout",    NULL, &options.dial_timeout,  "", 60           },

    	{ NULL,		     NULL, NULL,                   "", 0            }
    };

    const char * d = "Dialer Defaults";

    for( int i=0; opts[i].name != NULL; i++ ) 
    {
    	if( opts[i].str_member == NULL ) 
	{
    	    // it's an int/bool option.
    	    *( opts[i].int_member ) =
		cfg.fuzzy_getint( *sect_list, opts[i].name,
		       cfg.getint( d, opts[i].name, opts[i].int_default ) );
    	} 
	else 
	{
    	    // it's a string option.
    	    *( opts[i].str_member ) = 
    	    		cfg.fuzzy_get( *sect_list, opts[i].name, 
    	    		    cfg.get( d, opts[i].name, opts[i].str_default ) );
    	}
    }

    // Support Init, as well as Init1, to make old WvDial people happy.
    const char * newopt = cfg.fuzzy_get( *sect_list, "Init",
	                        cfg.get( d, "Init", NULL ) );
    if( newopt ) 
	options.init1 = newopt;
}

bool WvDialer::init_modem()
/*************************/
{
    int	received, count;
    
    load_options();

    if (!options.modem) 
    {
	err( "Configuration does not specify a valid modem device.\n" );
    	stat = ModemError;
	return( false ); // if we get this error, we already have a problem.
    }
    
    for (count = 0; count < 3; count++)
    {
	// the buffer is empty.
	offset = 0;
    
	del_modem();
	
	// Open the modem...
	if( chat_mode ) 
	{
	    int flags = fcntl( STDIN_FILENO, F_GETFL );
	    if( ( flags & O_ACCMODE ) == O_RDWR ) 
	    {
		cloned = modem = new WvModemBase( STDIN_FILENO );
	    } 
	    else 
	    {
		// The following is needed for diald.
		// Stdin is not opened for read/write.
		::close( STDIN_FILENO );
		if( getenv( "MODEM" ) == NULL ) 
		{
		    err( "stdin not read/write and $MODEM not set\n" );
		    exit( 1 );
		}
		// Try to open device $MODEM.
		flags &= !O_ACCMODE;
		flags |= O_RDWR;
		int tty = ::open( getenv( "MODEM" ), flags );
		if( tty == -1 ) 
		{
		    err( "can't open %s: %m\n", getenv( "MODEM" ) );
		    exit( 1 );
		}
		cloned = modem = new WvModemBase( tty );
	    }
	} 
	else
	{
	    cloned = modem = new WvModem( options.modem, options.baud );
	}
	if( !modem->isok() ) 
	{
	    err( "Cannot open %s: %s\n", options.modem, modem->errstr() );
	    continue;
	}
	
	log( "Initializing modem.\n" );
	
	// make modem happy
	modem->print( "\r\r\r\r\r" );
	while( modem->select( 100, true, false ) )
	    modem->drain();
	
	// Send up to nine init strings, in order.
	int	init_count;
	for( init_count=1; init_count<=9; init_count++ ) 
	{
	    WvString *this_str;
	    switch( init_count ) 
	    {
	    case 1:    this_str = &options.init1;	break;
	    case 2:    this_str = &options.init2;	break;
	    case 3:    this_str = &options.init3;	break;
	    case 4:    this_str = &options.init4;	break;
	    case 5:    this_str = &options.init5;	break;
	    case 6:    this_str = &options.init6;	break;
	    case 7:    this_str = &options.init7;	break;
	    case 8:    this_str = &options.init8;	break;
	    case 9:
	    default:
		this_str = &options.init9;	break;
	    }
	    if( !! *this_str ) 
	    {
		modem->print( "%s\r", *this_str );
		log( "Sending: %s\n", *this_str );
		
		received = wait_for_modem( init_responses, 5000, true );
		switch( received ) 
		{
		case -1:
		    modem->print( "ATQ0\r" );
		    log( "Sending: ATQ0\n" );
		    received = wait_for_modem( init_responses, 500, true );
		    modem->print( "%s\r", *this_str );
		    log( "Re-Sending: %s\n", *this_str );
		    received = wait_for_modem( init_responses, 5000, true );
		    switch( received ) 
		    {
			case -1:
			    err( "Modem not responding.\n" );
			    return( false );
			case 1:
			    err( "Bad init string.\n" );
			    return( false );
		    }
		    break;
		case 1:
		    err( "Bad init string.\n" );
		    goto end_outer;
		}
	    }
	}

	// Everything worked fine.
	log( "Modem initialized.\n" );
	return( true );
	
	// allows us to exit the internal loop
      end_outer:
	continue;
    }
    
    // we tried 3 times and it didn't work.
    return( false );
}


void WvDialer::del_modem()
{
    assert(cloned == modem);
    
    if (modem)
    {
	modem->hangup();
	WVRELEASE(modem);
	cloned = NULL;
    }
}


WvModemBase *WvDialer::take_modem()
{
    WvModemBase *_modem;
    
    if (!modem)
	init_modem();
    
    _modem = modem;
    cloned = modem = NULL;
    
    return _modem;
}


void WvDialer::give_modem(WvModemBase *_modem)
{
    del_modem();
    cloned = modem = _modem;
}


void WvDialer::async_dial()
/*************************/
{
    int	received;

    if( stat == PreDial2 ) 
    {
    	// Wait for three seconds and then go to PreDial1.
    	continue_select(3000);
    	stat = PreDial1;
    	return;
    }
    
    if( stat == PreDial1 ) 
    {
	// Hit enter a few times.
	for( int i=0; i<3; i++ ) 
	{
	    modem->write( "\r", 1 );
	    continue_select(500);
	    if (!isok() || !modem)
		break;
	}
	stat = Dial;
	return;
    }
	
    if( stat == Dial ) 
    {
    	// Construct the dial string.  We use the dial command, prefix,
	// area code, and phone number as specified in the config file.
	WvString *this_str;
        switch( phnum_count ) 
	{  
            case 0:     
		this_str = &options.phnum;
		break;
            case 1:     
		this_str = &options.phnum1;
		break;
            case 2:     
		this_str = &options.phnum2;     
		break;
            case 3:     
		this_str = &options.phnum3;     
		break;
            case 4:
            default:
                this_str = &options.phnum4;     
		break;
        }

	WvString s( "%s%s%s%s%s\r", options.dial_cmd,
				 options.dial_prefix,
				 !options.dial_prefix ? "" : ",",
				 options.areacode,
				 *this_str );
	modem->print( s );
	log( "Sending: %s\n", s );
	log( "Waiting for carrier.\n" );

	stat = WaitDial;
    }

    received = async_wait_for_modem( dial_responses, true );
    
    switch( received ) 
    {
    case -1:	// nothing -- return control.
	if( time( NULL ) - last_rx  >= options.dial_timeout ) 
	{
	    log( WvLog::Warning, "Timed out while dialing.  Trying again.\n" );
	    stat = PreDial1;
	    connect_attempts++;
	    dial_stat = 1;
	    
	    //if Attempts in wvdial.conf is 0..dont do anything
	    if(options.dial_attempts != 0)
	    {
		if(check_attempts_exceeded(connect_attempts))
		{
		    hangup();
		}
	    }
	    
	}
	return;
    case 0:	// CONNECT
	
        /*
        if( chat_mode ) 
	 {
	  if( options.ask_password ) 
	 {
	    err( "Error: dial on demand does not work with option Ask Password = 1\n" );
	    exit( 1 );
	  }
	  
	  if( getuid() != 0 ) {
	    err( "Hint: if authentication does not work, start wvdial.dod once as user root\n" );
	  }
	  
	  WvPapChap papchap;
	  papchap.put_secret( options.login, options.password, options.remote );
	  if( getuid() == 0 ) {
	    if( papchap.isok_pap() == false ) {
	      err("Warning: Could not modify %s: %s\n",PAP_SECRETS, strerror(errno));
	    }
	    if( papchap.isok_chap() == false ) {
	      err("Warning: Could not modify %s: %s\n",CHAP_SECRETS, strerror(errno));
	    }
	  }
	}
        */
	
      	if( options.stupid_mode == true || options.tonline == true ) 
	{
	    if( chat_mode ) 
	    {
		log( "Carrier detected.  Chatmode finished.\n" );
		exit( 0 );
	    } 
	    else 
	    {
		log( "Carrier detected.  Starting PPP immediately.\n" );
		start_ppp();
	    }
	} 
	else 
	{
	    log( "Carrier detected.  Waiting for prompt.\n" );
	    stat = WaitAnything;
	}
	return;
    case 1:	// NO CARRIER
	log( WvLog::Warning, "No Carrier!  Trying again.\n" );
	stat = PreDial1;
	connect_attempts++;
	dial_stat = 2;
	continue_select(2000);

	//if Attempts in wvdial.conf is 0..dont do anything
	if(options.dial_attempts != 0)
	{
	    if(check_attempts_exceeded(connect_attempts))
	    {
		hangup();
	    }
	}
	
	return;
    case 2:	// NO DIALTONE
    case 3:	// NO DIAL TONE
	if( options.abort_on_no_dialtone == true ) 
	{
	    err( "No dial tone.\n" );
	    stat = ModemError;
	} 
	else 
	{
	    log( "No dial tone.  Trying again in 5 seconds.\n" );
	    stat = PreDial2;
	    connect_attempts++;
	    dial_stat = 3;
            //if Attempts in wvdial.conf is 0..dont do anything
            if(options.dial_attempts != 0)
	    {
		if(check_attempts_exceeded(connect_attempts))
		{
		    hangup();
		}
            }
	}
	return;
    case 4:	// BUSY
	if( options.abort_on_busy == true ) 
	{
	    err( "The line is busy.\n" );
	    stat = ModemError;
	} 
	else 
	{
	    if( phnum_count++ == phnum_max )
		phnum_count = 0;
	    if( phnum_count == 0 )
		log( WvLog::Warning, "The line is busy. Trying again.\n" );
	    else
		log( WvLog::Warning, "The line is busy. Trying other number.\n");
	    stat = PreDial1;
	    connect_attempts++;
	    dial_stat = 4;
	    continue_select(2000);
	}
	return;
    case 5:	// ERROR
	err( "Invalid dial command.\n" );
	stat = ModemError;
        //if Attempts in wvdial.conf is 0..dont do anything
        if(options.dial_attempts != 0)
	{
	    if(check_attempts_exceeded(connect_attempts))
	    {
               hangup();
	    }
        }
	return;
    case 6:	// VOICE
	log( "Voice line detected.  Trying again.\n" );
	connect_attempts++;
	dial_stat = 5;
	stat = PreDial2;
	
	//if Attempts in wvdial.conf is 0..dont do anything
	if(options.dial_attempts != 0)
	{
	    if(check_attempts_exceeded(connect_attempts))
	    {
		hangup();
	    }
	}
	
	return;
    case 7:	// FCLASS
	log( "Fax line detected.  Trying again.\n" );
	connect_attempts++;
	dial_stat = 6;
	stat = PreDial2;
	if(options.dial_attempts != 0)
	{
	    if(check_attempts_exceeded(connect_attempts))
	    {
		hangup();
	    }
	}
	return;
	
     case 8:    // NO ANSWER
        log( WvLog::Warning, "No Answer.  Trying again.\n" );
        stat = PreDial1;
        connect_attempts++;
        dial_stat = 7;
        if(options.dial_attempts != 0)
	{
            if(check_attempts_exceeded(connect_attempts))
	    {
		hangup();
            }
        }
        continue_select(2000);
        return;
    default:
	err( "Unknown dial response string.\n" );
	stat = ModemError;
	return;
    }
}


bool WvDialer::check_attempts_exceeded(int no_of_attempts)
/********************************************************/
{
    if(no_of_attempts > options.dial_attempts)
    {
	log( WvLog::Warning, "Maximum Attempts Exceeded..Aborting!!\n" );
	return true;
    }
    else
    {
	return false;
    }
}


static int set_echo( int desc, int value )
/****************************************/
{
    struct termios settings;
    
    if( isatty( desc ) != 1 )
	return 0;
    
    if( tcgetattr (desc, &settings) < 0 ) 
    {
	perror ("error in tcgetattr");
	return 0;
    }
    
    if( value )
	settings.c_lflag |= ECHO;
    else
	settings.c_lflag &= ~ECHO;
    
    if( tcsetattr (desc, TCSANOW, &settings) < 0 ) 
    {
	perror ("error in tcgetattr");
	return 0;
    }
    
    return 1;
}


int WvDialer::ask_password()
/**************************/
{
    char tmp[60];
    
    log("Please enter password (or empty password to stop):\n" );
    //  fflush( stdout );		// kinternet needs this - WvLog should do it
    // automagically
    // 
    set_echo( STDOUT_FILENO, 0 );
    fgets( tmp, 50, stdin );
    set_echo( STDOUT_FILENO, 1 );
    
    if( tmp[ strlen(tmp)-1 ] == '\n' )
	tmp[ strlen(tmp)-1 ] = '\0';
    
    options.password = tmp;
    
    return 1;
}


void WvDialer::start_ppp()
/************************/
{
    if( chat_mode ) exit(0); // pppd is already started...
    
    WvString	addr_colon( "%s:", options.force_addr );
    WvString	speed( options.baud );
    WvString	idle_seconds( options.idle_seconds );
    
    const char *dev_str = (const char *)options.modem;
    if (!(strncmp(options.modem, "/dev/", 5)))
	dev_str += 5;
    
    
    // open a pipe to access the messages of pppd
    if( pipe( pppd_msgfd ) == -1 ) 
    {
	err("pipe failed: %s\n", strerror(errno) );
	exit( EXIT_FAILURE );
    }
    pppd_log = new WvFDStream( pppd_msgfd[0] );
    WvString buffer1("%s", pppd_msgfd[1] );
    
    
    // open a pipe to pass password to pppd
    WvString buffer2;
    if (!options.password) 
    {
	if( pipe( pppd_passwdfd ) == -1 ) 
	{
	    err("pipe failed: %s\n", strerror(errno) );
	    exit( EXIT_FAILURE );
	}
	::write( pppd_passwdfd[1], (const char *) options.password, options.password.len() );
	::close( pppd_passwdfd[1] );
	buffer2.append("%s", pppd_passwdfd[0] );
    }
    
    char const *argv_raw[] = {
        options.where_pppd,
	speed,
	"modem",
	"crtscts",
	"defaultroute",
	"usehostname",
	"-detach",
	"user", options.login,
	(!!options.force_addr) ? (const char *)addr_colon : "noipdefault",
	options.new_pppd ? "call" : NULL, 
	options.new_pppd ? "wvdial" : NULL,
	options.new_pppd && options.auto_dns ? "usepeerdns"	   : NULL,
	options.new_pppd && options.isdn     ? "default-asyncmap"  : NULL,
	options.new_pppd && (!!options.pppd_option) ? (const char *) options.pppd_option : NULL,
	options.new_pppd && options.idle_seconds >= 0 ? "idle"	   : NULL, 
	options.new_pppd && options.idle_seconds >= 0 ? (const char *)idle_seconds : NULL, 
	"logfd", buffer1,
//	!!buffer2 ? "passwordfd" : NULL, !!buffer2 ? (const char *)buffer2 : NULL,
	NULL
    };
    
    /* Filter out NULL holes in the raw argv list: */
    char * argv[sizeof(argv_raw)];
    int argv_index = 0;
    for (unsigned int i = 0; i < sizeof(argv_raw)/sizeof(char *); i++) 
    {
	if (argv_raw[i])
            argv[argv_index++] = (char *)argv_raw[i];
    }
    argv[argv_index] = NULL;
    
    if( access( options.where_pppd, X_OK ) != 0 ) 
    {
        err( "Unable to run %s.\n", options.where_pppd );
        err( "Check permissions, or specify a \"PPPD Path\" option "
             "in wvdial.conf.\n" );
	return;
    }
    
    
    if (options.dialmessage1.len() || options.dialmessage2.len()) 
    {
        log( WvLog::Notice, "\
==========================================================================\n");
        log( WvLog::Notice, "> %s\n", options.dialmessage1);
        if (options.dialmessage2.len())
            log( WvLog::Notice, "> %s\n", options.dialmessage2);
        log( WvLog::Notice, "\
==========================================================================\n");
        if (options.homepage.len())
            log( WvLog::Notice, "Homepage of %s: %s\n",
		 options.provider.len() ? (const char *)options.provider : "this provider",
		 options.homepage);
    }
    
    time_t now;
    time( &now );
    log( WvLog::Notice, "Starting pppd at %s", ctime( &now ) );
    
    // PP - Put this back in, since we're not using passwordfd unless we're
    // SuSE... how did this work without this?
    WvPapChap   papchap;
    papchap.put_secret( options.login, options.password, options.remote );
    if( papchap.isok_pap() == false ) 
    {
        err( "Warning: Could not modify %s: %s\n"
             "--> PAP (Password Authentication Protocol) may be flaky.\n",
             PAP_SECRETS, strerror( errno ) );
    }
    if( papchap.isok_chap() == false ) 
    {
        err( "Warning: Could not modify %s: %s\n"
             "--> CHAP (Challenge Handshake) may be flaky.\n",
             CHAP_SECRETS, strerror( errno ) );
    }
 
    ppp_pipe = new WvPipe( argv[0], argv, false, false, false,
			   modem, modem, modem );

    log( WvLog::Notice, "Pid of pppd: %s\n", ppp_pipe->getpid() );

    stat 	 = Online;
    been_online  = true;
    connected_at = time( NULL );
}

void WvDialer::async_waitprompt()
/*******************************/
{
    int		received;
    const char *prompt_response;

    if( options.carrier_check == true ) 
    {
	if( !modem || !modem->carrier() ) 
	{
	    err( "Connected, but carrier signal lost!  Retrying...\n" );
	    stat = PreDial2;
	    return;
	}
    }
    
    received = async_wait_for_modem( prompt_strings, false, true );
    if( received >= 0 ) 
    {
	// We have a PPP sequence!
	log( "PPP negotiation detected.\n" );
	start_ppp();
    } 
    else if( received == -1 ) 
    {
	// some milliseconds must have passed without receiving anything,
	// or async_wait_for_modem() would not have returned yet.
	
    	// check to see if we are at a prompt.
        // Note: the buffer has been lowered by strlwr() already.

	prompt_response = brain->check_prompt( buffer );
	if( prompt_response != NULL )
	    modem->print( "%s\r", prompt_response );
    }
}


static void strip_parity( char * buf, size_t size )
/*************************************************/
// clear the parity bit on incoming data (to allow 7e1 connections)
{
    while( size-- > 0 )
    {
	*buf = *buf & 0x7f;
	buf++;
    }
}


int WvDialer::wait_for_modem( const char * strs[], 
			      int	timeout, 
			      bool	neednewline,
			      bool 	verbose )
/***********************************************/
{
    off_t	onset;
    char *	soff;
    int		result = -1;
    int		len;
    const char *ppp_marker = NULL;

    while( modem->select( timeout, true, false ) ) 
    {
	last_rx = time( NULL );
	onset = offset;
	offset += modem->read( buffer + offset, INBUF_SIZE - offset );
	
	// make sure we do not split lines TOO arbitrarily, or the
	// logs will look bad.
	while( offset < INBUF_SIZE && modem->select( 100, true, false ) )
	    offset += modem->read( buffer + offset, INBUF_SIZE - offset );
	
	// Make sure there is a NULL on the end of the buffer.
	buffer[ offset ] = '\0';
	
	// Now turn all the NULLs in the middle of the buffer to spaces, for
	// easier parsing.
	replace_char( buffer + onset, '\0', ' ', offset - onset );
	strip_parity( buffer + onset, offset - onset );
	replace_char( buffer + onset, '\0', ' ', offset - onset );
	
	if( verbose )
	    modemrx.write( buffer + onset, offset - onset );
	
	strlwr( buffer + onset );
	
	// Now we can search using strstr.
	for( result = 0; strs[ result ] != NULL; result++ )
	{
	    len = strlen( strs[ result ] );
	    soff = strstr( buffer, strs[ result ] );
	    
	    if( soff && ( !neednewline 
			  || strchr( soff, '\n' ) || strchr( soff, '\r' ) ) )
	    {
		memmove( buffer, soff + len,
			 offset - (int)( soff+len - buffer ) );
		offset -= (int)( soff+len - buffer );
		break;
	    }
	}
	
	if( strs[ result ] == NULL )
	    result = -1;
	
	// Search the buffer for a valid menu option...
	// If guess_menu returns an offset, we zap everything before it in
	// the buffer.  This prevents finding the same menu option twice.
	ppp_marker = brain->guess_menu( buffer );
	if (strs != dial_responses) 
	{
	    if( ppp_marker != NULL )
		memset( buffer, ' ', ppp_marker-buffer );
	}
	
	// Looks like we didn't find anything.  Is the buffer full yet?
	if( offset == INBUF_SIZE ) 
	{
	    // yes, move the second half to the first half for next time.
	    memmove( buffer, buffer + INBUF_SIZE/2,
		     INBUF_SIZE - INBUF_SIZE/2 );
	    offset = INBUF_SIZE/2;
	}
	
	if( result != -1 )
	    break;
    }
    
    buffer[ offset ] = 0;
    return( result ); // -1 == timeout
}

int WvDialer::async_wait_for_modem( const char * strs[], bool neednl, bool verbose )
/****************************************************************************/
{
    return( wait_for_modem( strs, 10, neednl, verbose ) );
}

void WvDialer::reset_offset()
/***************************/
{
    offset = 0;
    buffer[0] = '\0';
}
