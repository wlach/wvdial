/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997, 1998 Worldvisions Computer Technology, Inc.
 *
 * Re-write of wvpapsecrets.cc.  This one supports CHAP as well, and is also
 * much safer.
 */

#include "wvpapchap.h"

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
    nuke_contents();
    load_file( PAP_SECRETS );
    do_secret( username, password, remote );
    if( write_file( PAP_SECRETS ) == false )
	pap_success = false;

    // CHAP secrets:
    nuke_contents();
    load_file( CHAP_SECRETS );
    do_secret( username, password, remote );
    if( write_file( CHAP_SECRETS ) == false )
	chap_success = false;
}


///////////////////////////////////////////////////////////
// WvPapChap private functions
///////////////////////////////////////////////////////////

void WvPapChap::nuke_contents()
/*****************************/
// Wipe out the "contents" list.
{
    WvStringList::Iter	iter( contents );

    iter.rewind();
    iter.next();
    while( iter.cur() )
    	iter.unlink();		// also deletes the WvString
}

bool WvPapChap::load_file( char * filename )
/******************************************/
// Loads filename into the "contents" string list, one line per entry.
{
    char * 	from_file;
    WvString *	tmp;

    WvFile file( filename, O_RDONLY );
    if( file.isok() == false )
    	return( false );

    from_file = file.getline( 0 );
    while( from_file ) {
    	tmp = new WvString( from_file );
    	tmp->unique();
    	contents.append( tmp, true );
    	from_file = file.getline( 0 );
    }
    file.close();

    return( true );
}

bool WvPapChap::write_file( char * filename )
/*******************************************/
// Writes the "contents" list to the file, one entry per line.
{
    WvFile file( filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR );
    if( file.isok() == false )
    	return( false );

    WvStringList::Iter	iter( contents );
    for( iter.rewind(); iter.next(); )
    	file.print( "%s\n", iter );

    file.close();
    return( true );
}

void WvPapChap::do_secret( const char * username, const char * password, 
			   const char * remote )
/**************************************************************************/
// Goes through the "contents" list, looking for lines that have the same
// username.  If they do, and the remote value is either "*" or remote,
// the secret is removed.  Otherwise, it is left in place.  At the end of the
// list, the secret "username remote password" is added.
// remote defaults to "wvdial".
{
    WvStringList::Iter	iter( contents );

    if( !username || !password )
    	return;

    iter.rewind();
    iter.next();
    while( iter.cur() ) {
    	// Is this line a comment?
    	if( iter()[0] == '#' ) {
    	    iter.next();
    	    continue;
    	}

    	// Is the line blank?
    	const char * p = iter();
    	do 
    	    p++;
    	while( *p != '\0' && isspace( *p ) );
    	p--;
    	if( *p == '\0' ) {
    	    iter.next();
    	    continue;
    	}

	// p points at the first non-whitespace char.
	const char * q = p;
	do
	    q++;
	while( *q != '\0' && !isspace( *q ) );
	q--;
	if( *q == '\0' ) {
	    // illegal line, so get rid of it.
	    iter.unlink();
	    continue;
	}
	if( strncmp( username, p, q-p ) != 0 ) {
	    // different username, so let it stay.
	    iter.next();
	    continue;
	}

	p=q;
	do
	    p++;
	while( *p != '\0' && isspace( *p ) );
	// p now points to the beginning of the "remote" section.
	if( strncmp( p, remote, strlen( remote ) ) == 0 || *p == '*' ) {
	    // conflicting secret, so get rid of it.
	    iter.unlink();
	    continue;
	}

	// This secret line should be fine.
	iter.next();
	continue;
    }

    contents.append( new WvString( "%s\t%s\t%s", username, remote, password ), 
    		     true );
}
