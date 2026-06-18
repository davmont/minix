# $NetBSD: Makefile.inc,v 1.4.14.1 2023/08/11 13:39:44 martin Exp $

BINDIR=/usr/bin

PROGDPLIBS += ${DPLIBKRB5} ${DPLIBHX509} ${DPLIBASN1} ${DPLIBCOM_ERR}
PROGDPLIBS += ${DPLIBWIND} ${DPLIBHEIMBASE} ${DPLIBROKEN} ${DPLIBVERS}
# MINIX: libgssapi.so (pulled in transitively via libkrb5) NEEDs libheimntlm;
# list it so its build dir lands on -L/-rpath-link and the dynamic program link
# can resolve heim_ntlm_* during a clean build.
PROGDPLIBS += ${DPLIBHEIMNTLM}
