/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
 *
 * The brains behind the WvDialer class.  This handles prompt-detection,
 * menu-guessing, and related tasks.
 *
 */

#include "wvdialbrain.h"
#include "wvdialer.h"

#include <ctype.h>
#include <string.h>
#include <time.h>

static char      rbrackets[] = ".-:'\">)]}"; 
static char      lbrackets[] = ".-:'\"<([{";
static char       brackets[] = ".-:'\"<>()[]{}";
static char menu_bad_words[] = "press push hit enter type key";


//**************************************************
//       WvDialBrain Public Functions
//**************************************************

WvDialBrain::WvDialBrain( WvDialer * a_dialer )
/*********************************************/
: dialer( a_dialer )
{
  saw_first_compuserve_prompt = 0;
  reset();
}

WvDialBrain::~WvDialBrain()
/*************************/
{
}

void WvDialBrain::reset()
/***********************/
{
    sent_login	    =  0;
    prompt_tries    =  0;
    prompt_response = "";
}

const char * WvDialBrain::guess_menu( char * buf )
/************************************************/
// Searches buf for signs of intelligence, and tries to guess how to
// start PPP on the remote side.  Good for terminal servers with menus,
// prompts, and (hopefully) whatever else.
// Assumes buf is lowercase already, and has all its nulls turned into
// whitespace.
{
    char *	 cptr = NULL; 
    char *	 line; 
    char *	 nextline;
    BrainToken * tok;

    while( strstr( buf, "ppp" ) != NULL ) {
    	cptr = strstr( buf, "ppp" );
	// pick out the beginning of the line containing "ppp"
	for( line=cptr; line >= buf && !isnewline( *line ); line-- )
	    ;
	line++;		// too far!
	
	// find the beginning of the next line in the buffer, or the
	// end of the buffer... so we know where to stop our searches.
	for( nextline = line; *nextline && !isnewline( *nextline ); nextline++ )
	    ;
    
	buf = nextline; // now 'continue' will check the next line

	// Now tokenize the line and perform an IntelliSearch (tm)
	tok = tokenize( line, nextline );
	if( tok ) {
	    guess_menu_guts( tok );	// may call set_prompt_response()
	    token_list_done( tok );
	}
    }
    if( cptr )
	return( cptr + 4 );	// return pointer directly AFTER "ppp".
    else
    	return( NULL );
}

const char * WvDialBrain::check_prompt( const char * buffer )
/***********************************************************/
{
    WvString tprompt;
    
    // If we've been here too many times, or too long ago, just give up and
    // start pppd.
    if( prompt_tries >= 5 || time( NULL ) - dialer->last_rx >= 10 ) {
    	dialer->log( WvLog::Notice, "Don't know what to do!  "
    		"Starting pppd and hoping for the best.\n" );
    	dialer->start_ppp();

    } else if( dialer->options.compuserve && is_compuserve_prompt( buffer )) {
    	// We have a login prompt, so send a suitable response.
    	const char * send_this = "CIS";
    	dialer->log( "Looks like a Compuserve host name prompt.\n"
    		     "Sending: %s\n", send_this );
	saw_first_compuserve_prompt++;
	saw_first_compuserve_prompt++;
    	dialer->reset_offset();
    	prompt_tries++;
	return( send_this );

    } else if( dialer->options.compuserve 
	       && !saw_first_compuserve_prompt && is_login_prompt ( buffer )) {
    	// We have a login prompt, so send a suitable response.
    	const char * send_this = "cisv1";
    	dialer->log( "Looks like a Compuserve New login prompt.\n"
    		     "Sending: %s\n", send_this );
    	dialer->reset_offset();
    	prompt_tries++;
	saw_first_compuserve_prompt++;
	return( send_this );

    } else if( is_login_prompt( buffer ) ) {
    	// We have a login prompt, so send a suitable response.
    	WvString login = dialer->options.login;
	if (dialer->options.compuserve &&
	    strstr (dialer->options.login, "/noint") == 0) {
	   login = WvString("%s%s", login, "/noint/go:pppconnect");
	}
    	dialer->log( "Looks like a login prompt.\n"
    		     "Sending: %s\n", login );
    	dialer->reset_offset();
    	prompt_tries++;
	return( login );

    } else if( is_password_prompt( buffer ) ) {
        const char *passwd = 0;
        if (dialer->options.compuserve && saw_first_compuserve_prompt == 1) {
	  passwd = "classic";
	  dialer->log( "Looks like a Compuserve classic password prompt.\nSending: classic\n" );
	  saw_first_compuserve_prompt++;
        } else {
	  // We have a password prompt, so send a suitable resonse.
	  dialer->log( "Looks like a password prompt.\nSending: (password)\n" );
	}
    	dialer->reset_offset();
    	prompt_tries++;
    	sent_login = 1;	// yes, we've sent a password:
    			// assume we've sent username too.
	if (!passwd) passwd = dialer->options.password;
    	return( passwd );

    } else if( is_welcome_msg( buffer ) ) {
    	dialer->log( "Looks like a welcome message.\n" );
    	dialer->start_ppp();

    } else if( is_prompt( buffer ) ) {
    	// We have some other prompt.
    	if( dialer->is_pending() ) {
    	    return( NULL );	// figure it out next time
    	}

    	if( !prompt_response[0] )
    	    prompt_response = dialer->options.default_reply;	// wild guess

    	dialer->log( "Hmm... a prompt.  Sending \"%s\".\n",
    		     prompt_response );
    	dialer->reset_offset();
    	prompt_tries++;

	// only use our prompt guess the first time.
	// if it fails, go back to the default reply.
	tprompt = prompt_response;
	prompt_response = "";
	
    	return( tprompt );

    } else {
    	// not a prompt at all!
    	if( dialer->is_pending() )
    	    return( NULL );	// figure it out next time
    }

    // If we get here, then we aren't at a prompt.
    return( NULL );
}




