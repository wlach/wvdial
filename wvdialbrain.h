/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
 *
 * Definition of the brains behind the WvDialer class.
 *
 */

#ifndef __WVDIALBRAIN_H
#define __WVDIALBRAIN_H

#include <termios.h>

#include "strutils.h"
#include "wvlog.h"
#include "wvpipe.h"
#include "wvstreamclone.h"

class WvDialer;

enum BrainTokenType
/*****************/
{
    TOK_WORD = 0,
    TOK_NUMBER,
    TOK_PUNCT
};

struct BrainToken
/***************/
{
    BrainTokenType	type;
    char *		tok_str;
    char		tok_char;
    BrainToken *	next;
};

class WvDialBrain
/***************/
{
public:
    WvDialBrain( WvDialer * a_dialer );
    ~WvDialBrain();

    void		reset();

    const char *	check_prompt( const char * buffer );
    const char *	guess_menu( char * buf );
    int                 saw_first_compuserve_prompt;

private:
    WvDialer *		dialer;
    
    int			sent_login;
    int			prompt_tries;
    WvString		prompt_response;

    // These functions are called from check_prompt()....
    bool 		is_prompt( const char * c, 
				   const char * promptstring = NULL,
				   bool	        dots_wild    = false );
    bool		is_login_prompt( const char * buf );
    bool		is_compuserve_prompt( const char * buf );
    bool		is_password_prompt( const char * buf );
    bool		is_welcome_msg( const char * buf );

    // Menu-string tokenizer....
    BrainToken *	tokenize( char * left, char * right );
    BrainToken * tokenize( char * str );
    void		token_list_done( BrainToken * token_list );

    // Called from guess_menu....
    void		guess_menu_guts( BrainToken * token_list );
    void		set_prompt_response( char * str );
};

inline BrainToken * WvDialBrain::tokenize( char * str )
/*****************************************************/
{
    if( str == NULL ) 
    	return( NULL );

    return( tokenize( str, str+strlen( str ) - 1 ) );
}

#endif // __WVDIALBRAIN_H
