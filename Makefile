TOPDIR=..
include $(TOPDIR)/wvrules.mk

XPATH=.. ../wvstreams/include $(PKGINC)

default: all papchaptest
all: wvdial.a wvdial wvdialconf pppmon

wvdial.a: wvdialer.o wvdialtext.o wvmodemscan.o wvpapchap.o wvdialbrain.o \
	wvdialmon.o

LIBS = -L../wvstreams -lwvutils -lwvstreams

wvdial wvdialconf papchaptest atztest pppmon: wvdial.a

clean:
	rm -f wvdial wvdialconf wvdialmon papchaptest pppmon atztest *.o *.a
