// copyright: (C) 2000 by SuSE GmbH
// author: arvin@suse.de
// WvStreamsified by Patrick Patterson (ppatters@nit.ca)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <netdb.h>
#include <net/route.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "wvdialmon.h"



WvDialMon::WvDialMon()
    : log( "WvDial", WvLog::Debug ),
      err( log.split( WvLog::Error ) ),
      buffer("")
{
    // executename = WvString( "" );
    connectmsg = WvString( "Connected... Press Ctrl-C to disconnect\n" );
    
    // usleep_time = 1000;
    do_check_dfr = 0;
    do_check_dns = 0;
    
    route_fd = (FILE *) 0;
    
    buffer.setsize(100);
    
    regcomp( &rx_status, "status *= *", REG_EXTENDED );
    regcomp( &rx_quote, "\\\"[^\\\"]+\\\"", REG_EXTENDED );
    regcomp( &rx_namesrv, "nameserver *[0-9]+.[0-9]+.[0-9]+.[0-9]+", REG_EXTENDED );
    
    reset();
}



WvDialMon::~WvDialMon()
{
    regfree( &rx_status );
    regfree( &rx_quote );
    regfree( &rx_namesrv );
}


void WvDialMon::setdnstests (WvString dnstest1, WvString dnstest2)
{
    WvDialMon::dnstest1 = dnstest1;
    WvDialMon::dnstest2 = dnstest2;
}


void WvDialMon::reset()
{
    _auth_failed = 0;
}

const int WvDialMon::auth_failed()
{
    return _auth_failed;
}


int WvDialMon::get_quotedtext(char *dest, const char *line)
{
    regmatch_t rm[1];
    
    if( regexec( &rx_quote, line, 1, &rm[0], 0 ) == REG_NOMATCH ) 
    {
	err("***** no quoted text found in `%s' *****\n", line );
	return 0;
    }
    
    int s = rm[0].rm_so, e = rm[0].rm_eo;
    strncpy (dest, &line[s], e-s);
    dest[e-s] = '\0';
    
    return 1;
}

