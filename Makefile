# $Header: /afs/dev.mit.edu/source/repository/athena/etc/xdm/Makefile,v 1.1 1990-11-18 18:31:00 mar Exp $

all:
	cd dm; make all
	cd wcl; make all
	cd xlogin; make all
	cd console; make all
	cd cleanup; make all

install:
	cd dm; make install DESTDIR=${DESTDIR}
	cd wcl; make install DESTDIR=${DESTDIR}
	cd xlogin; make install DESTDIR=${DESTDIR}
	cd console; make install DESTDIR=${DESTDIR}
	cd conf; make install DESTDIR=${DESTDIR}
	cd cleanup; make install DESTDIR=${DESTDIR}

clean:
	cd dm; make clean
	cd wcl; make clean
	cd xlogin; make clean
	cd console; make clean
	cd cleanup; make clean
