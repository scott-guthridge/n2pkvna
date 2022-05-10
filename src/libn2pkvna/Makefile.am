ACLOCAL_AMFLAGS= -I m4
PDFROFF= pdfroff
SUFFIXES= .3 .pdf

#
# Library
#
include_HEADERS = n2pkvna.h
lib_LTLIBRARIES = libn2pkvna.la
libn2pkvna_la_SOURCES = archdep.h archdep.c \
	n2pkvna_internal.h n2pkvna_error.c n2pkvna_generate.c \
	n2pkvna_hardware.c n2pkvna_open.c n2pkvna_parse_address.c \
	n2pkvna_parse_config.c n2pkvna_reset.c n2pkvna_save.c n2pkvna_scan.c \
	n2pkvna_switch.c

#
# Man pages
#
dist_man_MANS = n2pkvna.3

#
# Tests
#
#TESTS = vnacommon-test
#check_PROGRAMS = vnacommon-test
#
#vnacommon_test_SOURCES = vnacommon-test.c
#vnacommon_test_LDADD = libn2pkvna.la -lm
#vnacommon_test_LDFLAGS = -static


#
# Examples
#
noinst_PROGRAMS = libn2pkvna-example

libn2pkvna_example_SOURCES = libn2pkvna-example.c
libn2pkvna_example_LDADD = libn2pkvna.la -lvna -lusb-1.0 -lyaml -lm
libn2pkvna_example_LDFLAGS = -static

dist_doc_DATA = libn2pkvna-example.c

.3.pdf:
	$(PDFROFF) -t -e -man --no-toc-relocation $< > "$@"

pdfman: n2pkvna.pdf

clean-local:
	rm -f n2pkvna.pdf