//**************************************************
//       WvDialBrain Private Functions
//**************************************************

bool WvDialBrain::is_prompt( const char * c, 
			     const char * promptstring,
			     bool	  dots_wild )
/***************************************************/
// Searches the buffer 'c' for a prompt.  If no promptstring is given, we
// return true for ANY prompt.  If a prompt string is given, then we are
// looking for a SPECIFIC prompt that contains that string.
{
    const char *	cptr;
    static const char *	prompt_punct	= ")>}]:.|-?$%=\x11";
    
    // if no promptstring was given, the search is simple:  it is a 
    // prompt if the last line ends in punctuation and no newline.
    if( !promptstring ) {
	for( cptr = c + strlen( c ) - 1; cptr >= c; cptr-- ) {
	    if( isnewline( *cptr ) ) {
		if ( !prompt_response[0] )
		    return( false ); // last line was empty: not a prompt
		else
		    continue; // we have a guess, so use it anyway, IF the
                              // last non-blank line was a prompt.
	    }
	    if( strchr( prompt_punct, *cptr ) )
		return( true );  // first non-whitespace was punctuation! good.
	    if ( !isspace( *cptr ) )
		return( false ); // not punctuation or whitespace at end
	}
	return( false );
    }
    
    // seek backwards from the end of the buffer to the beginning of
    // the last line, or the beginning of the buffer, whichever comes first.
    // Then skip leading whitespace so that when we are done, (c) points at
    // the first non-white character on the line.
    for( cptr = c + strlen( c ) - 1; cptr >= c; cptr-- ) {
	if( isnewline( *cptr ) )
	    break;
    }
    c = cptr > c ? cptr : c;
    while( isspace( *c ) )
	c++;
    
    // find the promptstring in the buffer.
    if( dots_wild == false ) {
    	// a bit of an obscure case, but: find the _last_ occurrence on the
    	// prompt line, not the first.
	c = strstr( c, promptstring );
	if( !c ) 
	    return( false );
	else while( strstr( c+1, promptstring ) )
	    c = strstr( c+1, promptstring );
    } else {	// dots are wild, like in regular expressions
    	const char *	p 	   = c;
    	unsigned int	char_count = 0;
	while( *p != '\0' && char_count != strlen( promptstring ) ) {
	    char 	tmp = promptstring[ char_count ];
	    if( tmp == '.' || *p == tmp ) {
	    	if( char_count == 0 ) {
	    	    // If we match the beginning of the promptstring,
	    	    // set c to point at it.
	    	    c = p;
	    	}
	    	char_count++;
	    } else {
	    	// Start over, since a letter did not match and was not a dot.
	    	char_count = 0;
	    }
	    p++;
	}

	// If we hit the end of the promptstring, and char_count does not
	// equal its length, then it was not there.
	if( char_count != strlen( promptstring ) )
	    return( false );
    }

    // now make sure everything after "promptstring" is whitespace
    // and there is at least one punctuation mark.
    bool foundpunct = false;
    for( c += strlen( promptstring ); *c; c++ ) {
        if( strchr( prompt_punct, *c ) )
	    foundpunct = true;
	else if( !isspace( *c ) )
	    return false; // non-whitespace or punctuation: not a prompt
    }
    return( foundpunct ); // found a prompt if the string was followed by punct
}

