/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 *
 * Intelligent serial port scanner: try to find a port (or ports)
 * with a working modem, guess a basic startup init string, and find
 * the maximum baud rate.
 */
#ifndef __WVMODEMSCAN_H
#define __WVMODEMSCAN_H

#include "wvlinklist.h"
#include "wvlog.h"

class WvModem;


class WvModemScan
{
public:
    enum Stage { Startup=0, AT, ATZ, ATS0, 
			Carrier, DTR, FastDial, FCLASS, GetIdent,
			BaudStep, Reinit, Done, NUM_STAGES };
    
private:
    int stage;
    enum Status { Fail = -1, Unknown = 0, Worked = 1, Test };
    Status status[NUM_STAGES];
    WvLog debug;
    
    WvString file;
    WvString identifier;
    int baud, tries;
    WvModem *modem;
    bool broken;
    
    bool doresult(const WvString &s, int msec);
    size_t coagulate(char *buf, size_t size, int msec);
	
public:
    WvModemScan(const char *devname);
    ~WvModemScan();
    
    // check probe status
    bool isdone() const
	{ return stage == Done; }
    bool isok() const;

    // is this an isdn modem?  Returns modem identifier if so.
    const char *is_isdn() const;

    // continue the probe where we left off
    void execute();

    // after a probe finishes (isdone()==true) these functions return
    // the final status info for the device.
    const WvString &filename() const
        { return file; }
    int maxbaud() const
        { return baud; }
    WvString initstr() const;
};


// Declare a WvModemScanList, which searches for all available modems.
// After an instance of the class has been created, run execute()
// again and again until isdone()==true; then the contents of the list
// is a set of all available modems, in the form of WvModemScan objects.
DeclareWvList2(WvModemScan,
	       WvLog *log;
	       int thisline;
	       bool printed;
	       void setup();
	       void shutdown();
	       void execute();
	       bool isdone();
	       );


#endif // __WVMODEMSCAN_H
