ACLOCAL_AMFLAGS= -I m4
libn2pkvna_ladir= $(libdir)

SUBDIRS= doc firmware src rpm deb

dist_doc_DATA= COPYING README-EXAMPLES
dist_noinst_SCRIPTS= bootstrap

dist_noinst_DATA = \
	debian/changelog \
	debian/control \
	debian/copyright \
	debian/n2pkvna-dev.examples \
	debian/n2pkvna-dev.install \
	debian/n2pkvna-dev.manpages \
	debian/n2pkvna.docs \
	debian/n2pkvna-gui.docs \
	debian/n2pkvna-gui.install \
	debian/n2pkvna.install \
	debian/n2pkvna.manpages \
	debian/not-installed \
	debian/postinst_run \
	debian/postrm_run \
	debian/rules \
	debian/source/format \
	debian/source/options

deb: dist
	(cd deb && $(MAKE) $(AM_MAKEFLAGS) $@)

srcrpm rpm: dist
	(cd rpm && $(MAKE) $(AM_MAKEFLAGS) $@)

pdfman:
	(cd src && $(MAKE) $(AM_MAKEFLAGS) $@)

#clean-local:
#	rm -f

.PHONY: srcrpm rpm pdfman
