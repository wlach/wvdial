TOPDIR=../..
include $(TOPDIR)/rules.mk

XPATH=.. ../utils ../configfile ../streams

default: all papchaptest
all: wvdial.a wvdial wvdialconf

wvdial.a: wvdialer.o wvdialtext.o wvmodemscan.o wvpapchap.o wvdialbrain.o

#LIBS = ${EFENCE}

papchaptest: papchaptest.o wvdial.a ../streams/streams.a ../utils/utils.a

wvdial: wvdial.o wvdial.a ../configfile/configfile.a ../streams/streams.a \
	  ../utils/utils.a

atztest: atztest.o wvdial.a ../configfile/configfile.a ../streams/streams.a \
	  ../utils/utils.a

wvdialconf: wvdialconf.o wvdial.a ../configfile/configfile.a \
	  ../streams/streams.a ../utils/utils.a

clean:
	rm -f wvdial wvdialconf papchaptest *.o *.a