bool WvDialBrain::is_login_prompt( const char * buf )
/***************************************************/
{
    return( is_prompt( buf, "login" ) ||
    	    is_prompt( buf, "name" )  ||
    	    is_prompt( buf, "user" ) ||
    	    is_prompt( buf, "id" ) ||
    	    is_prompt( buf, "userid" ) ||
    	    is_prompt( buf, "user.id", true ) ||
    	    is_prompt( buf, "signon" ) ||
    	    is_prompt( buf, "sign.on", true ) ||
    	    is_prompt( buf, "usuario", false ) ||
    	    ( dialer->options.login_prompt[0] &&
    	      is_prompt( buf, dialer->options.login_prompt ) ) );
}

bool WvDialBrain::is_compuserve_prompt( const char * buf )
/***************************************************/
{
    return( is_prompt( buf, "host name" ));
}

bool WvDialBrain::is_password_prompt( const char * buf )
/******************************************************/
{
    return( is_prompt( buf, "password" ) ||
      ( dialer->options.pass_prompt[0] &&
        is_prompt( buf, dialer->options.pass_prompt ) ) );
}

bool WvDialBrain::is_welcome_msg( const char * buf )
/**************************************************/
// Thanks to dsb for this one, 3/10/98.
{
    return( sent_login && ( strstr( buf, "mtu" ) ||
    			    strstr( buf, "ip address is" ) ) );
}

BrainToken * WvDialBrain::tokenize( char * left, char * right )
/*************************************************************/
{
    BrainToken *   token_list = NULL;
    BrainToken *   new_token  = NULL;
    BrainToken *   prev_token = NULL;
    char * 	   p;

    if( left == NULL || right == NULL || right <= left )
    	return( NULL );

    p = left;
    while( p <= right ) {
	// If *p is a null or a new-line, we are done.
	if( *p == '\0' || isnewline( *p ) )
	    break;

	// Skip whitespace in the string.
	if( isspace( *p ) ) {
	    p++;
	    continue;
	}

	// If it's a letter, we've got the beginning of a word.
	if( isalpha( *p ) ) {
	    char * end = p+1;
	    new_token = new BrainToken;
	    new_token->type = TOK_WORD;
	    new_token->next = NULL;
	    if( token_list == NULL )
	    	token_list = new_token;
	    else
	    	prev_token->next = new_token;
	    while( end <= right && isalpha( *end ) )
	    	end++;
	    new_token->tok_str = new char[ end - p + 1 ];
	    strncpy( new_token->tok_str, p, end - p );
	    new_token->tok_str[ end-p ] = '\0';
	    p = end;	// skip to the end of the word for next time
	    prev_token = new_token;
	    continue;
	}

	// If it's a digit, we've got the beginning of a number.
	if( isdigit( *p ) ) {
	    char * end = p+1;
	    new_token = new BrainToken;
	    new_token->type = TOK_NUMBER;
	    new_token->next = NULL;
	    if( token_list == NULL )
	    	token_list = new_token;
	    else
	    	prev_token->next = new_token;
	    while( end <= right && isdigit( *end ) )
	    	end++;
	    new_token->tok_str = new char[ end - p + 1 ];
	    strncpy( new_token->tok_str, p, end - p );
	    new_token->tok_str[ end-p ] = '\0';
	    p = end;	// skip to the end of the number for next time
	    prev_token = new_token;
	    continue;
	}

	// If it's useful punctuation (brackets and such), grab it.
	if( strchr( brackets, *p ) ) {
	    new_token = new BrainToken;
	    new_token->type = TOK_PUNCT;
	    new_token->next = NULL;
	    if( token_list == NULL )
	    	token_list = new_token;
	    else
	    	prev_token->next = new_token;
	    new_token->tok_char = *p;
	    p++;
	    prev_token = new_token;
	    continue;
	 }

	 // If it's anything else, ignore it.
	 p++;
    }

    return( token_list );
}

