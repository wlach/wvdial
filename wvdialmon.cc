

// copyright: (C) 2000 by SuSE GmbH
// author: arvin@suse.de


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
{
    // executename = WvString( "" );
  connectmsg = WvString( "Connected... Press Ctrl-C to disconnect\n" );

  // usleep_time = 1000;
  do_check_dfr = 0;
  do_check_dns = 0;

  route_fd = (FILE *) 0;

  size = 100;
  buffer = (char*) malloc( size );
  if( !buffer ) {
    fprintf( stderr, "out of memory\n" );
    exit( EXIT_FAILURE );
  }

  regcomp( &rx_status, "status *= *", REG_EXTENDED );
  regcomp( &rx_quote, "\\\"[^\\\"]+\\\"", REG_EXTENDED );
  regcomp( &rx_namesrv, "nameserver *[0-9]+.[0-9]+.[0-9]+.[0-9]+", REG_EXTENDED );

  reset();
}



WvDialMon::~WvDialMon()
{
  if( buffer ) {
    free( buffer );
    buffer = NULL;
  }

  regfree( &rx_status );
  regfree( &rx_quote );
  regfree( &rx_namesrv );
}


void
WvDialMon::setdnstests (WvString dnstest1, WvString dnstest2)
{
    WvDialMon::dnstest1 = dnstest1;
    WvDialMon::dnstest2 = dnstest2;
}


void
WvDialMon::reset()
{
  _auth_failed = 0;
}


int
WvDialMon::auth_failed() const
{
  return _auth_failed;
}


// FIXME: use vsnprintf
void
WvDialMon::output(const char *buf1)
{
  int new_size = strlen( buffer ) + strlen( buf1 ) + 1;

  if( new_size > size ) {
    buffer = (char*) realloc( buffer, new_size );
    size = new_size;
    if( !buffer ) {
      fprintf( stderr, "out of memory\n" );
      exit( EXIT_FAILURE );
    }
  }

  strcat( buffer, buf1 );
}



void
WvDialMon::output(const char *buf1, const char *buf2)
{
  output( buf1 );
  output( buf2 );
}

void
WvDialMon::output(const char *buf1, const char *buf2, const char *buf3)
{
  output( buf1 );
  output( buf2 );
  output( buf3 );
}



int
WvDialMon::get_quotedtext(char *dest, const char *line)
{
  regmatch_t rm[1];

  if( regexec( &rx_quote, line, 1, &rm[0], 0 ) == REG_NOMATCH ) {
    // fprintf( stderr, "***** no quoted text found in `%s' *****\n", line );
    return 0;
  }

  int s = rm[0].rm_so, e = rm[0].rm_eo;
  strncpy (dest, &line[s], e-s);
  dest[e-s] = '\0';

  return 1;
}



char*
WvDialMon::analyse_line(const char *line)
{
  regmatch_t rm[1];

  buffer[0] = '\0';


#if 0
  char tmp[ strlen( line ) + 10 ];
  sprintf( tmp, "* %s *\n", line );
  output( tmp );
#endif


  // PAP stuff

  if (strstr(line, "AuthReq") != NULL)
    output ("Authentication (PAP) started\n");

  if (strstr(line, "AuthAck") != NULL)
    output ("Authentication (PAP) successful\n");

  if (strstr(line, "AuthNak") != NULL) {
    output ("Authentication (PAP) failed");

    char buf[strlen(line)];
    if( get_quotedtext( buf, line ) )
      output (" (Message: ", buf, ")");
    output ("\n");

    _auth_failed = 1;
  }


  // CHAP stuff

  if (strstr(line, "CHAP Challenge") != NULL)
    output ("Authentication (CHAP) started\n");

  if (strstr(line, "CHAP Success") != NULL)
    output ("Authentication (CHAP) successful\n");

  if (strstr(line, "CHAP Failure") != NULL) {
    output ("Authentication (CHAP) failed");

    char buf[strlen(line)];
    if( get_quotedtext( buf, line ) )
      output (" (Message: ", buf, ")");
    output ("\n");

    _auth_failed = 1;
  }


  // IP stuff

  if (strncmp(line, "local  IP address", 17) == 0)
    output (line, "\n");

  if (strncmp(line, "remote IP address", 17) == 0)
    output (line, "\n");

  if (strncmp(line, "primary   DNS address", 20) == 0)
    output (line, "\n");

  if (strncmp(line, "secondary DNS address", 20) == 0)
    output (line, "\n");


  // Script stuff

  if (strncmp(line, "Script", 6) == 0) {

    if (strstr(line, "/etc/ppp/ip-up") != NULL && strstr(line, "finished") != NULL) {

      if( regexec( &rx_status, line, 1, &rm[0], 0 ) == 0 ) {

	const char *p = &line[ rm[0].rm_eo ];

	// fprintf( stderr, "***** status is `%s' *****\n", p2 );

	if (strcmp( p, "0x0") == 0) {

	  output ("Script /etc/ppp/ip-up run successful\n");

	  if( do_check_dfr ) {
	    if( check_dfr() )
	      output( "Default route Ok.\n" );
	    else
	      output( "Default route failure.\n" );
	  }

	  if( do_check_dns ) {
	    if( check_dns() )
	      output( "Nameserver (DNS) Ok.\n" );
	    else
	      output( "Nameserver (DNS) failure, the connection may not work.\n" );
	  }

	  output ( connectmsg );

//	  // execute whatever the user wants to
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

	} else
	  output ("Script /etc/ppp/ip-up failed (return value: ", p, ")\n");

      } else {
	// fprintf( stderr, "***** no status found *****\n" );
      }

    }

    if (strstr(line, "/etc/ppp/ip-down") != NULL && strstr(line, "started") != NULL)
      output ("Script /etc/ppp/ip-down started\n");
  }


  // TermReq stuff

  if (strstr(line, "LCP TermReq") != NULL) {

    output ("Terminate Request");

    char buf[strlen(line)];
    if( get_quotedtext( buf, line ) )
      output (" (Message: ", buf, ")");
    output ("\n");
  }


  // connect time stuff

  if (strncmp(line, "Connect time", 12) == 0)
    output (line, "\n");


  // interface stuff

  if (strncmp(line, "Using interface", 15) == 0)
    output (line, "\n");


  // terminate stuff

  if( strncmp(line,"Terminating", 11 ) == 0 )
    output (line, "\n");


  return buffer;
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

void
WvDialMon::close_route_table ()
{
  if (route_fd != (FILE *) 0) {
    fclose (route_fd);
    route_fd = (FILE *) 0;
  }
}

/********************************************************************
 *
 * open_route_table - open the interface to the route table
 */

static char route_delims[] = " \t\n";

int
WvDialMon::open_route_table ()
{
  char path[] = "/proc/net/route";

  close_route_table();

  route_fd = fopen (path, "r");
  if (route_fd == NULL) {
    output( "can't read `", path, "\n" );
    return 0;
  }

  route_dev_col = 0;		/* default to usual columns */
  route_dest_col = 1;
  route_gw_col = 2;
  route_flags_col = 3;
  route_mask_col = 7;
  route_num_cols = 8;

  /* parse header line */
  if (fgets(route_buffer, sizeof(route_buffer), route_fd) != 0) {
    char *p = route_buffer, *q;
    int col;
    for (col = 0; col < ROUTE_MAX_COLS; ++col) {
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

int
WvDialMon::read_route_table(struct rtentry *rt)
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

int
WvDialMon::defaultroute_exists (struct rtentry *rt)
{
  int result = 0;

  if (!open_route_table())
    return 0;

  while (read_route_table(rt) != 0) {
    if ((rt->rt_flags & RTF_UP) == 0)
      continue;

    if (kernel_version > KVERSION(2,1,0) && SIN_ADDR(rt->rt_genmask) != 0)
      continue;
    if (SIN_ADDR(rt->rt_dst) == 0L) {
      result = 1;
      break;
    }
  }

  close_route_table();
  return result;
}



int
WvDialMon::check_dfr()
{
  struct rtentry def_rt;

  if( !defaultroute_exists( &def_rt ) )
    return 0;

  // FIXME: check if the gateway is set correct

  return 1;
}



int
WvDialMon::check_dns_name( const char *name )
{
  struct hostent *hp;
  hp = gethostbyname( name );
  if( hp == NULL ) {
    output( "warning, can't find address for `", name, "'\n" );
    return 0;
  }

  // fprintf( stderr, "***** official name for `%s' is `%s' *****\n", name, hp->h_name );

  return 1;
}



int
WvDialMon::check_dns()
{
  char name[] = "/etc/resolv.conf";

  FILE *fin = fopen( name, "r" );
  if( fin == NULL ) {
    output( "warning, can't read `", name, "'\n" );
    return 0;
  }

  const int size = 100;
  char line[size];

  regmatch_t rm[1];
  int found_namesrv = 0;

  while( fgets( line, size, fin ) != NULL ) {

    if( line[0] == '\n' || line[0] == '#' )
      continue;

    if( line[ strlen(line)-1 ] == '\n' )
      line[ strlen(line)-1 ] = '\0';

    if( regexec( &rx_namesrv, line, 1, &rm[0], 0 ) == 0 ) {
      found_namesrv = 1;
      break;
    }

  }

  fclose( fin );

  if( !found_namesrv ) {
    output( "warning, no nameserver found `", name, "'\n" );
    return 0;
  }

  if (!check_dns_name ((const char* ) dnstest1) || !check_dns_name ((const char* ) dnstest2)) {
    output( "warning, address lookup does not work\n" );
    return 0;
  }

  return 1;
}

