ifeq ($(TOPDIR),)
  TOPDIR=.
  PKGINC=/usr/include/wvstreams /usr/local/include/wvstreams
  LIBS := $(LIBS) \
		$(shell $(CC) -lsupc++ 2>&1 | grep -q "undefined reference" \
			&& echo " -lsupc++")
endif

PREFIX=/usr/local
BINDIR=${PREFIX}/bin
MANDIR=${PREFIX}/share/man
PPPDIR=/etc/ppp/peers

include $(TOPDIR)/wvrules.mk

XPATH=.. ../wvstreams/include $(PKGINC)

default: all papchaptest
all: wvdial.a wvdial wvdialconf pppmon

wvdial.a: wvdialer.o wvdialtext.o wvmodemscan.o wvpapchap.o wvdialbrain.o \
	wvdialmon.o

LIBS += -L../wvstreams -lwvutils -lwvstreams -luniconf

wvdial wvdialconf papchaptest pppmon: wvdial.a

install-bin: all
	[ -d ${BINDIR}      ] || install -d ${BINDIR}
	[ -d ${PPPDIR}      ] || install -d ${PPPDIR}
	install -m 0755 wvdial wvdialconf ${BINDIR}
	cp ppp.provider ${PPPDIR}/wvdial
	cp ppp.provider-pipe ${PPPDIR}/wvdial-pipe

install-man:
	[ -d ${MANDIR}/man1 ] || install -d ${MANDIR}/man1
	[ -d ${MANDIR}/man5 ] || install -d ${MANDIR}/man5
	install -m 0644 wvdial.1 wvdialconf.1 ${MANDIR}/man1
	install -m 0644 wvdial.conf.5 ${MANDIR}/man5

install: install-bin install-man

uninstall-bin:
	rm -f ${BINDIR}/wvdial ${BINDIR}/wvdialconf
	rm -f ${PPPDIR}/wvdial
	rm -f ${PPPDIR}/wvdial-pipe

uninstall-man:
	rm -f ${MANDIR}/man1/wvdial.1 ${MANDIR}/man1/wvdialconf.1
	rm -f ${MANDIR}/man5/wvdial.conf.5

uninstall: uninstall-bin uninstall-man

clean:
	rm -f wvdial wvdialconf wvdialmon papchaptest pppmon

.PHONY: clean all install-bin install-man install uninstall-bin uninstall-man \
	uninstall
