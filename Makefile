TOPDIR=../..
include $(TOPDIR)/wvrules.mk

XPATH=.. ../wvstreams/include

default: all papchaptest
all: wvdial.a wvdial wvdialconf

wvdial.a: wvdialer.o wvdialtext.o wvmodemscan.o wvpapchap.o wvdialbrain.o

#LIBS = ${EFENCE}

wvdial wvdialconf papchaptest atztest: wvdial.a ../wvstreams/libwvstreams.a

clean:
	rm -f wvdial wvdialconf papchaptest atztest *.o *.a

