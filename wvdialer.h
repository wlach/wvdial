/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
 *
 * Definition of the WvDialer smart-dialer class.
 *
 */

#ifndef __DIALER_H
#define __DIALER_H

#include <termios.h>

#include "strutils.h"
#include "wvconfemu.h"
#include "wvlog.h"
#include "wvmodem.h"
#include "wvpapchap.h"
#include "wvdialbrain.h"
#include "wvpipe.h"
#include "wvstreamclone.h"
#include "wvdialmon.h"

#define INBUF_SIZE	1024
#define DEFAULT_BAUD	57600U

extern const char wvdial_help_text[];
extern const char wvdial_version_text[];

struct OptInfo
/************/
{
    const char * name;
    WvString *	 str_member;
    int *	 int_member;
    const char * str_default;
    int		 int_default;
};

class WvConf;

class WvDialer : public WvStreamClone
/***********************************/
{
public:
    WvDialer( WvConf &_cfg, WvStringList *_sect_list, bool _chat_mode = false );
    virtual ~WvDialer();
   
    bool	dial();
    void	hangup();
    void	execute();
   
    bool check_attempts_exceeded(int connect_attempts);

    void	pppd_watch( int w );
   
    int         ask_password();
   
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
   
    time_t auto_reconnect_time() const
        { return (auto_reconnect_at - time(NULL)); }
   
    virtual void pre_select(SelectInfo &si);
    virtual bool post_select(SelectInfo &si);
    virtual bool isok() const;
   
    int	  connect_attempts;
    int	  dial_stat;
    char   *connect_status() const;
    bool   init_modem();
    void   del_modem();
    WvModemBase *take_modem();
    void give_modem(WvModemBase *_modem);
   
    friend class WvDialBrain;
   
    struct {
	WvString	        modem;
	int		baud;
	WvString	        init1;
	WvString	        init2;
	WvString	        init3;
	WvString	        init4;
	WvString	        init5;
	WvString	        init6;
	WvString	        init7;
	WvString	        init8;
	WvString	        init9;
	WvString	        phnum;
	WvString	        phnum1;
	WvString	        phnum2;
	WvString	        phnum3;
	WvString	        phnum4;
	WvString	        dial_prefix;
	WvString	        areacode;
	WvString	        dial_cmd;
	WvString	        login;
	WvString	        login_prompt;
	WvString	        password;
	WvString	        pass_prompt;
	WvString	        where_pppd;
	WvString	        pppd_option;
	WvString	        force_addr;
	WvString	        remote;
	WvString	        default_reply;
	WvString         country;
	WvString         provider;
	WvString         product;
	WvString         homepage;
	WvString         dialmessage1;
	WvString         dialmessage2;
	WvString         dnstest1, dnstest2;
	int              carrier_check;
	int		stupid_mode;
	int		new_pppd;
	int		auto_reconnect;
	int		abort_on_busy;
	int		abort_on_no_dialtone;
	int              dial_attempts;
	int              compuserve;
	int              tonline;
	int              auto_dns;
	int              check_dns;
	int              check_dfr;
	int              idle_seconds;
	int              isdn;
	int              ask_password;
	int              dial_timeout;
       
    } options;
   
   
    WvDialMon pppd_mon;               // class to analyse messages of pppd
   
   
private:
    WvDialBrain  *brain;
    WvConf       &cfg;
    WvStringList *sect_list;
    WvModemBase *modem;
   
    bool		chat_mode;
   
    bool		been_online;
    time_t	connected_at;
    time_t	auto_reconnect_delay;
    time_t	auto_reconnect_at;
    WvPipe       *ppp_pipe;
   
    int     	phnum_count;
    int     	phnum_max;  
   
    WvLog	log;
    WvLog	err;
    WvLog	modemrx;
   
    Status	stat;
   
    time_t	last_rx;
    time_t	last_execute;
    int		prompt_tries;
    WvString	prompt_response;
   
    void		load_options();
   
    void		async_dial();
    void		async_waitprompt();
   
    void		start_ppp();
   
    // The following members are for the wait_for_modem() function.
    int		wait_for_modem( const char *strs[], int timeout, bool neednewline,
				bool verbose = true);
    int		async_wait_for_modem( const char * strs[], bool neednewline,
				      bool verbose = true);
    char	        buffer[ INBUF_SIZE + 1 ];
    off_t	offset;
    void	        reset_offset();
   
    // Called from WvDialBrain::guess_menu()
    bool 	is_pending() { return( modem->select( 1000 ) ); }
   
    // These are used to read the messages of pppd
    int          pppd_msgfd[2];		// two fd of the pipe
    WvFDStream  *pppd_log;		// to read messages of pppd
   
    // These are used to pipe the password to pppd
    int          pppd_passwdfd[2];	// two fd of the pipe
   
};
#endif // __DIALER_H
