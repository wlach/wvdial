/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
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
			Carrier, DTR, FCLASS, GetIdent,
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
    
    bool doresult(WvStringParm s, int msec);
    size_t coagulate(char *buf, size_t size, int msec);
	
public:
    WvModemScan(WvStringParm devname, bool is_modem_link);
    ~WvModemScan();
    
    WvString modem_name;
    bool use_modem_link;

    // check probe status
    bool isdone() const
	{ return stage == Done; }
    bool isok() const;

    // is this an isdn modem?  Returns modem identifier if so.
    const char *is_isdn() const;

    bool use_default_asyncmap() const;

    // continue the probe where we left off
    void execute();

    // after a probe finishes (isdone()==true) these functions return
    // the final status info for the device.
    WvStringParm filename() const
        { return file; }
    int maxbaud() const
        { return baud; }
    WvString initstr() const;
};


// Declare a WvModemScanList, which searches for all available modems.
// After an instance of the class has been created, run execute()
// again and again until isdone()==true; then the contents of the list
// is a set of all available modems, in the form of WvModemScan objects.
DeclareWvList2(WvModemScanListBase, WvModemScan);

class WvModemScanList : public WvModemScanListBase
{
    WvLog log;
    int thisline;
    bool printed;
public:
    WvModemScanList(WvStringParm _exception = WvString::null);
    
    void execute();
    bool isdone();
};


#endif // __WVMODEMSCAN_H
