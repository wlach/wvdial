

// copyright: (C) 2000 by SuSE GmbH
// author: arvin@suse.de


#ifndef __DIALMON_H
#define __DIALMON_H


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <regex.h>

#include <wvbuffer.h>
#include <wvstring.h>


class WvDialMon
{

public:

    WvDialMon ();
    ~WvDialMon ();

    // void setexec( const WvString name ) { executename = name; };
    void setconnectmsg( const WvString name ) { connectmsg = name; };

    void setdnstests (WvString, WvString);
    void setcheckdns (int flag) { do_check_dns = flag; }
    void setcheckdfr (int flag) { do_check_dfr = flag; }

  void reset();

  // the returned buffer is only valid until the next call
  char* analyse_line( const char *line );

  int auth_failed() const;

private:

  // name of program to execute after successful connection
    // WvString executename;

  // message to display after connection has been setup
  WvString connectmsg;

  // time to wait before launch of executename
    // int usleep_time;

  // regex to match `status = '
  regex_t rx_status;

  // regex to match quoted (not empty) text
  regex_t rx_quote;


  // the return value
  char *buffer;
  int size;

  void output(const char *buf1);
  void output(const char *buf1, const char *buf2);
  void output(const char *buf1, const char *buf2, const char *buf3);

  int get_quotedtext(char *dest, const char *line);

  // flag
  int _auth_failed;

  // check defaultroute stuff

  int do_check_dfr;
  int check_dfr();

  int kernel_version;
  FILE *route_fd;
  char route_buffer[512];
  int route_dev_col, route_dest_col, route_gw_col;
  int route_flags_col, route_mask_col;
  int route_num_cols;

  int open_route_table ();
  void close_route_table ();
  int read_route_table (struct rtentry *rt);
  int defaultroute_exists (struct rtentry *rt);

  // check DNS stuff

  int do_check_dns;
  int check_dns();

    WvString dnstest1, dnstest2;

  int check_dns_name( const char *name );
  regex_t rx_namesrv;

};


#endif
