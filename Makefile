ifeq ($(TOPDIR),)                                                                              
  TOPDIR=.       
  PKGINC=/usr/include/wvstreams /usr/local/include/wvstreams
  LIBS := $(LIBS) \
		$(shell $(CC) -lsupc++ 2>&1 | grep -q "undefined reference" \
			&& echo " -lsupc++")
endif

include $(TOPDIR)/wvrules.mk

XPATH=.. ../wvstreams/include $(PKGINC)

default: all papchaptest
all: wvdial.a wvdial wvdialconf pppmon

wvdial.a: wvdialer.o wvdialtext.o wvmodemscan.o wvpapchap.o wvdialbrain.o \
	wvdialmon.o

LIBS += -L../wvstreams -lwvutils -lwvstreams

wvdial wvdialconf papchaptest pppmon: wvdial.a

clean:
	rm -f wvdial wvdialconf wvdialmon papchaptest pppmon
