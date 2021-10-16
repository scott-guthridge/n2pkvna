ACLOCAL_AMFLAGS= -I m4
PDFROFF= pdfroff
SUFFIXES= .1 .pdf

AM_CFLAGS= -I$(top_srcdir)/src/libn2pkvna

bin_PROGRAMS = n2pkvna

n2pkvna_SOURCES = attenuate.h attenuate.c calibrate.h calibrate.c \
	cf.h cf.c cli.h cli.c configure.h configure.c generate.h generate.c \
	main.h main.c measure.h measure.c switch.h switch.c

n2pkvna_LDADD = ../libn2pkvna/libn2pkvna.la -lvna -lusb-1.0 -lyaml -lm

dist_man_MANS = n2pkvna.1

.1.pdf:
	$(PDFROFF) -t -e -man --no-toc-relocation $< > "$@"

pdfman: n2pkvna.pdf

clean-local:
	rm -f n2pkvna.pdf
