ACLOCAL_AMFLAGS= -I m4
applicationsdir=$(datadir)/applications/n2pkvna
icondir=$(datadir)/icons/hicolor/scalable/apps
uidir=$(datadir)/n2pkvna/ui

dist_ui_DATA = n2pkvna.glade
dist_applications_DATA = n2pkvna.desktop
dist_icon_DATA = n2pkvna.svg
bin_SCRIPTS = n2pkvna-gui

dist_noinst_SCRIPTS = n2pkvna-gui.pl make-icon.pl

n2pkvna-gui: n2pkvna-gui.pl
	sed -e 's@%%BINDIR%%@$(bindir)@' \
	    -e 's@%%DOCDIR%%@$(docdir)@' \
	    -e 's@%%UIDIR%%@$(uidir)@' \
	    -e 's@%%VERSION%%@$(PACKAGE_VERSION)@' \
	    $(srcdir)/n2pkvna-gui.pl > "$@" 
	chmod 755 "$@"

n2pkvna.svg: make-icon.pl
	./make-icon.pl
	sed -i \
	    -e 's/\(^[ 	]*width=\).*\(height=\)"[^"]*"$$/\1"256px" \2"256px"/' \
	    -e 's/viewBox="[^"]*"/viewBox="0 0 1200 1200"/' "$@"

pdfman:

clean-local:
	rm -f n2pkvna-gui

# Can install sized icons using install-data-local and ininstall-local targets.
# Create directories using mkdir -p from $(DESTDIR)$(datadir).
