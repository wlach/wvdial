/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997, 1998 Worldvisions Computer Technology, Inc.
 *
 * Implementation of the WvDialer smart-dialer class.  
 *
 */
#include "wvdialer.h"
#include "wvver.h"

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


static char *	init_responses[] = {
	"ok",
	"error",
	NULL
};

static char *	dial_responses[] = {
	"connect",
	"no carrier",
	"no dialtone",
	"no dial tone",
	"busy",
	"error",
	"voice",
	"fclass",
	NULL
};

static char *	prompt_strings[] = {
	"}!}",
	"!}!",
	NULL
};




//**************************************************
//       WvDialer Public Functions
//**************************************************

WvDialer::WvDialer( WvConf& cfg, WvStringList& sect_list )
/********************************************************/
: WvStreamClone( (WvStream **)&modem ),
    log( "WvDial", WvLog::Debug ), err( log.split( WvLog::Error ) ),
    modemrx( "Modem", WvLog::Debug )
{
    modem 		 = NULL;
    ppp_pipe 		 = NULL;
    been_online 	 = false;
    stat 		 = Idle;
    offset 		 = 0;
    prompt_tries 	 = 0;
    last_rx 		 = last_execute = 0;
    prompt_response 	 = "";
    auto_reconnect_delay = 0;
    auto_reconnect_at    = 0;
    connected_at         = 0;
    
    log( "WvDial: Internet dialer version " WVDIAL_VER_STRING "\n" );

    // Ensure all sections in sect_list actually exist, warning if not.
    WvStringList::Iter	iter( sect_list );
    for( iter.rewind(); iter.next(); ) {
    	if( cfg[iter] == NULL ) {
    	    err( WvLog::Warning,
		 "Warning: section [%s] does not exist in wvdial.conf.\n",
    	    	 iter );
    	}
    }

    // Activate the brain and read configuration.
    brain = new WvDialBrain( this );
    load_options( cfg, sect_list );

    if( !options.modem[0] ) {
	err( "Configuration does not specify a valid modem device.\n" );
    	stat = ModemError;
	return; // if we get this error, we already have a problem.
    }
    
    if( !init_modem() )
    {
	// init_modem() printed an error
	stat = ModemError;
	return;
    }
}

WvDialer::~WvDialer()
/*******************/
{
    if( ppp_pipe )
	delete ppp_pipe;
    if( modem )
	delete modem;
    if( brain )
    	delete brain;
}

bool WvDialer::dial()
/*******************/
// Returns false on error, or true to go asynchronous while dialing.
{
    if( stat == Online ) {
    	return( true );
    } else if( stat != Idle ) {
	// (error message has already been printed elsewhere)
    	// err( "Modem is not ready to dial.\n" );
    	return( false );
    }

    if( !options.phnum[0] ) {
	err( "Configuration does not specify a valid phone number.\n" );
    	stat = OtherError;
    }

    if( !options.login[0] ) {
	err( "Configuration does not specify a valid login name.\n" );
    	stat = OtherError;
    }

    if( !options.password[0] ) {
	err( "Configuration does not specify a valid password.\n" );
    	stat = OtherError;
    }
    
    if( stat != Idle )
	return( false );

    // we need to re-init the modem if we were online before.
    if( been_online && !init_modem() )
	stat = ModemError;
    else
    {
	stat = Dial;
	connect_attempts = 1;
	dial_stat = 0;
	brain->reset();
    }
    
    return( true );
}

void WvDialer::hangup()
/*********************/
{
    if( ppp_pipe ) {
    	delete ppp_pipe;
	ppp_pipe = NULL;
    }

    if( stat != Idle ) {
	time_t 	now;
	time( &now );
	log( "Disconnecting at %s", ctime( &now ) );
	if( modem )
	{
	    modem->hangup();
	    delete modem;
	    modem = NULL;
	}
	stat = Idle;
    }
}

bool WvDialer::select_setup(SelectInfo &si)
/**************************************************************************/
{
    if( isok() && stat != Online && stat != Idle
	       && time( NULL ) - last_execute > 1 )
    {
	// Pretend we have "data ready," so execute() gets called.
	// select() already returns true whenever the modem is readable,
	// but when we are doing a timeout (eg. PreDial1/2) for example,
	// we need to execute() even if no modem data is incoming.
	return( true );
    } else {
	return WvStreamClone::select_setup( si );
    }
}

bool WvDialer::isok() const
/*************************/
{
    bool b = (!modem || modem->isok())
	&& stat != ModemError && stat != OtherError;
    if (!b)
	fprintf(stderr, "Returning not ok!!\n");
    return b;
}

char * WvDialer::connect_status() const
/*************************************/
{
    static char msg[ 160 ];

    switch( stat ) {
    case PreDial2:
    case PreDial1:
    case Dial:
    case WaitDial:
    	if( dial_stat == 1 )
    	    strcpy( msg, "Last attempt timed out.  Trying again." );
    	else if( dial_stat == 2 )
    	    strcpy( msg, "Modem did not connect last attempt.  Trying again." );
    	else if( dial_stat == 3 )
    	    strcpy( msg, "No dial tone last attempt.  Trying again." );
    	else if( dial_stat == 4 )
    	    strcpy( msg, "Busy signal on last attempt.  Trying again." );
    	else if( dial_stat == 5 )
    	    strcpy( msg, "Voice answer on last attempt.  Trying again." );
    	else if( dial_stat == 6 )
    	    strcpy( msg, "Fax answer on last attempt.  Trying again." );
    	else
    	    return( NULL );
	break;
    case WaitAnything:
    	strcpy( msg, "Waiting for a response from Internet Provider." );
    	break;
    case WaitPrompt:
    	strcpy( msg, "Waiting for a prompt from Internet Provider." );
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

void WvDialer::execute()
/**********************/
{
    // the modem object might not exist, if we just disconnected and are
    // redialing.
    if( !modem && !init_modem() )
    	return;

    last_execute = time( NULL );
    
    switch( stat ) {
    case Dial:
    case WaitDial:
    case PreDial1:
    case PreDial2:
    	async_dial();
    	break;
    case WaitAnything:
	// we allow some time after connection for silly servers/modems.
	if( modem->select( 500 ) ) {
	    // if any data comes in at all, switch to impatient mode.
	    stat = WaitPrompt;
	    last_rx = time( NULL );
	} else if( time( NULL ) - last_rx >= 30 ) {
	    // timed out - do what WaitPrompt would do on a timeout.
	    stat = WaitPrompt;
	} else {
	    // We prod the server with a CR character every once in a while.
	    // FIXME: Does this cause problems with login prompts?
	    modem->write( "\r", 1 );
	}
	break;
    case WaitPrompt:
    	async_waitprompt();
    	break;
    case Online:
    	// If already online, we only need to make sure pppd is still there.
	if( ppp_pipe && ppp_pipe->child_exited() ) {
	    if( ppp_pipe->child_killed() ) {
		log( WvLog::Error, "PPP was killed! (signal = %s)\n",
		      ppp_pipe->exit_status() );
	    } else {
		log( WvLog::Error, "PPP daemon has died! (exit code = %s)\n",
		      ppp_pipe->exit_status() );
	    }
	    
	    // set up to dial again, if it is requested.
	    // we must delete the WvModem object so it can be recreated
	    // later; starting pppd seems to screw up the file descriptor.
	    hangup();
	    delete( modem );
	    modem = NULL;

	    // check to see if we're supposed to redial automatically soon.
	    if( options.auto_reconnect && isok() ) {
		if( time( NULL ) - connected_at >= 45 )
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
    	if( time( NULL ) >= auto_reconnect_at ) {
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

void WvDialer::load_options( WvConf& cfg, WvStringList& sect_list )
/*****************************************************************/
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
    	{ "Dial Prefix",     &options.dial_prefix,  NULL, "",               0 },
    	{ "Dial Command",    &options.dial_cmd,     NULL, "ATDT",           0 },
    	{ "Username",        &options.login,        NULL, "",               0 },
    	{ "Login Prompt",    &options.login_prompt, NULL, "",               0 },
    	{ "Password",        &options.password,     NULL, "",               0 },
    	{ "Password Prompt", &options.pass_prompt,  NULL, "",               0 },
    	{ "PPPD Path",       &options.where_pppd,   NULL, "/usr/sbin/pppd", 0 },
    	{ "Force Address",   &options.force_addr,   NULL, "",               0 },
    	{ "Remote Name",     &options.remote,       NULL, "*",              0 },
    	{ "Default Reply",   &options.default_reply,NULL, "ppp",	    0 },
    	{ "ISDN",	     &options.isdn,	    NULL, "",		    0 },

    // int/bool options
    	{ "Baud",            NULL, &options.baud,          "", DEFAULT_BAUD },
    	{ "Carrier Check",   NULL, &options.carrier_check, "", true         },
    	{ "Stupid Mode",     NULL, &options.stupid_mode,   "", false        },
    	{ "New PPPD",	     NULL, &options.new_pppd, 	   "", false	    },
    	{ "Auto Reconnect",  NULL, &options.auto_reconnect,"", true	    },
    	{ NULL,		     NULL, NULL,                   "", 0            }
    };

    char *	d = "Dialer Defaults";

    for( int i=0; opts[i].name != NULL; i++ ) {
    	if( opts[i].str_member == NULL ) {
    	    // it's an int/bool option.
    	    *( opts[i].int_member ) =
    	    		cfg.fuzzy_get( sect_list, opts[i].name,
    	    		    cfg.get( d, opts[i].name, opts[i].int_default ) );
    	} else {
    	    // it's a string option.
    	    *( opts[i].str_member ) = 
    	    		cfg.fuzzy_get( sect_list, opts[i].name, 
    	    		    cfg.get( d, opts[i].name, opts[i].str_default ) );
    	}
    }
}

bool WvDialer::init_modem()
/*************************/
{
    int	received;

    // Open the modem...
    if( modem ) delete modem;
    
    modem = new WvModem( options.modem, options.baud );
    if( !modem->isok() ) {
	err( "Cannot open %s: %s\n", options.modem, modem->errstr() );
	return( false );
    }

    log( "Initializing modem.\n" );
    
    // make modem happy
    modem->print( "\r\r\r\r\r" );
    while( modem->select( 100 ) )
	modem->drain();

    // Send up to nine init strings, in order.
    int	init_count;
    for( init_count=1; init_count<=9; init_count++ ) {
    	WvString *	this_str;
    	switch( init_count ) {
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
    	if( !! *this_str ) {
    	    modem->print( "%s\r", *this_str );
    	    log( "Sending: %s\n", *this_str );

    	    received = wait_for_modem( init_responses, 5000, true );
    	    switch( received ) {
    	    case -1:
    	    	err( "Modem not responding.\n" );
    	    	return( false );
    	    case 1:
    	    	err( "Bad init string.\n" );
    	    	return( false );
    	    }
    	}
    }

    // If we're using an ISDN modem, allow one second for the SPID
    // settings to kick in.  It dials so fast anyway that no one will care.
    if( options.isdn[0] )
	sleep( 1 );
    
    // Everything worked fine.
    log( "Modem initialized.\n" );
    return( true );
}

void WvDialer::async_dial()
/*************************/
{
    int	received;

    if( stat == PreDial2 ) {
    	// Wait for three seconds and then go to PreDial1.
    	usleep( 3 * 1000 * 1000 );
    	stat = PreDial1;
    	return;
    }
    
    if( stat == PreDial1 ) {
	// Hit enter a few times.
	for( int i=0; i<3; i++ ) {
	    modem->write( "\r", 1 );
	    usleep( 500 * 1000 );
	}
	stat = Dial;
	return;
    }
	
    if( stat == Dial ) {
    	// Construct the dial string.  We use the dial command, prefix, and
    	// phone number as specified in the config file.
	WvString s( "%s %s%s\r", options.dial_cmd,
				 options.dial_prefix,
				 options.phnum );
	modem->print( s );
	log( "Sending: %s\n", s );
	log( "Waiting for carrier.\n" );

	stat = WaitDial;
    }

    received = async_wait_for_modem( dial_responses, true );
    switch( received ) {
    case -1:	// nothing -- return control.
	if( last_rx - time( NULL ) >= 60 ) {
	    log( WvLog::Warning, "Timed out while dialing.  Trying again.\n" );
	    stat = PreDial1;
	    connect_attempts++;
	    dial_stat = 1;
	}
	return;
    case 0:	// CONNECT
	if( options.stupid_mode == true ) {
	    log( "Carrier detected.  Starting ppp.\n" );
	    start_ppp();
	} else {
	    log( "Carrier detected.  Waiting for prompt.\n" );
	    stat = WaitAnything;
	}
	return;
    case 1:	// NO CARRIER
	log( WvLog::Warning, "No Carrier!  Trying again.\n" );
	stat = PreDial1;
	connect_attempts++;
	dial_stat = 2;
	sleep( 2 );
	return;
    case 2:	// NO DIALTONE
    case 3:	// NO DIAL TONE
	err( "No dial tone.  Trying again in 5 seconds.\n" );
	stat = PreDial2;
	connect_attempts++;
	dial_stat = 3;
	return;
    case 4:	// BUSY
	log( WvLog::Warning, "The line is busy.  Trying again.\n" );
	stat = PreDial1;
	connect_attempts++;
	dial_stat = 4;
	sleep( 2 );
	return;
    case 5:	// ERROR
	err( "Invalid dial command.\n" );
	stat = ModemError;
	return;
    case 6:	// VOICE
    	log( "Voice line detected.  Trying again.\n" );
	connect_attempts++;
	dial_stat = 5;
    	stat = PreDial2;
    	return;
    case 7:	// FCLASS
    	log( "Fax line detected.  Trying again.\n" );
	connect_attempts++;
	dial_stat = 6;
    	stat = PreDial2;
    	return;
    default:
	err( "Unknown dial response string.\n" );
	stat = ModemError;
	return;
    }
}

void WvDialer::start_ppp()
/************************/
{
    WvString	addr_colon( "%s:", options.force_addr );

    char const *argv[] = {
	options.where_pppd,
	"modem",
	"crtscts",
	"defaultroute",
	"usehostname",
	"-detach",
	"user", options.login,
	options.force_addr[0] ? (const char *)addr_colon : "noipdefault",
	options.new_pppd ? "call" : NULL, 
	options.new_pppd ? "wvdial" : NULL,
	NULL
    };

    if( access( options.where_pppd, X_OK ) != 0 ) {
        err( "Unable to run %s.\n", options.where_pppd );
        err( "Check permissions, or specify a \"PPPD Path\" option "
             "in wvdial.conf.\n" );
    	return;
    }

    WvPapChap	papchap;
    papchap.put_secret( options.login, options.password, options.remote );
    if( papchap.isok_pap() == false ) {
    	err( "Warning: Could not modify %s: %s\n"
    	     "--> PAP (Password Authentication Protocol) may be flaky.\n",
    	     PAP_SECRETS, strerror( errno ) );
    }
    if( papchap.isok_chap() == false ) {
    	err( "Warning: Could not modify %s: %s\n"
    	     "--> CHAP (Challenge Handshake) may be flaky.\n",
	     CHAP_SECRETS, strerror( errno ) );
    }

    time_t	now;
    time( &now );
    log( WvLog::Notice, "Starting pppd at %s", ctime( &now ) );

    ppp_pipe = new WvPipe( argv[0], argv, false, false, false,
			   modem, modem, modem );

    stat 	 = Online;
    been_online  = true;
    connected_at = time( NULL );
}

void WvDialer::async_waitprompt()
/*******************************/
{
    int		received;
    const char *prompt_response;

    if( options.carrier_check == true ) {
	if( !modem || !modem->carrier() ) {
	    stat = ModemError;
    	    return;
	}
    }

    received = async_wait_for_modem( prompt_strings, false, true );
    if( received >= 0 ) {
    	// We have a PPP sequence!
    	log( "PPP negotiation detected.\n" );
    	start_ppp();
    } else if( received == -1 ) {
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


int WvDialer::wait_for_modem( char * 	strs[], 
			      int	timeout, 
			      bool	neednewline,
			      bool 	verbose )
/***********************************************/
{
    off_t	onset;
    char *	soff;
    int		result;
    int		len;

    while( modem->select( timeout ) ) {
	last_rx = time( NULL );
    	onset = offset;
	offset += modem->read( buffer + offset, INBUF_SIZE - offset );
	
	// make sure we do not split lines TOO arbitrarily, or the
	// logs will look bad.
	while( offset < INBUF_SIZE && modem->select( 100 ) )
	    offset += modem->read( buffer + offset, INBUF_SIZE - offset );

	// Make sure there is a NULL on the end of the buffer.
	buffer[ offset ] = '\0';

	// Now turn all the NULLs in the middle of the buffer to spaces, for
	// easier parsing.
	replace_char( buffer + onset, '\0', ' ', offset - onset );
	strip_parity( buffer + onset, offset - onset );

	if( verbose )
	    modemrx.write( buffer + onset, offset - onset );

	strlwr( buffer + onset );

	// Search the buffer for a valid menu option...
	// If guess_menu returns an offset, we zap everything before it in
	// the buffer.  This prevents finding the same menu option twice.
	const char *ppp_marker = brain->guess_menu( buffer );
	if( ppp_marker != NULL )
	    memset( buffer, ' ', ppp_marker-buffer );
	
	// Now we can search using strstr.
	for( result = 0; strs[ result ] != NULL; result++ ) {
	    len = strlen( strs[ result ] );
	    soff = strstr( buffer, strs[ result ] );
	    
	    if( soff && ( !neednewline 
			 || strchr( soff, '\n' ) || strchr( soff, '\r' ) ) )
	    {
		memmove( buffer, soff + len,
			 offset - (int)( soff+len - buffer ) );
		offset -= (int)( soff+len - buffer );
		return( result );
	    }
	}

	// Looks like we did not find anything.  Is the buffer full yet?
	if( offset == INBUF_SIZE ) {
	    // yes, move the second half to the first half for next time.
	    memmove( buffer, buffer + INBUF_SIZE/2,
		     INBUF_SIZE - INBUF_SIZE/2 );
	    offset = INBUF_SIZE/2;
	}
    }
    
    buffer[ offset ] = 0;
    return( -1 ); // timeout
}

int WvDialer::async_wait_for_modem( char * strs[], bool neednl, bool verbose )
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
