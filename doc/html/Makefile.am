ACLOCAL_AMFLAGS= -I m4
htmldir=$(docdir)/html

dist_html_DATA = calibrate.html generate.html measure.html \
	quick_start.html setup.html \
	calibrate-screen.png gen_quadrature_edit.png \
	gen_quadrature_main.png gen_xy_main.png \
	measure-screen.png setup-edit-dialog.png setup-menu.png \
	cal-model.svg d_RB_2x1.svg d_RFIV_2x1.svg d_RFIV_2x2.svg \
	d_S_2x2.svg E10_1x2.svg E10_2x1.svg E10_2x2.svg \
	E12_2x2f.svg E12_2x2r.svg E16_1x2.svg E16_2x1.svg \
	E16_2x2.svg E8_1x1.svg E8_1x2.svg E8_2x1.svg \
	E8_2x2.svg s_RB_1x1.svg s_RB_1x2.svg s_RB_2x1.svg \
	s_RB_ref_2x2.svg \
	gen_quadrature_waveforms.jpg gen_xy_0.jpg gen_xy_90.jpg

diagrams:
	./make-setup-diagrams
	./make-calibration-diagrams

pdfman:
