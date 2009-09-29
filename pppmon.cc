

// not finished
// author: arvin@suse.de


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <wvstream.h>

#include "wvdialmon.h"
#include "wvfork.h"
#include "wvfdstream.h"


WvStream *pppd_log = NULL;	// to read messages of pppd

WvDialMon pppd_mon;


int main( int argc, char ** argv )
{
  int argc_ppp = 0;
  const char * argv_ppp[ argc+10 ];
  
  argv_ppp[argc_ppp++] = "/usr/sbin/pppd";
  
  for( int i = 1; i < argc; i++ )
    argv_ppp[argc_ppp++] = argv[i];
  
  
  // open a pipe to access the messages of pppd
  int pppd_msgfd[2];
  if( pipe( pppd_msgfd ) == -1 ) {
    fprintf( stderr, "pipe failed: %s\n", strerror(errno) );
    exit( EXIT_FAILURE );
  }
  
  char buffer[20];
  sprintf( buffer, "%d", pppd_msgfd[1] );
  argv_ppp[argc_ppp++] = "logfd";
  argv_ppp[argc_ppp++] = buffer;
  
  pppd_log = new WvFDStream( pppd_msgfd[0] );
  
  pppd_mon.setconnectmsg( "Connected..." );
  
  
  /*
  for( int i = 0; i < argc_ppp; i++ )
    printf( "%s\n", argv_ppp[i] );
  */
  

  // fork and exec pppd
  pid_t pid = wvfork();
  
  if( pid == (pid_t) 0 ) {	// we are the child
    argv_ppp[argc_ppp] = NULL;
    execv( argv_ppp[0], (char* const*)argv_ppp );
    fprintf( stderr, "exec failed: %s\n", strerror(errno) );
    exit( EXIT_FAILURE );
  }
  
  if( pid < (pid_t) 0 ) {	// the fork failed
    fprintf( stderr, "error: can't fork child process\n" );
    exit( EXIT_FAILURE );
  }

  
  /*
  ppp_pipe = new WvPipe( argv_ppp[0], argv_ppp, false, false, false );
  */
  
  
  // install signals
  
  
  
  for( ;; ) {
    
    // see if pppd is still alive
    
    
    
    
    // now watch for messages and output to stdout
    
    if( pppd_log != NULL && pppd_log->isok() ) {
      
      char *line;
      
      do {
	
	line = pppd_log->blocking_getline( 100 );
	if( line != NULL ) {
	  char *buffer1 = pppd_mon.analyse_line( line );
	  if( buffer1 != NULL && buffer1[0] != '\0' ) {
	    char buffer2[ strlen( buffer1 ) + 10 ];
	    sprintf( buffer2, "pppd: %s", buffer1 );
	    fprintf( stdout, "%s", buffer2 );
	  }
	}
      } while( line != NULL );
      
    }
    
  }
  
  exit( EXIT_SUCCESS );
}
