/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
 *
 * Re-write of wvpapsecrets.h.  This one supports CHAP as well, and is also
 * much safer.
 */

#ifndef __WVPAPCHAP_H
#define __WVPAPCHAP_H

#include "wvconfemu.h"
#include "wvlinklist.h"
#include "wvstring.h"

#define PAP_SECRETS	"/etc/ppp/pap-secrets"
#define CHAP_SECRETS	"/etc/ppp/chap-secrets"
#define REMOTE_SECRET	"wvdial"

class WvPapChap
/*************/
{
public:
    WvPapChap()
    	: pap_success( true ), chap_success( true ) {}
    ~WvPapChap() {}

    void put_secret( WvString _username, WvString _password, WvString _remote );
    bool isok_pap() const
    	{ return( pap_success ); }
    bool isok_chap() const
    	{ return( chap_success ); }

private:
    WvStringList contents;
    bool	 pap_success;
    bool	 chap_success;

    bool load_file( const char * filename );
    bool write_file( const char * filename );
    void do_secret( const char * username, const char * password,
		    const char * remote );
};

#endif // __WVPAPCHAP_H