char *WvDialMon::analyse_line(const char *line)
{
    regmatch_t rm[1];
    
    if (line == NULL )
        return NULL;

    // PAP stuff
    // 
    if (strstr(line, "AuthReq") != NULL)
	log("Authentication (PAP) started\n");
    
    if (strstr(line, "AuthAck") != NULL)
	log("Authentication (PAP) successful\n");
    
    if (strstr(line, "AuthNak") != NULL) {
	log("Authentication (PAP) failed");
	
	char buf[strlen(line)];
	if( get_quotedtext( buf, line ) )
	    log(" (Message: %s )\n", buf);
	
	_auth_failed = 1;
    }
    
    
    // CHAP stuff
    // 
    if (strstr(line, "CHAP Challenge") != NULL)
	log("Authentication (CHAP) started\n");
    
    if (strstr(line, "CHAP Success") != NULL)
	log("Authentication (CHAP) successful\n");
    
    if (strstr(line, "CHAP Failure") != NULL) {
	log("Authentication (CHAP) failed");
	
	char buf[strlen(line)];
	if( get_quotedtext( buf, line ) )
	    log(" (Message: %s )\n", buf);
	
	_auth_failed = 1;
    }
    
    
    // IP stuff
    // 
    if (!strncmp(line, "local  IP address", 17)     ||
	!strncmp(line, "remote IP address", 17)     ||
	!strncmp(line, "primary   DNS address", 20) ||
	!strncmp(line, "secondary DNS address", 20)   )
    {
	log("%s\n",line);
    }
    
    // Script stuff
    // 
    if (strncmp(line, "Script", 6) == 0) 
    {
	if (strstr(line, "/etc/ppp/ip-up") != NULL && strstr(line, "finished") != NULL) 
	{    
	    if( regexec( &rx_status, line, 1, &rm[0], 0 ) == 0 ) 
	    {
		const char *p = &line[ rm[0].rm_eo ];
		
		// err("***** status is `%s' *****\n", p2 );

		if (strcmp( p, "0x0") == 0) 
		{
		    
		    log("Script /etc/ppp/ip-up run successful\n");
		    
		    if( do_check_dfr ) 
		    {
			if( check_dfr() )
			    log( "Default route Ok.\n" );
			else
			    log( "Default route failure.\n" );
		    }
		    
		    if( do_check_dns ) 
		    {
			if( check_dns() )
			    log( "Nameserver (DNS) Ok.\n" );
			else
			    log( "Nameserver (DNS) failure, the connection may not work.\n" );
		    }
		    
		    log( "%s\n", connectmsg );
		    
		    //	  execute whatever the user wants to
		    //
		    //  	  if( executename.len() > 0 ) {
		    //
		    //  	    fflush(stdout);
		    //  	    fflush(stderr);
		    //
		    //  	    pid_t pid = fork();
		    //
		    //  	    if( pid == (pid_t) 0 ) { // we are the child
		    //
		    //  	      int devnullr = open("/dev/null",O_RDONLY,0);
		    //  	      dup2(devnullr, fileno(stdin));
		    //  	      close(devnullr);
		    //
		    //  	      int devnullw = open("/dev/null",O_WRONLY,0);
		    //  	      dup2(devnullw, fileno(stdout));
		    //  	      dup2(devnullw, fileno(stderr));
		    //  	      close(devnullw);
		    //  	      fflush(stdout);
		    //  	      fflush(stderr);
		    //
		    //  	      for( int tty = 3; tty < 256; tty++ )
		    //  		close(tty);
		    //
		    //  	      usleep( usleep_time );
		    //
		    //  	      // executename = Netscape -remote "reload()", macht probleme
		    //  	      // wenn mehrere netscapes auf dem xserver laufen (dann
		    //  	      // empfängt zufällig einer die message)
		    //
		    //  	      const char *new_argv[4];
		    //  	      new_argv[0] = "sh";
		    //  	      new_argv[1] = "-c";
		    //  	      new_argv[2] = executename;
		    //  	      new_argv[3] = NULL;
		    //
		    //  	      execv( "/bin/sh", (char *const *) new_argv );
		    //
		    //  	      fprintf( stderr, "exec failed: %s\n", strerror(errno) );
		    //  	    }
		    //
		    //	    if( pid < (pid_t) 0 ) // the fork failed
		    //	      fprintf( stderr, "error: can't fork child process\n" );
		    //	    else
		    //	      output( "Started `", executename, "' successfully\n" );
		    //
		    //	  }
		    
		} 
		else
		{
		    log("Script /etc/ppp/ip-up failed (return value: %s )\n", p);
		}

	    } 
	    else 
	    {
		// fprintf( stderr, "***** no status found *****\n" );
	    }
	    
	}
	
	if (strstr(line, "/etc/ppp/ip-down") != NULL && strstr(line, "started") != NULL)
	    log("Script /etc/ppp/ip-down started\n");
    }
    
    
    // TermReq stuff
    // 
    if (strstr(line, "LCP TermReq") != NULL) 
    {
	log("Terminate Request");

	char buf[strlen(line)];
	if( get_quotedtext( buf, line ) )
	    log(" (Message: %s )\n", buf);
    }
    
    
    // connect time stuff
    // 
    if (strncmp(line, "Connect time", 12) == 0)
	log("%s\n",line);
    
    
    // interface stuff
    // 
    if (strncmp(line, "Using interface", 15) == 0)
	log("%s\n", line);
    
    // terminate stuff
    // 
    if( strncmp(line,"Terminating", 11 ) == 0 )
	log("%s\n",line);
    
    
    return buffer.edit();
}



/********* taken from pppd ************/

#define KVERSION(j,n,p)	((j)*1000000 + (n)*1000 + (p))

#define SET_SA_FAMILY(addr, family)			\
	memset ((char *) &(addr), '\0', sizeof(addr));	\
    addr.sa_family = (family);

#define SIN_ADDR(x)	(((struct sockaddr_in *) (&(x)))->sin_addr.s_addr)

#define ROUTE_MAX_COLS	12

/********************************************************************
 *
 * close_route_table - close the interface to the route table
 */

void WvDialMon::close_route_table ()
{
    if (route_fd != (FILE *) 0) 
    {
	fclose (route_fd);
	route_fd = (FILE *) 0;
    }
}

/********************************************************************
 *
 * open_route_table - open the interface to the route table
 */

static char route_delims[] = " \t\n";

