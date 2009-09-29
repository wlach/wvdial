/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
 *
 * Re-write of wvpapsecrets.cc.  This one supports CHAP as well, and is also
 * much safer.
 */

#include "wvpapchap.h"
#include "wvfile.h"
#include "strutils.h"
#include <assert.h>
#include <ctype.h>
#include <sys/stat.h>


///////////////////////////////////////////////////////////
// WvPapChap public functions
///////////////////////////////////////////////////////////

void WvPapChap::put_secret( WvString username, WvString password,
			    WvString remote )
/*******************************************/
{
    assert( remote[0] );

    // PAP secrets:
    contents.zap();
    load_file( PAP_SECRETS );
    do_secret( username, password, remote );
    if( write_file( PAP_SECRETS ) == false )
	pap_success = false;

    // CHAP secrets:
    contents.zap();
    load_file( CHAP_SECRETS );
    do_secret( username, password, remote );
    if( write_file( CHAP_SECRETS ) == false )
	chap_success = false;
}


///////////////////////////////////////////////////////////
// WvPapChap private functions
///////////////////////////////////////////////////////////

bool WvPapChap::load_file( const char * filename )
/******************************************/
// Loads filename into the "contents" string list, one line per entry.
{
    char * 	from_file;
    WvString *	tmp;

    WvFile file( filename, O_RDONLY );
    if( file.isok() == false )
    	return( false );

    from_file = file.getline();
    while( from_file ) {
    	tmp = new WvString( from_file );
    	contents.append( tmp, true );
    	from_file = file.getline();
    }
    file.close();

    return( true );
}

bool WvPapChap::write_file( const char * filename )
/*******************************************/
// Writes the "contents" list to the file, one entry per line.
{
    WvFile file( filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR );
    if( file.isok() == false )
    	return( false );

    WvStringList::Iter	iter( contents );
    for( iter.rewind(); iter.next(); )
    	file.print( "%s\n", *iter );

    file.close();
    return( true );
}

void WvPapChap::do_secret( const char * _username, const char * _password, 
			   const char * _remote )
/***********************************************/
// Goes through the "contents" list, looking for lines that have the same
// username.  If they do, and the remote value is either "*" or remote,
// the secret is removed.  Otherwise, it is left in place.  At the end of the
// list, the secret "username remote password" is added.
// remote defaults to "wvdial".
{
    WvStringList::Iter	iter( contents );
    WvString username;
    WvString password;
    WvString remote;

    if( !_username || !_password )
    	return;
    
    // we need to backslash-escape all punctuation, so that pppd reads it
    // correctly.
    username = backslash_escape( _username );
    password = backslash_escape( _password );
    remote   = _remote;

    if( !remote )
        remote = "*";

    for( iter.rewind(); iter.next(); ) {
    	// Is this line a comment?
    	if( iter()[0] == '#' )
    	    continue;

    	// Is the line blank?
    	const char * p = iter();
    	do 
    	    p++;
    	while( *p != '\0' && isspace( *p ) );
    	p--;
    	if( *p == '\0' )
    	    continue;

	// p points at the first non-whitespace char.
	const char * q = p;
	do
	    q++;
	while( *q != '\0' && !isspace( *q ) );
	q--;
	if( *q == '\0' ) {
	    // illegal line, so get rid of it.
	    iter.unlink();
            iter.rewind();
	    continue;
	}
	if( strncmp( username, p, q-p ) != 0 )
	    // different username, so let it stay.
	    continue;

	p=q;
	do
	    p++;
	while( *p != '\0' && isspace( *p ) );
	// p now points to the beginning of the "remote" section.
	if( strncmp( p, remote, strlen( remote ) ) == 0 || *p == '*' ) {
	    // conflicting secret, so get rid of it.
	    iter.unlink();
            iter.rewind();
	    continue;
	}

	// This secret line should be fine.
    }

    contents.append( new WvString( "%s\t%s\t%s", username, remote, password ), 
    		     true );
}