void WvDialBrain::token_list_done( BrainToken * token_list )
/**********************************************************/
{
    BrainToken * next_token;

    while( token_list != NULL ) {
    	next_token = token_list->next;
    	if( token_list->type == TOK_WORD || token_list->type == TOK_NUMBER )
    	    delete token_list->tok_str;
    	delete token_list;
    	token_list = next_token;
    }
}

void WvDialBrain::guess_menu_guts( BrainToken * token_list )
/**********************************************************/
// There are some cases which may occur in a valid menu line.
// Number 1 is of the form "P for PPP"
// Number 2 is of the form "1 - start PPP"
// Number 3 is of the form "(1) start PPP"
// Number 4 has the form "1 blah blah PPP".  The first non-whitespace character
//     is a number, followed by more whitespace.  We have to be paranoid here
//     to avoid seeing phone numbers and such.
// We check for these cases in order.
//
// Okay, fine.  This function now uses goto for no good reason.  So shoot me.
// At least it doesn't randomly return from the middle of the function before
// trying all the cases...
{
    BrainToken *	lmarker = NULL;
    BrainToken *	rmarker = NULL;
    BrainToken *	tok;
    char *		prompt_resp = NULL;
    int			index;

    /////////////// FIRST CASE
    // This should be generalized later, but for now we'll look for "FOR PPP",
    // and then examine the thing in front of THAT.  If it's not punctuation,
    // we'll use it as a prompt response.  If it IS punctuation, but is NOT
    // a bracket, we'll take the thing before THAT even, and use it as a prompt
    // response.
    for( tok = token_list; tok != NULL; tok = tok->next ) {
        bool failed = false;
    	if( tok->type == TOK_PUNCT )
    	    continue;

    	// Only looking at words and numbers now.
    	BrainToken * tok2 = tok->next;
    	for( ; tok2 != NULL; tok2 = tok2->next ) {
    	    if( tok2->type != TOK_PUNCT )
    	    	break;
    	    // Only looking at punctuation after the word we're investigating.
    	    if( strchr( brackets, tok2->tok_char ) ) {
    	    	failed = true;
    	    	break;
    	    }
    	}
    	if( failed )
    	    continue;

    	if( !tok2 || !tok2->next )
    	    break;

    	// Now tok is the potential response, and tok2 is the next word or
    	// number, as long as there were no brackets in between.
    	// So now we can look for "for ppp".
    	if( !strcmp( tok2->tok_str, "for" ) &&
    	    !strcmp( tok2->next->tok_str, "ppp" ) )
    	{
    	    set_prompt_response( tok->tok_str );
    	    return;
    	}
    }

    /////////////// SECOND CASE
    // Find the first right-bracket on the line, and evaluate everything
    // before it.  Things that are allowed are numbers, and words other 
    // than "press" etc.
    for( tok = token_list; tok != NULL; tok = tok->next )
    	if( tok->type == TOK_PUNCT )
    	    if( strchr( rbrackets, tok->tok_char ) ) {
    	    	rmarker = tok;	// leftmost right-bracket on this line.
    	    	break;
    	    }
    if( rmarker == NULL )
	goto three;		// no right-bracket on this line.

    // Make sure "ppp" comes _AFTER_ the rmarker...  So that we don't respond
    // to "I like food (ppp is fun too)" or similar things.
    for( tok = rmarker->next; tok != NULL; tok = tok->next )
    	if( tok->type == TOK_WORD && strcmp( tok->tok_str, "ppp" ) == 0 )
    	    break;
    if( tok == NULL )	// We did not find "ppp" after the rmarker
    	goto three;

    for( tok = token_list; tok != rmarker; tok = tok->next ) {
    	// If we find punctuation in here, then Case Two is WRONG.
    	// Also, handles things like "Press 5" or "Type ppp" correctly.
    	// If there's more than one valid "thing", use the last one.
    	if( tok->type == TOK_PUNCT ) {
    	    prompt_resp = NULL;
    	    break;
    	}
    	if( tok->type == TOK_NUMBER )
    	    prompt_resp = tok->tok_str;
    	if( tok->type == TOK_WORD )
    	    if( strstr( menu_bad_words, tok->tok_str ) == NULL )
    	    	prompt_resp = tok->tok_str;
    }

    if( prompt_resp != NULL ) {		// Case Two was successful!
    	set_prompt_response( prompt_resp );
    	return;
    }

three:
    /////////////// THIRD CASE
    // Find the first (and recursively innermost) matching pair of brackets.
    // For example: "This ('a', by the way) is what you type to start ppp."
    //              will parse out simply the letter a.
    //
    // Start by finding the RIGHTmost right-bracket which immediately
    // follows the LEFTmost right-bracket (ie - no words in between).
    // In the above example, it is the first apostrophe.
    //
    // Nov 6/98: Ummmmm, does the above paragraph make sense?
    bool ready_to_break = false;
    rmarker 		= NULL;
    for( tok = token_list; tok != NULL; tok = tok->next ) {

    	if( tok->type == TOK_PUNCT ) {
    	    if( strchr( lbrackets, tok->tok_char ) ) {
    	    	lmarker = tok;
    	    	ready_to_break = true;
    	    }
    	} else {
    	    if( ready_to_break )
    	    	break;
    	}
    }
    if( lmarker == NULL )
	goto four;		// no left-bracket on this line.

    // Now find the matching bit of punctuation in the remainder.
    // Watch for useful words as we do it...
    index = strchr( lbrackets, lmarker->tok_char ) - lbrackets;
    for( tok = lmarker->next; tok != NULL; tok = tok->next ) {
    	if( tok->type == TOK_PUNCT ) {
    	    if( tok->tok_char == rbrackets[ index ] ) {
    	    	rmarker = tok;
    	    	break;
    	    }
    	} else if( tok->type == TOK_WORD ) {
    	    if( strstr( menu_bad_words, tok->tok_str ) == NULL )
    	    	prompt_resp = tok->tok_str;
    	} else // tok->type == TOK_NUMBER
    	    prompt_resp = tok->tok_str;
    }

    if( rmarker == NULL )
    	goto four;		// no corresponding right-bracket on this line.

    // Make sure "ppp" comes _AFTER_ the rmarker...  So that we don't respond
    // to "I like food (ppp is fun too)" or similar things.
    for( tok = rmarker->next; tok != NULL; tok = tok->next )
    	if( tok->type == TOK_WORD && strcmp( tok->tok_str, "ppp" ) == 0 )
    	    break;
    if( tok == NULL )	// We did not find "ppp" after the rmarker
	goto four;
    
    if( prompt_resp != NULL ) {		// Case Three was successful
	set_prompt_response( prompt_resp );
    	return;
    }
    
four:    
    /////////////// FOURTH CASE
    // This is a catch-all for punctuationless menus of the form:
    //       1   PPP
    //       2   Quit
    // Let's just assume the command is a single-digit number.  This should
    // avoid accidentally parsing phone numbers or IP addresses.
    lmarker = token_list;
    if ( lmarker->type == TOK_NUMBER
	 && lmarker->tok_str[0] && !lmarker->tok_str[1] )
    {
	for ( tok = token_list; tok != NULL; tok = tok->next )
	    if (tok->type == TOK_WORD && strcmp( tok->tok_str, "ppp" ) == 0 )
		break;
	
	if ( tok )
	{
	    set_prompt_response( lmarker->tok_str ); // Case Four worked!
	    return;
	}
    }
    
    
    // Apparently this was not a valid menu option.  Oh well.
    return;
}

void WvDialBrain::set_prompt_response( char * str )
/*************************************************/
{
    WvString	n;

    if( strcmp( str, prompt_response ) ) {
	n.setsize( strlen( str ) + 1 );
	strcpy( n.edit(), str );
	n.edit()[ strlen( str ) ] = '\0';

    	dialer->log( "Found a good menu option: \"%s\".\n", n );
    	prompt_response = n;
    }
}