int WvDialMon::open_route_table ()
{
    char path[] = "/proc/net/route";
    
    close_route_table();
    
    route_fd = fopen (path, "r");
    if (route_fd == NULL) {
	log( "can't read %s\n", path );
	return 0;
    }
    
    route_dev_col = 0;		/* default to usual columns */
    route_dest_col = 1;
    route_gw_col = 2;
    route_flags_col = 3;
    route_mask_col = 7;
    route_num_cols = 8;
    
    /* parse header line */
    if (fgets(route_buffer, sizeof(route_buffer), route_fd) != 0) 
    {
	char *p = route_buffer, *q;
	int col;
	for (col = 0; col < ROUTE_MAX_COLS; ++col) 
	{
	    int used = 1;
	    if ((q = strtok(p, route_delims)) == 0)
		break;
	    if (strcasecmp(q, "iface") == 0)
		route_dev_col = col;
	    else if (strcasecmp(q, "destination") == 0)
		route_dest_col = col;
	    else if (strcasecmp(q, "gateway") == 0)
		route_gw_col = col;
	    else if (strcasecmp(q, "flags") == 0)
		route_flags_col = col;
	    else if (strcasecmp(q, "mask") == 0)
		route_mask_col = col;
	    else
		used = 0;
	    if (used && col >= route_num_cols)
		route_num_cols = col + 1;
	    p = NULL;
	}
    }
    
    return 1;
}

/********************************************************************
 *
 * read_route_table - read the next entry from the route table
 */

int WvDialMon::read_route_table(struct rtentry *rt)
{
    char *cols[ROUTE_MAX_COLS], *p;
    int col;
    
    memset (rt, '\0', sizeof (struct rtentry));
    
    if (fgets (route_buffer, sizeof (route_buffer), route_fd) == (char *) 0)
	return 0;
    
    p = route_buffer;
    for (col = 0; col < route_num_cols; ++col) {
	cols[col] = strtok(p, route_delims);
	if (cols[col] == NULL)
	    return 0;		/* didn't get enough columns */
	p = NULL;
    }
    
    SET_SA_FAMILY (rt->rt_dst,     AF_INET);
    SET_SA_FAMILY (rt->rt_gateway, AF_INET);
    
    SIN_ADDR(rt->rt_dst) = strtoul(cols[route_dest_col], NULL, 16);
    SIN_ADDR(rt->rt_gateway) = strtoul(cols[route_gw_col], NULL, 16);
    SIN_ADDR(rt->rt_genmask) = strtoul(cols[route_mask_col], NULL, 16);
    
    rt->rt_flags = (short) strtoul(cols[route_flags_col], NULL, 16);
    rt->rt_dev   = cols[route_dev_col];
    
    return 1;
}

/********************************************************************
 *
 * defaultroute_exists - determine if there is a default route
 */

int WvDialMon::defaultroute_exists (struct rtentry *rt)
{
    int result = 0;
    
    if (!open_route_table())
	return 0;
    
    while (read_route_table(rt) != 0) 
    {
	if ((rt->rt_flags & RTF_UP) == 0)
	    continue;
	
	if (kernel_version > KVERSION(2,1,0) && SIN_ADDR(rt->rt_genmask) != 0)
	    continue;
	if (SIN_ADDR(rt->rt_dst) == 0L) 
	{
	    result = 1;
	    break;
	}
    }
    
    close_route_table();
    return result;
}

int WvDialMon::check_dfr()
{
    struct rtentry def_rt;
    
    if( !defaultroute_exists( &def_rt ) )
	return 0;
    
    // FIXME: check if the gateway is set correct
    // 
    return 1;
}

int WvDialMon::check_dns_name( const char *name )
{
    struct hostent *hp;
    hp = gethostbyname( name );
    if( hp == NULL ) 
    {
	log( "warning, can't find address for `%s`\n", name );
	return 0;
    }
    
    // err("***** official name for `%s' is `%s' *****\n", name, hp->h_name );
    // 
    return 1;
}

int WvDialMon::check_dns()
{
  char name[] = "/etc/resolv.conf";
    
    FILE *fin = fopen( name, "r" );
    if( fin == NULL ) {
	log( "warning, can't read `%s`\n", name );
	return 0;
    }
    
    const int size = 100;
    char line[size];
    
    regmatch_t rm[1];
    int found_namesrv = 0;
    
    while( fgets( line, size, fin ) != NULL ) 
    {
	if( line[0] == '\n' || line[0] == '#' )
	    continue;
	
	if( line[ strlen(line)-1 ] == '\n' )
	    line[ strlen(line)-1 ] = '\0';
	
	if( regexec( &rx_namesrv, line, 1, &rm[0], 0 ) == 0 ) 
	{
	    found_namesrv = 1;
	    break;
	}
	
    }
    
    fclose( fin );
    
    if( !found_namesrv ) 
    {
	log( "warning, no nameserver found `%s`\n",name);
	return 0;
    }
    
    if (!check_dns_name ((const char* ) dnstest1) || !check_dns_name ((const char* ) dnstest2)) 
    {
	log( "warning, address lookup does not work\n" );
	return 0;
    }
    
    return 1;
}

