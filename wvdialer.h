/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997, 1998 Worldvisions Computer Technology, Inc.
 *
 * Definition of the WvDialer smart-dialer class.
 *
 */

#ifndef __DIALER_H
#define __DIALER_H

#include <termios.h>

#include "strutils.h"
#include "wvconf.h"
#include "wvlog.h"
#include "wvmodem.h"
#include "wvpapchap.h"
#include "wvdialbrain.h"
#include "wvpipe.h"
#include "wvstreamclone.h"

#define INBUF_SIZE	1024
#define DEFAULT_BAUD	57600U

extern const char wvdial_help_text[];
extern const char wvdial_version_text[];

struct OptInfo
/************/
{
    char *	name;
    WvString *	str_member;
    int *	int_member;
    char *	str_default;
    int		int_default;
};

class WvConf;

class WvDialer : public WvStreamClone
/***********************************/
{
public:
    WvDialer( WvConf &_cfg, WvStringList *_sect_list );
    virtual ~WvDialer();

    WvModem *	modem;

    bool	dial();
    void	hangup();
    void	execute();

    enum Status {
	Idle,
	ModemError,
	OtherError,
	Online,
	Dial,
	PreDial1,
	PreDial2,
	WaitDial,
	WaitAnything,
	WaitPrompt,
	AutoReconnectDelay
    };

    Status status() const
        { return stat; }
    
    virtual bool select_setup(SelectInfo &si);
    virtual bool isok() const;

    int	   connect_attempts;
    int	   dial_stat;
    char * connect_status() const;

    friend class WvDialBrain;

    struct {
    	WvString	modem;
    	int		baud;
    	WvString	init1;
    	WvString	init2;
    	WvString	init3;
    	WvString	init4;
    	WvString	init5;
    	WvString	init6;
    	WvString	init7;
    	WvString	init8;
    	WvString	init9;
    	WvString	phnum;
    	WvString	dial_prefix;
    	WvString	dial_cmd;
    	WvString	login;
    	WvString	login_prompt;
    	WvString	password;
    	WvString	pass_prompt;
    	WvString	where_pppd;
    	WvString	force_addr;
    	WvString	remote;
    	WvString	default_reply;
    	WvString	isdn;
    	int		carrier_check;
    	int		stupid_mode;
    	int		new_pppd;
    	int		auto_reconnect;
    } options;

private:
    WvDialBrain *	brain;
    WvConf &		cfg;
    WvStringList *	sect_list;
    
    bool		been_online;
    time_t		connected_at;
    time_t		auto_reconnect_delay;
    time_t		auto_reconnect_at;
    WvPipe *		ppp_pipe;

    WvLog		log;
    WvLog		err;
    WvLog		modemrx;

    Status		stat;

    time_t		last_rx;
    time_t		last_execute;
    int			prompt_tries;
    WvString		prompt_response;

    void		load_options();
    
    bool		init_modem();
    void		async_dial();
    void		async_waitprompt();

    void		start_ppp();

    // The following members are for the wait_for_modem() function.
    int		wait_for_modem( char *strs[], int timeout, bool neednewline,
    				bool verbose = true);
    int		async_wait_for_modem( char * strs[], bool neednewline,
				      bool verbose = true);
    char	buffer[ INBUF_SIZE + 1 ];
    off_t	offset;
    void	reset_offset();

    // Called from WvDialBrain::guess_menu()
    bool 	is_pending() { return( modem->select( 1000 ) ); }
};

#endif // __DIALER_H
