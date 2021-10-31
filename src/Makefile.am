ACLOCAL_AMFLAGS= -I m4
libn2pkvna_ladir= $(libdir)

SUBDIRS= libn2pkvna n2pkvna n2pkvna-gui

pdfman:
	for dir in ${SUBDIRS}; do \
		(cd $${dir} && $(MAKE) $(AM_MAKEFLAGS) $@); \
	done

#clean-local:
#	rm -f

.PHONY: pdfman
