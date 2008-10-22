COMPILER_STANDARD=posix
EXEEXT=
INSTALL=/usr/bin/install -c
INSTALL_DATA=${INSTALL} -m 644
INSTALL_PROGRAM=${INSTALL}
INSTALL_SCRIPT=${INSTALL}
LN_S=ln -s
LN=ln
MOC=/usr/bin/moc

LIBS_DBUS=-ldbus-1
LIBS_QT=-lqt-mt
LIBS_PAM=-lpam
LIBS_TCL=

prefix=/usr/local
datadir=${prefix}/share
includedir=${prefix}/include
infodir=${prefix}/share/info
localstatedir=${prefix}/var
mandir=${prefix}/share/man
sharedstatedir=${prefix}/com
sysconfdir=${prefix}/etc

exec_prefix=${prefix}
bindir=${exec_prefix}/bin
libdir=${exec_prefix}/lib
libexecdir=${exec_prefix}/libexec
sbindir=${exec_prefix}/sbin

enable_debug=yes
enable_optimization=no
enable_resolver_fork=
enable_warnings=
enable_testgui=
