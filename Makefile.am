ACLOCAL_AMFLAGS= -I m4
libn2pkvna_ladir= $(libdir)

SUBDIRS= firmware src rpm

dist_doc_DATA= COPYING README-EXAMPLES
dist_noinst_SCRIPTS= bootstrap

srcrpm rpm: dist
	(cd rpm && $(MAKE) $(AM_MAKEFLAGS) $@)

pdfman:
	(cd src && $(MAKE) $(AM_MAKEFLAGS) $@)

#clean-local:
#	rm -f

.PHONY: srcrpm rpm pdfman
