/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
 *
 * Little test program for WvPapChap.
 */

#include "wvpapchap.h"

int main( int argc, char * argv[] )
/*********************************/
{
    if( argc != 4 ) {
    	printf( "Usage:	papchaptest username remote password\n" );
    	return( -1 );
    }

    WvPapChap	papchap;

    papchap.put_secret( argv[1], argv[3], argv[2] );

    if( papchap.isok_pap() )
    	printf( "PAP successful.\n" );
    else
    	printf( "PAP failed.\n" );

    if( papchap.isok_chap() )
    	printf( "CHAP successful.\n" );
    else
    	printf( "CHAP failed.\n" );

    return( 0 );
}
