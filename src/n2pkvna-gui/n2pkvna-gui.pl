#!/usr/bin/perl
#
# N2PK Vector Network Analyzer
# Copyright © 2021-2022 D Scott Guthridge <scott_guthridge@rompromity.net>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published
# by the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A11 PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
use utf8;
use open qw( :encoding(UTF-8) :std );
use Browser::Open qw(open_browser);
use Cairo;
use Clone qw( clone );
use Gtk3 '-init';
use Glib 'TRUE', 'FALSE';
use Glib;
use Gtk3;
use YAML::XS;
use strict;
use warnings;

our $DOCDIR     = "%%DOCDIR%%";
our $N2PKVNA	= "%%BINDIR%%/n2pkvna";
our $N2PKVNA_UI	= "%%UIDIR%%/n2pkvna.glade";
our $VERSION    = "%%VERSION%%";

use constant MaxFrequency => 75.0e+6;
use constant NumberRE => qr/^[-+]?([0-9]+(\.[0-9]*)?|\.[0-9]+)([eE][-+][0-9]+)?/;

#
# Default calibration standards
#
use constant CalSingle => "S,O,M";
use constant CalHalf8  => "S-,O-,M-,T";
use constant CalHalf16 => "S-O,S-M,O-M,O-S,M-S,T";
use constant CalFull8  => "S-O,S-M,T";
use constant CalFull12 => "S-O,M-O,M-S,T";
use constant CalFull16 => "S-O,S-M,O-M,O-S,T";

my @HzIndexToMultiplier = ( 1.0, 1000.0, 1.0e+6 );
my @HzIndexToName = (
    "Hz",
    "kHz",
    "MHz"
);
my %HzUnitNameToIndex = (
    "Hz"  => 0,
    "kHz" => 1,
    "MHz" => 2,
);
my @AttenuationIndexToValue = (
    0, 10, 20, 30, 40, 50, 60, 70
);

my $builder;
my $appwindow;

my %CurrentSettings = (
    vna			=> undef,
    properties		=> {},
    result		=> {},
    calibration_files	=> [],
    standards_1port	=> [],
    standards_2port	=> [],
    page		=> "setup",
    busy		=> {
	setup_edit => 0,
	cal_main   => 0,
	m_main     => 0,
    },
    setup_edit_name	=> undef,
    setup_edit_data	=> undef,
    setup_edit_extra	=> undef,
    cal_setup		=> undef,
    cal_type		=> undef,
    cal_standards	=> {
	ports	=> 1,
	list	=> []
    },
    cal_defaults	=> "",
    m_cal		=> undef,
    m_conversions	=> undef,
    m_graph_width	=> 640,
    m_graph_height	=> 480,
    m_parameter		=> "sri",
    m_title		=> {},	# by root parameter
    m_parameter_settings => {
	s => {
	    title       => 'S Parameters',
	    legend	=> 'top right',
	    coordinates => 'ri',
	    # normalize always off
	},
	smith => {
	    title       => '',
	    legend	=> 'top left',
	    # no coordinate options
	    # normalize always off
	},
	t => {
	    title       => 'T Parameters',
	    legend	=> 'top right',
	    coordinates => 'ri',
	    # normalize always off
	},
	u => {
	    title       => 'U Parameters',
	    legend	=> 'top right',
	    coordinates => 'ri',
	    # normalize always off
	},
	z => {
	    title       => 'Z Parameters',
	    legend	=> 'top right',
	    coordinates => 'ri',
	    normalize   => 0,
	},
	y => {
	    title       => 'Y Parameters',
	    legend	=> 'top right',
	    coordinates => 'ri',
	    normalize   => 0,
	},
	h => {
	    title       => 'H Parameters',
	    legend	=> 'top right',
	    coordinates => 'ri',
	    # normalize always on
	},
	g => {
	    title       => 'G Parameters',
	    legend	=> 'top right',
	    coordinates => 'ri',
	    # normalize always on
	},
	a => {
	    title       => 'A Parameters',
	    legend	=> 'top right',
	    coordinates => 'ri',
	    # normalize always on
	},
	b => {
	    title       => 'B Parameters',
	    legend	=> 'top right',
	    coordinates => 'ri',
	    # normalize always on
	},
	zin => {
	    title       => 'Input Impedance',
	    legend	=> 'top right',
	    coordinates => 'ri',
	    normalize   => 1,
	},
	prc => {
	    title       => 'Parallel RC Equivalent',
	    legend	=> 'top right',
	    # no coordinate options
	    # normalize always off
	},
	prl => {
	    title       => 'Parallel RL Equivalent',
	    legend	=> 'top right',
	    # no coordinate options
	    # normalize always off
	},
	src => {
	    title       => 'Series RC Equivalent',
	    legend	=> 'top right',
	    # no coordinate options
	    # normalize always off
	},
	srl => {
	    title       => 'Series RL Equivalent',
	    legend	=> 'top right',
	    # no coordinate options
	    # normalize always off
	},
	rl => {
	    title       => 'Return Loss',
	    legend	=> 'top right',
	    # no coordinate options
	    # normalize always off
	},
	il => {
	    title       => 'Insertion Loss',
	    legend	=> 'top right',
	    # no coordinate options
	    # normalize always off
	},
	vswr => {
	    title       => 'Voltage Standing Wave Ratio',
	    legend	=> 'top right',
	    # no coordinate options
	    # normalize always off
	},
    },
    m_x_ranges		=> {
	F => [ "", "", "MHz", undef ],	# frequency
    },
    m_y_ranges		=> {
	B => [ "", "", undef ],		# bell
	R => [ "", "", "Ω"   ],		# impedance
	Y => [ "", "", "S"   ],		# admittance
	Z => [ "", "", "Ω"   ],		# impedance
    },
    m_y2_ranges		=> {
	A => [ "", "", "degrees" ],	# angle
	C => [ "", "", "nF"      ],	# capacitance
	L => [ "", "", "μH"      ]	# inductance
    },
    m_smith_ranges      => undef,
    gen_RF_frequency	=> 10.0e+6,
    gen_LO_frequency	=> 10.0e+6,
    gen_RF_unit_index	=> 2,
    gen_LO_unit_index	=> 2,
    gen_RF_enable	=> 1,
    gen_LO_enable	=> 1,
    gen_phase		=> 0.0,
    gen_lock_to_RF	=> 1,
    gen_master_enable	=> 0,
    attenuation		=> 0,
);

#
# quit: exit the app
#
sub quit {
    if (defined($CurrentSettings{vna})) {
	$CurrentSettings{vna}->closeVNA();
	$CurrentSettings{vna}->shutdown();
    }
    Gtk3->main_quit();
}

#
# is_yaml_true: test if a YAML bool is true
#
sub is_yaml_true {
    my $value = shift;

    if ($value =~ m/^[yYtT1]/) {
	return 1;
    }
    if ($value =~ m/^on$/i) {
	return 1;
    }
    return undef;
}

#
# set_font: set the font size on a text object
#
sub set_font {
    my $object = shift;
    my $size   = shift;

    return;
    my $attrlist = Pango::AttrList->new();
    my $attr = Pango::AttrSize->new($size*Pango::SCALE);
    Pango::AttrList::insert($attrlist, $attr);
    Gtk3::Label::set_attributes($object, $attrlist);
}

#
# run_command_dialog: interact with the VNA
#
sub run_command_dialog {
    my $cmd              = shift;
    my $input_data       = shift;
    my $callback	 = shift;
    my $callback_arg	 = shift;
    my $activity_message = shift;
    my $cur = \%CurrentSettings;

    #
    # Report an error if the VNA device isn't ready for a command.
    #
    if (!defined($cur->{vna}) || $cur->{vna}{state} ne "ready") {
	my $dialog = Gtk3::MessageDialog->new($appwindow,
		'destroy-with-parent',
		'error', 'ok', "VNA device not open.");
	$dialog->run();
	$dialog->destroy();
	return undef;
    }

    #
    # My context structure
    #
    my $context = {
	callback         => $callback,
	callback_arg     => $callback_arg,
	activity_message => $activity_message,
	progress_window  => undef,
	progress_bar     => undef
    };

    #
    # Send the command.  If synchronous, wait for it.
    #
    $cur->{vna}->sendCommand($cmd, $input_data);
    if (!defined($callback)) {
	my $result = $cur->{vna}->receiveResponse(0);
	return &run_command_callback($context, $result);
    }

    #
    # If an activity message was given, create the activity window.
    #
    if (defined($activity_message)) {
	&show_progress_bar($context, $appwindow);
    }

    #
    # Start the polling timer.
    #
    $context->{timer} = Glib::Timeout->add(200, \&run_command_timer_cb,
	    $context);
}

#
# show_progress_bar
#
sub show_progress_bar {
    my $context = shift;
    my $parent  = shift;

    my $window = Gtk3::Window->new();
    $window->set_decorated(FALSE);
    $window->set_modal(TRUE);
    $window->set_transient_for($parent);
    my $vbox = Gtk3::Box->new("vertical", 5);
    $window->add($vbox);
    my $label = Gtk3::Label->new($context->{activity_message});
    $vbox->pack_start($label, FALSE, FALSE, 5);
    my $alignment = Gtk3::Alignment->new(0.5, 0.5, 0, 0);
    $vbox->pack_start($alignment, FALSE, FALSE, 5);
    my $pbar = Gtk3::ProgressBar->new();
    $alignment->add($pbar);
    $window->show_all();
    $context->{progress_window} = $window;
    $context->{progress_bar}    = $pbar;
}

#
# run_command_timer_cb
#
sub run_command_timer_cb {
    my $context = shift;

    my $cur = \%CurrentSettings;
    my $result = $cur->{vna}->receiveResponse(1);
    if (defined($result)) {
	$context->{timer} = undef;
	if (defined(my $pbar = $context->{progress_bar})) {
	    my $pwindow = $context->{progress_window};

	    $pbar->set_fraction(0.0);
	    $pwindow->destroy();
	    $context->{progress_bar}    = undef;
	    $context->{progress_window} = undef;
	}
	&run_command_callback($context, $result);
	return 0;
    }
    if (defined(my $pbar = $context->{progress_bar})) {
	$pbar->pulse();
    }
    return 1;
}

#
# run_command_callback
#
sub run_command_callback {
    my $context = shift;
    my $result  = shift;
    my $cur = \%CurrentSettings;
    my $lastLine = "Press Continue when ready.";

    my $ack_dialog      = $builder->get_object("ack_dialog");
    my $ack_dialog_grid = $builder->get_object("ack_dialog_grid");

    $cur->{result} = $result;
    die unless ref($result) eq "HASH";
    if ($result->{status} ne "needsACK") {
	$ack_dialog->hide();
    }
    while ($result->{status} ne "ok" && $result->{status} ne "canceled") {
	#
	# If the VNA needs an acknowledgement, show a dialog box
	# of instructions with click to continue or canceled, then
	# loop to process the new result.
	#
	if ($result->{status} eq "needsACK") {
	    my $row = 0;

	    #
	    # Clear out old entries then add the new.
	    #
	    $ack_dialog_grid->foreach(sub { $_[0]->destroy(); });
	    my @instructions;
	    my $data_column = 0;
	    if (defined($result->{instructions})) {
		@instructions = @{$result->{instructions}};
		$data_column = 1;
	    }
	    push(@instructions, $lastLine);
	    foreach my $instruction (@instructions) {
		if ($data_column != 0) {
		    my $bullet = Gtk3::Label->new("•");
		    $bullet->set_yalign(0);
		    &set_font($bullet, 18);
		    $ack_dialog_grid->attach($bullet, 0, $row, 1, 1);
		}
		my $label = Gtk3::Label->new($instruction);
		$label->set_xalign(0);
		&set_font($label, 18);
		$label->set_line_wrap(1);
		$label->set_width_chars(80);
		$label->set_max_width_chars(80);
		$ack_dialog_grid->attach($label, $data_column, $row, 1, 1);
		++$row;
	    }
	    $ack_dialog->show_all();
	    if ($ack_dialog->run() eq "ok") {
		$cur->{vna}->sendACK([], undef);
		if (defined($context->{callback}) &&
			defined($context->{activity_message})) {
		    &show_progress_bar($context, $ack_dialog);
		    $context->{timer} = Glib::Timeout->add(200,
			    \&run_command_timer_cb, $context);
		    return;
		}
	    } else {
		$cur->{vna}->sendACK(["q"], undef);
	    }
	    $result = $cur->{vna}->receiveResponse(0);
	    $cur->{result} = $result;
	    $ack_dialog->hide();
	    next;
	}

	#
	# If the VNA asks for a frequency measurement, show a dialog
	# box to get it.
	#
	if ($result->{status} eq "needsMeasuredF") {
	    my $setup_fcal_dialog =
		$builder->get_object("setup_fcal_dialog");
	    my $setup_fcal_measured =
		$builder->get_object("setup_fcal_measured");
	    my $setup_fcal_measured_unit =
		$builder->get_object("setup_fcal_measured_unit");

	    $setup_fcal_measured->set_text("");
	    $setup_fcal_dialog->show_all();
	    if ($setup_fcal_dialog->run() eq "apply") {
		my $value = $setup_fcal_measured->get_text();
		my $index = $setup_fcal_measured_unit->get_active();
		$value *= $HzIndexToMultiplier[$index] / 1.0e+6;
		$cur->{vna}->sendACK([ $value ], undef);
	    } else {
		$cur->{vna}->sendACK([ "q" ], undef);
	    }
	    $result = $cur->{vna}->receiveResponse(0);
	    $cur->{result} = $result;
	    $setup_fcal_dialog->hide();
	    next;
	}

	#
	# Otherwise, show an error dialog.
	#
	if (!defined($result->{errors})) {
	    $result->{errors} = "";
	}
	$result->{errors} .= "\n";
	$result->{errors} .= sprintf("Unexpected status from VNA: %s\n",
		$result->{status});
	my $dialog = Gtk3::MessageDialog->new($appwindow,
		'destroy-with-parent',
		'error', 'ok', "%s", $result->{errors});
	$dialog->run();
	$dialog->destroy();
	return;
    }

    if (!defined($context->{callback})) {
	return $result;
    }
    if ($result->{status} eq "ok") {
	$context->{callback}($context->{callback_arg}, $result);
    }
}

#
# attenuate_command: update the VNA attenuator
#
sub attenuate_command {
    my $cur = \%CurrentSettings;

    !&run_command_dialog([ "attenuate",
	    $AttenuationIndexToValue[$cur->{attenuation}] ],
	    undef, undef, undef, undef);
}

#
# Units and multipliers.
#
my %Units = (
    A => [
	"degrees",
	"radians"
    ],
    B => [
	"dB"
    ],
    F => [
	"Hz",
	"kHz",
	"MHz"
    ],
    R => [
	"mΩ",
	"Ω",
	"kΩ"
    ],
    Z => [
	"mΩ",
	"Ω",
	"kΩ"
    ],
    Y => [
	"mS",
	"S",
	"kS"
    ],
    C => [
	"pF",
	"nF",
	"μF"
    ],
    L => [
	"nH",
	"μH",
	"mH"
    ],
);

my %UnitToScale = (
    dB		=> 1.0,
    degrees	=> 1.0,
    Hz		=> 1.0,
    kHz		=> 1.0e+3,
    kS		=> 1.0e+3,
    kΩ		=> 1.0e+3,
    mH		=> 1.0e-3,
    MHz		=> 1.0e+6,
    mS		=> 1.0e-3,
    mΩ		=> 1.0e-3,
    nF		=> 1.0e-9,
    nH		=> 1.0e-9,
    pF		=> 1.0e-12,
    radians	=> 57.29578,
    S		=> 1.0,
    μF		=> 1.0e-6,
    μH		=> 1.0e-6,
    Ω		=> 1.0,
);

my %BaseUnitNames = (
    A   => "Angle",
    B   => "dB",
    F   => "Frequency",
    C	=> "Capacitance",
    L	=> "Inductance",
    R	=> "Resistance",
    Y	=> "Admittance",
    Z	=> "Impedance",
);

#
# ParameterToBaseUnits: convert from parameter name to y and y2 units
#
my %ParameterToBaseUnits = (
    sri		=> [ "F",  "1", undef ],
    sma		=> [ "F",  "1", "A"   ],
    sdb		=> [ "F",  "B", "A"   ],
    smith       => [ "1",  "1", undef ],
    tri		=> [ "F",  "1", undef ],
    tdb		=> [ "F",  "B", "A"   ],
    tma		=> [ "F",  "1", "A"   ],
    uri		=> [ "F",  "1", undef ],
    uma		=> [ "F",  "1", "A"   ],
    udb		=> [ "F",  "B", "A"   ],
    zri		=> [ "F",  "Z", undef ],
    zri_n	=> [ "F",  "1", undef ],
    zma		=> [ "F",  "Z", "A"   ],
    zma_n	=> [ "F",  "1", "A"   ],
    yri		=> [ "F",  "Y", undef ],
    yri_n	=> [ "F",  "1", undef ],
    yma		=> [ "F",  "Y", "A"   ],
    yma_n	=> [ "F",  "1", "A"   ],
    hri_n	=> [ "F",  "1", undef ],
    hma_n	=> [ "F",  "1", "A"   ],
    gri_n	=> [ "F",  "1", undef ],
    gma_n	=> [ "F",  "1", "A"   ],
    ari_n	=> [ "F",  "1", undef ],
    ama_n	=> [ "F",  "1", "A"   ],
    bri_n	=> [ "F",  "1", undef ],
    bma_n	=> [ "F",  "1", "A"   ],
    zinri	=> [ "F",  "Z", undef ],
    zinri_n	=> [ "F",  "1", undef ],
    zinma	=> [ "F",  "Z", "A"   ],
    zinma_n	=> [ "F",  "1", "A"   ],
    prc		=> [ "F",  "R", "C"   ],
    src		=> [ "F",  "R", "C"   ],
    prl		=> [ "F",  "R", "L"   ],
    srl		=> [ "F",  "R", "L"   ],
    rl		=> [ "F",  "B", undef ],
    il		=> [ "F",  "B", undef ],
    vswr	=> [ "F",  "1", undef ],
);

#
# parameter_to_x_ranges: given a parameter name, return ranges & units
# Return:
#   ([x_min, x_max, x_unit, logscale], x_base_unit)
#
#   x_min, x_max are empty string if not set, never undef
#   x_unit is the current unit or undef
#   logscale is 0, 1 or undef if not yet set
#
sub parameter_to_x_ranges {
    my $parameter = shift;
    my $cur = \%CurrentSettings;

    my $base_units = $ParameterToBaseUnits{$parameter};
    die "$parameter" unless defined($base_units);
    my $x_base_unit = $base_units->[0];
    my $ranges;
    if ($x_base_unit ne "1") {
	$ranges = $cur->{m_x_ranges}{$x_base_unit};
	die $x_base_unit unless defined($ranges);
    } elsif (!defined($ranges = $cur->{m_x_ranges}{$parameter})) {
	$ranges = [ "", "", undef, undef ];
	$cur->{m_x_ranges}{$parameter} = $ranges;
    }
    return ($ranges, $x_base_unit);
}

#
# parameter_to_y_ranges: given a parameter name, return ranges & units
# Return:
#   ([y_min, y_max, y_unit], y_base_unit)
#
#   y_min, y_max are empty string if not set, never undef
#   y_unit is the current unit or undef
#
sub parameter_to_y_ranges {
    my $parameter = shift;
    my $cur = \%CurrentSettings;

    my $base_units = $ParameterToBaseUnits{$parameter};
    die "$parameter" unless defined($base_units);
    my $y_base_unit = $base_units->[1];
    my $ranges;
    if ($y_base_unit ne "1") {
	$ranges = $cur->{m_y_ranges}{$y_base_unit};
	die $y_base_unit unless defined($ranges);
    } elsif (!defined($ranges = $cur->{m_y_ranges}{$parameter})) {
	$ranges = [ "", "", undef ];
	$cur->{m_y_ranges}{$parameter} = $ranges;
    }
    return ($ranges, $y_base_unit);
}

#
# parameter_to_y2_ranges: given a parameter name, return ranges & units
# Return:
#   ([y2_min, y2_max, y2_unit], y2_base_unit) or (undef, undef)
#
#   y2_min, y2_max are empty string if not set, never undef
#   y2_unit is the current unit or undef
#   returns both as undef if there is no y2 parameter
#
sub parameter_to_y2_ranges {
    my $parameter = shift;
    my $cur = \%CurrentSettings;

    my $base_units = $ParameterToBaseUnits{$parameter};
    die "$parameter" unless defined($base_units);
    my $y2_base_unit = $base_units->[2];
    if (!defined($y2_base_unit)) {
	return (undef, undef);
    }
    my $ranges;
    if ($y2_base_unit ne "1") {
	$ranges = $cur->{m_y2_ranges}{$y2_base_unit};
	die $y2_base_unit unless defined($ranges);
    } elsif (!defined($ranges = $cur->{m_y2_ranges}{$parameter})) {
	$ranges = [ "", "", undef ];
	$cur->{m_y2_ranges}{$parameter} = $ranges;
    }
    return ($ranges, $y2_base_unit);
}

#
# normalize_Hz: convert Hz to scaled version + unit index
#
sub normalize_Hz {
    my $frequency_in_Hz = shift;
    my $index = 2;

    while ($index > 0 && $frequency_in_Hz < $HzIndexToMultiplier[$index]) {
	--$index;
    }
    return ($frequency_in_Hz / $HzIndexToMultiplier[$index], $index);
}

#
# setup_calibrate_frequency_clicked_cb
#
sub setup_calibrate_frequency_clicked_cb {
    my $widget = shift;
    my $arg    = shift;

    my $setup = $builder->get_object("setup");
    $setup->set_visible_child_name("setup_fcal");
}

#
# setup_fcal_calibrate_clicked_cb: start frequency calibration
#
sub setup_fcal_calibrate_clicked_cb {
    my $widget = shift;
    my $arg    = shift;
    my @Cmd;

    my $setup_fcal_expected = $builder->get_object("setup_fcal_expected");
    my $setup_fcal_expected_unit =
	$builder->get_object("setup_fcal_expected_unit");

    push(@Cmd, "cf");
    my $f = $setup_fcal_expected->get_text();
    $f *= $HzIndexToMultiplier[$setup_fcal_expected_unit->get_active()];
    $f /= 1.0e+6;
    push(@Cmd, "-f", sprintf("%e", $f));
    &run_command_dialog(\@Cmd, undef, undef, undef, undef);
}

#
# setup_fcal_back_clicked_cb: return to setup menu
#
sub setup_fcal_back_clicked_cb {
    my $setup = $builder->get_object("setup");
    $setup->set_visible_child_name("setup_menu");
}

sub setup_init_menu {
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{setup_edit});
    my $setup_create_name = $builder->get_object("setup_create_name");
    my $setup_edit_name   = $builder->get_object("setup_edit_name");

    $refcount->hold();
    $setup_create_name->set_text("");
    $setup_edit_name->remove_all();
    my @setup_names = sort { $a cmp $b } keys(%{$cur->{properties}{setups}});
    $setup_edit_name->append(undef, "Select Setup");
    $setup_edit_name->set_active(0);
    foreach my $name (@setup_names) {
	$setup_edit_name->append($name, $name);
    }
    $refcount->release();
}

sub setup_edit_clicked_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;
    my $setup = $builder->get_object("setup");

    my $setup_edit_name = $builder->get_object("setup_edit_name");
    my $id = $setup_edit_name->get_active_id();
    if (defined($id)) {
	&setup_init_editor($id, $cur->{properties}{setups}{$id});
	$setup->set_visible_child_name("setup_editor");
    }
}

sub setup_configure_RB_clicked_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;
    my $setup = $builder->get_object("setup");

    my $setup_edit_name = undef;
    if (!defined($cur->{properties}{setups}{RB})) {
	$cur->{properties}{setups}{RB} = {
	    dimensions	=> "2x1",
	    enabled	=> "y",
	    fmin	=> 50.0e+3,
	    fmax	=> 60.0e+6,
	    steps	=> [
		{
		    measurements =>  [
			{
			    switch	=> undef,
			    detectors	=> [ "b11", "b21" ],
			}
		    ]
		}
	    ],
	};
    }
    &setup_init_editor("RB", $cur->{properties}{setups}{RB});
    $setup->set_visible_child_name("setup_editor");
}

sub setup_configure_S_clicked_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;
    my $setup = $builder->get_object("setup");

    my $setup_edit_name = undef;
    if (!defined($cur->{properties}{setups}{S})) {
	$cur->{properties}{setups}{S} = {
	    dimensions	=> "2x2",
	    enabled	=> "y",
	    fmin	=> 50.0e+3,
	    fmax	=> 60.0e+6,
	    steps	=> [
		{
		    measurements =>  [
			{
			    switch	=> 0,
			    detectors	=> [ "b11", "b21" ],
			},
			{
			    switch	=> 1,
			    detectors	=> [ "b12", "b22" ],
			},
		    ]
		}
	    ],
	};
    }
    &setup_init_editor("S", $cur->{properties}{setups}{S});
    $setup->set_visible_child_name("setup_editor");
}

sub setup_configure_RFIV_clicked_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;
    my $setup = $builder->get_object("setup");

    my $setup_edit_name = undef;
    if (!defined($cur->{properties}{setups}{RFIV})) {
	$cur->{properties}{setups}{RFIV} = {
	    dimensions	=> "2x1",
	    enabled	=> "y",
	    fmin	=> 50.0e+3,
	    fmax	=> 60.0e+6,
	    steps	=> [
		{
		    measurements =>  [
			{
			    switch	=> 0,
			    detectors	=> [ "v11", "b21" ],
			},
			{
			    switch	=> 1,
			    detectors	=> [ "i11", "b21" ],
			},
		    ]
		}
	    ],
	};
    }
    &setup_init_editor("RFIV", $cur->{properties}{setups}{RFIV});
    $setup->set_visible_child_name("setup_editor");
}

sub setup_create_clicked_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;
    my $setup = $builder->get_object("setup");
    my $setup_create_name = $builder->get_object("setup_create_name");

    my $id = $setup_create_name->get_text();
    if ($id ne "") {
	if (defined($cur->{properties}{setups}{$id})) {
	    my $dialog = Gtk3::MessageDialog->new($appwindow,
		    'destroy-with-parent', 'error', 'close',
		    "Setup ${id} exists: use Edit.");
	    $dialog->run();
	    $dialog->destroy();
	    return;
	}
	&setup_init_editor($id, {
	    dimensions	=> "1x1",
	    enabled	=> "y",
	    fmin	=> 50.0e+3,
	    fmax	=> 60.0e+6,
	    steps	=> [
		{
		    measurements => [
			{
			    switch	=> undef,
			    detectors	=> [ undef ]
			}
		    ]
		}
	    ]
	});
	$setup->set_visible_child_name("setup_editor");
    }
}

sub setup_fix_vectors {
    my $cur = \%CurrentSettings;
    my $setup = $cur->{setup_edit_data};
    my $extra = $cur->{setup_edit_extra};
    my $dimensions = $setup->{dimensions};
    my $n_detectors = $extra->{n_detectors};
    die unless defined ($n_detectors);
    my $use_RFIV = $extra->{use_RFIV};
    my $use_references = $extra->{use_references};
    my %legal_vectors;

    if (!($dimensions =~ m/^(\d)x(\d)$/ ||
		$1 < 1 || $1 > 2 || $2 < 1 || $2 > 2)) {
	die $dimensions;
    }
    my $rows = $1;
    my $columns = $2;
    if ($use_references) {
	for (my $i = 0; $i < $rows; ++$i) {
	    for (my $j = 0; $j < $columns; ++$j) {
		my $id = sprintf("a%d%d", $i+1, $j+1);
		$legal_vectors{$id} = 1;
	    }
	}
    }
    for (my $i = 0; $i < $rows; ++$i) {
	for (my $j = 0; $j < $columns; ++$j) {
	    my $id = sprintf("b%d%d", $i+1, $j+1);
	    $legal_vectors{$id} = 1;
	}
    }
    if ($use_RFIV) {
	for (my $i = 0; $i < $rows; ++$i) {
	    for (my $j = 0; $j < $columns; ++$j) {
		my $id = sprintf("i%d%d", $i+1, $j+1);
		$legal_vectors{$id} = 1;
	    }
	}
	for (my $i = 0; $i < $rows; ++$i) {
	    for (my $j = 0; $j < $columns; ++$j) {
		my $id = sprintf("v%d%d", $i+1, $j+1);
		$legal_vectors{$id} = 1;
	    }
	}
    }
    foreach my $step (@{$setup->{steps}}) {
	die unless defined($step);
	foreach my $measurement (@{$step->{measurements}}) {
	    die unless defined($measurement);
	    for (my $i = 0; $i < $n_detectors; ++$i) {
		die unless defined($measurement->{detectors});
		if (!defined($measurement->{detectors}[$i])) {
		    next;
		}
		if (!$legal_vectors{$measurement->{detectors}[$i]}) {
		    $measurement->{detectors}[$i] = undef;
		}
	    }
	}
    }
}

sub setup_build_detector_combobox {
    my $rows           = shift;
    my $columns        = shift;
    my $use_references = shift;
    my $use_RFIV       = shift;

    my $cb = Gtk3::ComboBoxText->new();
    $cb->append("~", "");
    if ($use_references) {
	for (my $i = 0; $i < $rows; ++$i) {
	    for (my $j = 0; $j < $columns; ++$j) {
		my $id = sprintf("a%d%d", $i+1, $j+1);
		$cb->append($id, $id);
	    }
	}
    }
    for (my $i = 0; $i < $rows; ++$i) {
	for (my $j = 0; $j < $columns; ++$j) {
	    my $id = sprintf("b%d%d", $i+1, $j+1);
	    $cb->append($id, $id);
	}
    }
    if ($use_RFIV) {
	for (my $i = 0; $i < $rows; ++$i) {
	    for (my $j = 0; $j < $columns; ++$j) {
		my $id = sprintf("i%d%d", $i+1, $j+1);
		$cb->append($id, $id);
	    }
	}
	for (my $i = 0; $i < $rows; ++$i) {
	    for (my $j = 0; $j < $columns; ++$j) {
		my $id = sprintf("v%d%d", $i+1, $j+1);
		$cb->append($id, $id);
	    }
	}
    }
    return $cb;
}

sub setup_build_table {
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{setup_edit});
    my $setup  = $cur->{setup_edit_data};
    my $steps = scalar(@{$setup->{steps}});
    my $extra = $cur->{setup_edit_extra};
    my $msteps = $extra->{msteps};
    my $table = $builder->get_object("setup_table");

    $setup->{dimensions} =~ m/^(\d)x(\d)$/;
    my $rows    = $1;
    my $columns = $2;

    #
    # Clear old table.
    #
    $refcount->hold();
    while ($table->get_child_at(0, 0) || $table->get_child_at(2, 0)) {
	$table->remove_row(0);
    }
    $extra->{name_widgets} = [];

    my $row = 0;
    {
	my $column = 0;
	if ($msteps > 0) {
	    my $label = Gtk3::Label->new("Manual Step");
	    $table->attach($label, $column, 0, 2, 1);
	    $column += 2;
	}
	if ($extra->{use_switch}) {
	    my $label = Gtk3::Label->new("Switches");
	    $table->attach($label, $column, 0, 1, 1);
	    ++$column;
	}
	if ($extra->{n_detectors} == 2) {
	    my $label1 = Gtk3::Label->new("Detector 1");
	    $table->attach($label1, $column, 0, 1, 1);
	    ++$column;
	    my $label2 = Gtk3::Label->new("Detector 2");
	    $table->attach($label2, $column, 0, 1, 1);
	    ++$column;
	} else {
	    my $label = Gtk3::Label->new("Detector");
	    $table->attach($label, $column, 0, 1, 1);
	    ++$column;
	}
	++$row;
    }
    for (my $step = 0; $step < $steps; ++$step) {
	my $measurements = $setup->{steps}[$step]{measurements};
	my $n = scalar(@{$measurements});
	my $sublines;

	if ($extra->{use_switch}) {
	    $sublines = $n < 4 ? $n + 1 : $n;
	} else {
	    $sublines = 1;
	}
	if ($n == 0 && !$extra->{use_switch}) {
	    push(@{$measurements}, {
		switch		=> undef,
		detectors	=> [ (undef) x $extra->{n_detectors} ],
	    });
	    $n = 1;
	}
	for (my $i = 0; $i < $n; ++$i) {
	    my $measurement = $measurements->[$i];
	    my $column = 0;

	    if ($msteps > 0) {
		if ($i == 0) {
		    my $entry = Gtk3::Entry->new();
		    my $text = $setup->{steps}[$step]{name};
		    $entry->set_text(defined($text) ? $text : "");
		    $table->attach($entry, $column, $row, 1, $sublines);
		    push(@{$extra->{name_widgets}}, $entry);
		    my $button = Gtk3::Button->new("Edit Custom Text");
		    $table->attach($button, $column + 1, $row, 1, $sublines);
		}
		$column += 2;
	    }
	    if ($extra->{use_switch}) {
		my $cbt = Gtk3::ComboBoxText->new();
		if ($n == 1) {
		    $cbt->append("~", "Any");
		}
		$cbt->append(0, "S1=0, S0=0");
		$cbt->append(1, "S1=0, S0=1");
		$cbt->append(2, "S1=1, S0=0");
		$cbt->append(3, "S1=1, S0=1");
		if ($n > 0) {
		    if (defined($measurement->{switch})) {
			$cbt->set_active_id($measurement->{switch});
		    } else {
			$cbt->set_active_id("~");
		    }
		}
		$cbt->signal_connect(changed => \&setup_switch_changed_cb,
			\$measurement->{switch});
		$table->attach($cbt, $column, $row, 1, 1);
		++$column;
	    }
	    {
		my $cb = setup_build_detector_combobox($rows, $columns,
			$extra->{use_references}, $extra->{use_RFIV});
		$cb->set_active_id($measurement->{detectors}[0]);
		$cb->signal_connect(changed => \&setup_vector_changed_cb,
			\$measurement->{detectors}[0]);
		$table->attach($cb, $column, $row, 1, 1);
		++$column;
	    }
	    if ($extra->{n_detectors} == 2) {
		my $cb = setup_build_detector_combobox($rows, $columns,
			$extra->{use_references}, $extra->{use_RFIV});
		$cb->set_active_id($measurement->{detectors}[1]);
		$cb->signal_connect(changed => \&setup_vector_changed_cb,
			\$measurement->{detectors}[1]);
		$table->attach($cb, $column, $row, 1, 1);
		++$column;
	    }
	    if ($extra->{use_switch}) {
		my $button = Gtk3::Button->new_from_stock("gtk-delete");
		$button->set_always_show_image(1);
		$button->signal_connect(clicked => \&setup_delete_clicked_cb,
		    [ $step, $i ]);
		$table->attach($button, $column, $row, 1, 1);
		++$column;
	    }
	    ++$row;
	}
	if ($extra->{use_switch} && $n < 4) {
	    if ($n == 0 && $msteps > 0) {
		    die $sublines unless $sublines == 1;
		    my $entry = Gtk3::Entry->new();
		    my $text = $setup->{steps}[$step]{name};
		    $entry->set_text(defined($text) ? $text : "");
		    $table->attach($entry, 0, $row, 1, $sublines);
		    my $button = Gtk3::Button->new("Edit Custom Text");
		    $table->attach($button, 1, $row, 1, $sublines);
	    }
	    my $button = Gtk3::Button->new_from_stock("gtk-add");
	    my $column = $msteps > 0 ? 2 : 0;
	    $button->set_always_show_image(1);
	    $button->signal_connect(clicked => \&setup_add_clicked_cb,
		    $step );
	    $table->attach($button, $column, $row, 1, 1);
	    ++$row;
	}
    }
    $table->show_all();
    $refcount->release();
}

sub setup_update_names {
    my $cur = \%CurrentSettings;
    my $setup = $cur->{setup_edit_data};
    my $extra = $cur->{setup_edit_extra};
    my $name_widgets = $extra->{name_widgets};

    for (my $i = 0; $i < scalar(@{$name_widgets}); ++$i) {
	my $text = $name_widgets->[$i]->get_text();
	$setup->{steps}[$i]{name} = $text;
    }
}

sub setup_hide_toggled_cb {
    my $widget = shift;
    my $cur = \%CurrentSettings;
    my $setup = $cur->{setup_edit_data};
    $setup->{enabled} = $widget->get_active() ? "n" : "y";
}

sub setup_dimensions_changed_cb {
    my $widget = shift;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{setup_edit});
    if (!$refcount->check_hold()) {
	my $setup = $cur->{setup_edit_data};
	$setup->{dimensions} = $widget->get_active_id();
	&setup_fix_vectors();
	&setup_update_names();
	&setup_build_table();
    }
}

sub setup_msteps_value_changed_cb {
    my $widget = shift;
    my $cur = \%CurrentSettings;
    my $extra = $cur->{setup_edit_extra};
    my $refcount = RefCount->new(\$cur->{busy}{setup_edit});
    if (!$refcount->check_hold()) {
	my $setup = $cur->{setup_edit_data};
	my $msteps = $widget->get_value();
	my $steps = ($msteps > 0) ? $msteps : 1;
	if (scalar($setup->{steps}) > $steps) {
	    $#{$setup->{steps}} = $steps - 1;
	} else {
	    for (my $i = scalar($setup->{steps}); $i < $steps; ++$i) {
		push(@{$setup->{steps}}, []);
	    }
	}
	if (scalar(@{$extra->{name_widgets}}) > $steps) {
	    $#{$extra->{name_widgets}} = $steps - 1;
	}
	if ($msteps == 0) {
	    $setup->{steps}[0]{name} = undef;
	    $setup->{steps}[0]{text} = undef;
	}
	my $n_detectors = $cur->{setup_edit_extra}{n_detectors};
	for (my $i = 0; $i < $steps; ++$i) {
	    if (!defined($setup->{steps}[$i])) {
		$setup->{steps}[$i] = {
		    name         => undef,
		    text         => undef,
		    measurements => []
		};
	    }
	}
	$cur->{setup_edit_extra}{msteps} = $msteps;
	&setup_update_names();
	&setup_build_table();
    }
}

sub setup_det1_toggled_cb {
    my $widget = shift;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{setup_edit});
    if (!$refcount->check_hold()) {
	my $n_detectors = $widget->get_active() ? 1 : 2;
	my $setup = $cur->{setup_edit_data};
	foreach my $step (@{$setup->{steps}}) {
	    foreach my $measurement (@{$step->{measurements}}) {
		$#{$measurement->{detectors}} = $n_detectors - 1;
	    }
	}
	$cur->{setup_edit_extra}{n_detectors} = $n_detectors;
	&setup_update_names();
	&setup_build_table();
    }
}

sub setup_use_switch_toggled_cb {
    my $widget = shift;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{setup_edit});
    if (!$refcount->check_hold()) {
	my $setup = $cur->{setup_edit_data};
	my $use_switch = $widget->get_active();
	if (!$use_switch) {
	    foreach my $step (@{$setup->{steps}}) {
		if (scalar(@{$step->{measurements}}) > 1) {
		    $#{$step->{measurements}} = 0;
		}
	    }
	}
	$cur->{setup_edit_extra}{use_switch} = $use_switch;
	&setup_fix_vectors();
	&setup_update_names();
	&setup_build_table();
    }
}

sub setup_use_RFIV_toggled_cb {
    my $widget = shift;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{setup_edit});
    if (!$refcount->check_hold()) {
	my $use_RFIV = $widget->get_active();
	my $setup = $cur->{setup_edit_data};
	$cur->{setup_edit_extra}{use_RFIV} = $use_RFIV;
	&setup_fix_vectors();
	&setup_update_names();
	&setup_build_table();
    }
}

sub setup_use_references_toggled_cb {
    my $widget = shift;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{setup_edit});
    if (!$refcount->check_hold()) {
	my $use_references = $widget->get_active();
	my $setup = $cur->{setup_edit_data};
	$cur->{setup_edit_extra}{use_references} = $use_references;
	&setup_fix_vectors();
	&setup_update_names();
	&setup_build_table();
    }
}

sub setup_switch_changed_cb {
    my $widget = shift;
    my $switch = shift;

    my $value = $widget->get_active_id();
    $$switch = defined($value) && $value ne "~" ? $value : undef;
}

sub setup_vector_changed_cb {
    my $widget      = shift;
    my $vector      = shift;

    my $value = $widget->get_active_id();
    $$vector = defined($value) && $value ne "~" ? $value : undef;
}

sub setup_delete_clicked_cb {
    my $widget   = shift;
    my $arg      = shift;
    my $step_i   = $arg->[0];
    my $switch_i = $arg->[1];
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{setup_edit});
    if (!$refcount->check_hold()) {
	my $setup = $cur->{setup_edit_data};
	my $step = $setup->{steps}[$step_i];
	my $measurements = $step->{measurements};
	splice(@{$measurements}, $switch_i, 1);
	&setup_update_names();
	&setup_build_table();
    }
}

sub setup_add_clicked_cb {
    my $widget = shift;
    my $step_i = shift;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{setup_edit});
    if (!$refcount->check_hold()) {
	my $setup = $cur->{setup_edit_data};
	my $step = $setup->{steps}[$step_i];
	my $measurements = $step->{measurements};
	my $n = scalar(@{$measurements});
	my $new_switch = undef;
	if ($n == 1 && !defined($measurements->[0]{switch})) {
	    $measurements->[0]{switch} = undef;
	}
	if ($n >= 1) {
	    $new_switch = undef;
	}
	push(@{$measurements}, {
	    switch    => $new_switch,
	    detectors => [ undef, undef ]
	});
	&setup_update_names();
	&setup_build_table();
    }
}

sub setup_init_editor {
    my $name  = shift;
    my $setup = shift;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{setup_edit});
    my %extra;

    $refcount->hold();
    $cur->{setup_edit_data}  = clone($setup);
    $cur->{setup_edit_extra} = \%extra;

    my $setup_editor         = $builder->get_object("setup_editor");
    my $setup_name           = $builder->get_object("setup_name");
    my $setup_hide           = $builder->get_object("setup_hide");
    my $setup_dimensions     = $builder->get_object("setup_dimensions");
    my $setup_fmin           = $builder->get_object("setup_fmin");
    my $setup_fmin_unit      = $builder->get_object("setup_fmin_unit");
    my $setup_fmax           = $builder->get_object("setup_fmax");
    my $setup_fmax_unit      = $builder->get_object("setup_fmax_unit");
    my $setup_fosc           = $builder->get_object("setup_fosc");
    my $setup_fosc_unit      = $builder->get_object("setup_fosc_unit");
    my $setup_msteps         = $builder->get_object("setup_msteps");
    my $setup_det1           = $builder->get_object("setup_det1");
    my $setup_det2           = $builder->get_object("setup_det2");
    my $setup_use_switch     = $builder->get_object("setup_use_switch");
    my $setup_use_RFIV       = $builder->get_object("setup_use_RFIV");
    my $setup_use_references = $builder->get_object("setup_use_references");

    my $value;
    my $index;

    $setup_name->set_text($name);
    if (!defined($setup->{enabled}) || &is_yaml_true($setup->{enabled})) {
	$setup_hide->set_active(0);
    } else {
	$setup_hide->set_active(1);
    }
    $setup_dimensions->set_active_id($setup->{dimensions});

    my $fmin;
    my $fmax;
    my $fosc;
    if (defined($setup->{fmin})) {
	$fmin = $setup->{fmin};
    } else {
	$fmin = 50.0e+3;
	$setup->{fmin} = $fmin;
    }
    if (defined($setup->{fmax})) {
	$fmax = $setup->{fmax};
    } else {
	$fmax = 60.0e+6;
	$setup->{fmax} = $fmax;
    }
    if (defined($setup->{fosc})) {
	$fosc = $setup->{fosc};
    } else {
	$fosc = "";
	$setup->{fosc} = $fosc;
    }
    ( $value, $index ) = &normalize_Hz($fmin);
    $setup_fmin->set_text($value);
    $setup_fmin_unit->set_active($index);
    ( $value, $index ) = &normalize_Hz($fmax);
    $setup_fmax->set_text($value);
    $setup_fmax_unit->set_active($index);
    if ($fosc ne "" && $fosc > 0.0) {
	( $value, $index ) = &normalize_Hz($fosc);
	$setup_fosc->set_text($value);
	$setup_fosc_unit->set_active($index);
    } else {
	$setup_fosc->set_text("");
	$setup_fosc_unit->set_active_id("MHz");
    }
    my $use_switch;
    my $use_RFIV;
    my $use_references;
    my $n_detectors = 1;
    my $steps = scalar(@{$setup->{steps}});
    my $msteps = 0;
    if ($steps > 0 && defined($setup->{steps}[0]{name})) {
	$msteps = $steps;
    }
    $setup_msteps->set_value($msteps);
    foreach my $step (@{$setup->{steps}}) {
	foreach my $measurement (@{$step->{measurements}}) {
	    if (defined($measurement->{switch})) {
		$use_switch = 1;
	    }
	    for (my $i = 0; $i < 2; ++$i) {
		my $detector = $measurement->{detectors}[$i];

		if (defined($detector)) {
		    if ($i == 1) {
			$n_detectors = 2;
		    }
		    if ($detector =~ m/^[iv]/) {
			$use_RFIV = 1;
		    } elsif ($detector =~ m/^a/) {
			$use_references = 1;
		    }
		}
	    }
	}
    }
    if ($n_detectors == 1) {
	$setup_det1->set_active(1);
    } else {
	$setup_det2->set_active(1);
    }
    $extra{msteps}         = $msteps;
    $extra{n_detectors}    = $n_detectors;
    $extra{use_switch}     = $use_switch;
    $extra{use_RFIV}       = $use_RFIV;
    $extra{use_references} = $use_references;

    $setup_use_switch->set_active($use_switch);
    $setup_use_RFIV->set_active($use_RFIV);
    $setup_use_references->set_active($use_references);

    &setup_build_table();
    $setup_editor->show_all();
    $refcount->release();
}

sub setup_cancel_clicked_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;
    my $setup = $builder->get_object("setup");

    $cur->{setup_edit_name} = undef;
    $cur->{setup_edit_data} = undef;
    $cur->{setup_edit_extra} = undef;
    $setup->set_visible_child_name("setup_menu");
}

sub setup_save_clicked_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;
    my $setup = $builder->get_object("setup");
    my $setup_name_widget = $builder->get_object("setup_name");
    my $setup_name = $setup_name_widget->get_text();
    my $setup_dimensions_widget = $builder->get_object("setup_dimensions");
    my $setup_dimensions = $setup_dimensions_widget->get_active_id();
    my $setup_fmin_widget = $builder->get_object("setup_fmin");
    my $setup_fmin_unit_widget = $builder->get_object("setup_fmin_unit");
    my $setup_fmax_widget = $builder->get_object("setup_fmax");
    my $setup_fmax_unit_widget = $builder->get_object("setup_fmax_unit");
    my $setup_fosc_widget = $builder->get_object("setup_fosc");
    my $setup_fosc_unit_widget = $builder->get_object("setup_fosc_unit");
    my $edit_name = $cur->{setup_edit_name};
    my $data  = $cur->{setup_edit_data};
    my $extra = $cur->{setup_edit_extra};
    my $steps = scalar(@{$data->{steps}});
    my $msteps = $extra->{msteps};
    die "steps ${steps} msteps ${msteps}" unless
	$steps == ($msteps == 0 ? 1 : $msteps);

    #
    # Update remaining fields.
    #
    &setup_update_names();
    my $fmin = $setup_fmin_widget->get_text();
    $fmin *= $HzIndexToMultiplier[$setup_fmin_unit_widget->get_active()];
    my $fmax = $setup_fmax_widget->get_text();
    $fmax *= $HzIndexToMultiplier[$setup_fmax_unit_widget->get_active()];
    if ($fmin <= 0 || $fmin > $fmax) {
	my $dialog = Gtk3::MessageDialog->new($appwindow,
		'destroy-with-parent', 'error', 'close',
		'Invalid frequency range.');
	$dialog->run();
	$dialog->destroy();
	return;
    }
    $data->{fmin} = $fmin;
    $data->{fmax} = $fmax;
    my $fosc = $setup_fosc_widget->get_text();
    if (length($fosc) > 0) {
	$fosc *= $HzIndexToMultiplier[$setup_fosc_unit_widget->get_active()];
	$data->{fosc} = $fosc;
	#ZZ: validate fosc
    } else {
	delete($data->{fosc});
    }

    #
    # Validate
    #
    if (length($setup_name) == 0) {
	my $dialog = Gtk3::MessageDialog->new($appwindow,
		'destroy-with-parent', 'error', 'close',
		'Setup name must be given.');
	$dialog->run();
	$dialog->destroy();
	return;
    }
    if (!defined($setup_dimensions)) {
	my $dialog = Gtk3::MessageDialog->new($appwindow,
		'destroy-with-parent', 'error', 'close',
		'Dimensions must be given.');
	$dialog->run();
	$dialog->destroy();
	return;
    }
    my %mstepNamesUsed;
    if ($msteps > 0) {
	for (my $i = 0; $i < $msteps; ++$i) {
	    my $step = $data->{steps}[$i];
	    if (length($step->{name}) == 0) {
		my $dialog = Gtk3::MessageDialog->new($appwindow,
			'destroy-with-parent', 'error', 'close',
			sprintf("Manual step %d needs a name.", $i + 1));
		$dialog->run();
		$dialog->destroy();
		return;
	    }
	    if (defined($mstepNamesUsed{$step->{name}})) {
		my $dialog = Gtk3::MessageDialog->new($appwindow,
			'destroy-with-parent', 'error', 'close',
			sprintf("Manual step \'%s\' used more than once.",
			    $step->{name}));
		$dialog->run();
		$dialog->destroy();
		return;
	    }
	    $mstepNamesUsed{$step->{name}} = 1;
	}
    }
    for (my $i = 0; $i < $steps; ++$i) {
	my $step = $data->{steps}[$i];
	my $n_measurements = scalar(@{$step->{measurements}});
	if ($n_measurements < 1) {
	    my $dialog = Gtk3::MessageDialog->new($appwindow,
		    'destroy-with-parent', 'error', 'close',
		    sprintf("step %d needs at least one measurement.", $i + 1));
	    $dialog->run();
	    $dialog->destroy();
	    return;
	}
	if ($n_measurements == 0) {
	    my $dialog = Gtk3::MessageDialog->new($appwindow,
		    'destroy-with-parent', 'error', 'close',
		    sprintf("Step %d must have at least one measurement.",
			$i + 1));
	    $dialog->run();
	    $dialog->destroy();
	    return;
	}
	my %switchesUsed;
	for (my $j = 0; $j < $n_measurements; ++$j) {
	    my $measurement = $step->{measurements}[$j];
	    if ($n_measurements > 1 && (!defined($measurement->{switch}) ||
		    defined($switchesUsed{$measurement->{switch}}))) {
		my $dialog = Gtk3::MessageDialog->new($appwindow,
			'destroy-with-parent', 'error', 'close',
			sprintf("In step %d, switch values must be unique.",
			    $i + 1));
		$dialog->run();
		$dialog->destroy();
		return;
	    }
	    if (defined($measurement->{switch})) {
		$switchesUsed{$measurement->{switch}} = 1;
	    } else {
		$switchesUsed{"~"} = 1;
	    }
	    my $n_detectors = $extra->{n_detectors};
	    if ($n_detectors == 1) {
		if (!defined($measurement->{detectors}[0])) {
		    my $dialog = Gtk3::MessageDialog->new($appwindow,
			    'destroy-with-parent', 'error', 'close',
			    sprintf("Setector selection needed in " .
				"step %d measurement %d.",
				$i + 1, $j + 1));
		    $dialog->run();
		    $dialog->destroy();
		    return;
		}
	    } elsif ($n_detectors == 2) {
		if (!defined($measurement->{detectors}[0]) &&
			!defined($measurement->{detectors}[1])) {
		    my $dialog = Gtk3::MessageDialog->new($appwindow,
			    'destroy-with-parent', 'error', 'close',
			    sprintf("Detector selection needed in " .
				"step %d measurement %d.",
				$i + 1, $j + 1));
		    $dialog->run();
		    $dialog->destroy();
		    return;
		}
		if (defined($measurement->{detectors}[0]) &&
			defined($measurement->{detectors}[1]) &&
			$measurement->{detectors}[0] eq
			$measurement->{detectors}[1]) {
		    my $dialog = Gtk3::MessageDialog->new($appwindow,
			    'destroy-with-parent', 'error', 'close',
			    sprintf("Detectors must have unique values in " .
				"step %d measurement %d.",
				$i + 1, $j + 1));
		    $dialog->run();
		    $dialog->destroy();
		    return;
		}
	    } else {
		die "n_detectors ${n_detectors}";
	    }
	}
    }
    if ((!defined($edit_name) || $setup_name ne $edit_name) &&
	    exists($cur->{properties}{setups}{$setup_name})) {
	my $dialog = Gtk3::MessageDialog->new($appwindow,
		'destroy-with-parent', 'question', 'ok_cancel',
		"Overwrite ${setup_name}?");
	if ($dialog->run() ne "ok") {
	    $dialog->destroy();
	    return;
	}
	$dialog->destroy();
    }
    if (!&run_command_dialog([ "setup", "yload" ],
		{ $setup_name => $data }, undef, undef, undef)) {
	return;
    }
    $cur->{properties}{setups}{$setup_name} = $data;
    &setup_init_menu();
    &setup_build_table();
    &cal_build_setup();

    $cur->{setup_edit_name} = undef;
    $cur->{setup_edit_data} = undef;
    $cur->{setup_edit_extra} = undef;
    $setup->set_visible_child_name("setup_menu");
}

#
# m_build_select_calibration: build the select calibration combo box
#
sub m_build_select_calibration {
    my $m_select_calibration = $builder->get_object("m_select_calibration");
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});

    $refcount->hold();
    $m_select_calibration->remove_all();
    $m_select_calibration->append_text("Select Calibration");
    $m_select_calibration->set_active(0);
    for (my $i = 0; $i < scalar(@{$cur->{calibration_files}}); ++$i) {
	$m_select_calibration->append_text(
		$cur->{calibration_files}[$i]{calfile});
    }
    $refcount->release();
}

#
# m_select_calibration_chanbed_cb: handle calibration selection
#
sub m_select_calibration_changed_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }

    if ($widget->get_active() == 0) {
	return;
    }
    my $basename = $widget->get_active_text();
    if (!defined($basename)) {
	return;
    }
    my $cal;
    for (my $i = 0; $i < scalar(@{$cur->{calibration_files}}); ++$i) {
	if ($cur->{calibration_files}[$i]{calfile} eq $basename) {
	    $cal = $cur->{calibration_files}[$i]{calibrations}->[0];
	    #ZZ: figure out what to do about multiple calibrations
	    #    this depends mostly on whether we need to calibrate
	    #    for the attenuator -- if so, we'll use multiple cals
	    #    internally under each calibration file
	    last;
	}
    }
    if (!defined($cal)) {
	return;
    }
    $cur->{m_cal} = $cal;
    my $m_fmin		= $builder->get_object("m_fmin");
    my $m_fmin_unit	= $builder->get_object("m_fmin_unit");
    my $m_fmax		= $builder->get_object("m_fmax");
    my $m_fmax_unit	= $builder->get_object("m_fmax_unit");
    my $m_steps		= $builder->get_object("m_steps");
    my $m_log		= $builder->get_object("m_log");
    my $m_symmetrical	= $builder->get_object("m_symmetrical");

    if ($cal->{fmin} >= 1.0e+6) {
	$m_fmin->set_text($cal->{fmin} / 1.0e+6);
	$m_fmin_unit->set_active(2);
    } else {
	$m_fmin->set_text($cal->{fmin} / 1.0e+3);
	$m_fmin_unit->set_active(1);
    }
    if ($cal->{fmax} >= 1.0e+6) {
	$m_fmax->set_text($cal->{fmax} / 1.0e+6);
	$m_fmax_unit->set_active(2);
    } else {
	$m_fmax->set_text($cal->{fmax} / 1.0e+3);
	$m_fmax_unit->set_active(1);
    }
    $m_steps->set_text($cal->{frequencies});

    my $frequency_spacing = $cal->{properties}{frequencySpacing};
    $m_log->set_active(defined($frequency_spacing) &&
	    $frequency_spacing eq "log");
    if ($cal->{rows} * $cal->{columns} == 2) {
	$m_symmetrical->set_sensitive(1);
    } else {
	$m_symmetrical->set_active(0);
	$m_symmetrical->set_sensitive(0);
    }
}

#
# m_start_scan_clicked_cb: start a VNA measurement
#
sub m_start_scan_clicked_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }

    my $m_select_calibration	= $builder->get_object("m_select_calibration");
    my $m_fmin			= $builder->get_object("m_fmin");
    my $m_fmin_unit		= $builder->get_object("m_fmin_unit");
    my $m_fmax			= $builder->get_object("m_fmax");
    my $m_fmax_unit		= $builder->get_object("m_fmax_unit");
    my $m_steps			= $builder->get_object("m_steps");
    my $m_symmetrical		= $builder->get_object("m_symmetrical");

    my $calibration_name = $m_select_calibration->get_active_text();
    if ($calibration_name eq "Select Calibration") {
	my $error_dialog =
	    Gtk3::MessageDialog->new($appwindow, 'destroy-with-parent',
		    'error', 'close', 'You must select a calibration.');
	$error_dialog->run();
	$error_dialog->destroy();
	return;
    }
    my $convert = N2PKVNAConvert->new();

    my $fmin = $m_fmin->get_text() *
	$HzIndexToMultiplier[$m_fmin_unit->get_active()];
    my $fmax = $m_fmax->get_text() *
	$HzIndexToMultiplier[$m_fmax_unit->get_active()];
    my @Cmd;
    push(@Cmd, "m");
    push(@Cmd, "-f", sprintf("%e:%e", $fmin / 1.0e+6, $fmax / 1.0e+6));
    push(@Cmd, "-n", $m_steps->get_text());
    push(@Cmd, "-o", $convert->getdir() . "/sri.npd");
    if ($m_symmetrical->get_active()) {
	push(@Cmd, "-y");
    }
    push(@Cmd, $calibration_name);
    &run_command_dialog(\@Cmd, undef,
	    \&m_start_scan_command_cb, $convert, "Measuring...");
}

#
# m_start_scan_command_cb
#
sub m_start_scan_command_cb {
    my $convert = shift;
    my $result  = shift;
    my $cur = \%CurrentSettings;
    # Must allow this callback in the context of another.

    my $m_log		= $builder->get_object("m_log");
    my $m_logscale_x	= $builder->get_object("m_logscale_x");

    my $cal     = $cur->{m_cal};
    my $rows    = $cal->{rows};
    my $columns = $cal->{columns};
    my $ports   = $rows >= $columns ? $rows : $columns;
    $convert->set_ready($ports);
    $cur->{m_conversions} = $convert;
    $m_logscale_x->set_active($m_log->get_active());
    $cur->{m_smith_ranges} = undef;

    &m_plot();
}

#
# add_file_filters
#
sub add_file_filters {
    my $file_chooser = shift;

    my $filter = Gtk3::FileFilter->new();
    $filter->set_name("Network Parameter Data");
    $filter->add_pattern("*.s[1-9]p");
    $filter->add_pattern("*.ts");
    $filter->add_pattern("*.npd");
    $file_chooser->add_filter($filter);
}

#
# m_load_clicked_cb: load network parameters from a file
#
sub m_load_clicked_cb {
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $m_logscale_x = $builder->get_object("m_logscale_x");

    my $dialog = Gtk3::FileChooserNative->new(
	    "Load Network Parameters From File",
	    $appwindow,
	    "open",
	    "_Open",
	    "_Cancel");
    &add_file_filters($dialog);
    my $res = $dialog->run();
    if ($res eq "ok" || $res == -3) {
	my $filename = $dialog->get_filename();
	my $convert = N2PKVNAConvert->new();
	my $directory = $convert->{directory};
	my $outputfile = $directory . "/sri.npd";
	my @Cmd = ("convert", "-p", "Sri", $filename, $outputfile);
	if (!&run_command_dialog(\@Cmd, undef, undef, undef, undef)) {
	    return;
	}
	my $metadata = $cur->{result}{metadata};
	die unless defined($metadata);
	my $frequencies = $metadata->{frequencies};
	my $fmin	= $metadata->{fmin};
	my $fmax	= $metadata->{fmax};
	my $ports	= $metadata->{ports};

	#
	# Default m_logscale_x based on the frequency range.
	#
	# TODO: could actually look at the frequency points in n2pkvna
	# to do this more accurately.
	#
	if ($fmin > 0 && $frequencies > 2 && $fmax / $fmin >= 50.0) {
	    $m_logscale_x->set_active(1);
	} else {
	    $m_logscale_x->set_active(0);
	}
	$convert->set_ready($ports);
	$cur->{m_conversions} = $convert;
	&m_plot();
    }
    $dialog->destroy();
}

#
# m_save_clicked_cb: save the scan
#
sub m_save_clicked_cb {
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $parameter = $cur->{m_parameter};
    if ($parameter eq "smith") {
	$parameter = "sri";
    }
    $parameter =~ s/_n$//;	# remove normalize suffix
    my $convert = $cur->{m_conversions};
    if (!defined($convert) || !defined($convert->{typeset}{sri})) {
	return;
    }
    my $dialog = Gtk3::FileChooserNative->new(
	    "Save Network Parameters",
	    $appwindow,
	    "save",
	    "_Save",
	    "_Cancel");
    &add_file_filters($dialog);
    my $res = $dialog->run();
    if ($res eq "ok" || $res == -3) {
	my $m_parameters  = $builder->get_object("m_parameters");
	my $directory = $convert->{directory};
	my $inputfile = $directory . "/sri.npd";

	my $filename = $dialog->get_filename();
	my @Cmd = ("convert", "-p", $parameter);
	push(@Cmd, $inputfile, $filename);
	&run_command_dialog(\@Cmd, undef, undef, undef, undef);
    }
    $dialog->destroy();
};

sub update_m_parameter {
    my $cur = \%CurrentSettings;
    my $m_parameters  = $builder->get_object("m_parameters");
    my $m_coordinates = $builder->get_object("m_coordinates");
    my $m_normalize   = $builder->get_object("m_normalize");
    my $parameter_id   = $m_parameters->get_active_id();
    my $coordinates_id = $m_coordinates->get_active_id();
    my $normalize      = $m_normalize->get_active();

    die unless defined($parameter_id);
    my $parameter = $parameter_id;
    if (defined($coordinates_id)) {
	$parameter .= $coordinates_id;
    }
    if ($normalize) {
	$parameter .= "_n";
    }
    die "$parameter" unless defined($ParameterToBaseUnits{$parameter});
    $cur->{m_parameter} = $parameter;
}

#
# build_m_range_units
#
sub build_m_range_units {
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    my $parameter  = $cur->{m_parameter};

    my $m_logscale_x = $builder->get_object("m_logscale_x");
    my $m_x_min      = $builder->get_object("m_x_min");
    my $m_x_max      = $builder->get_object("m_x_max");
    my $m_x_unit     = $builder->get_object("m_x_unit");
    my $m_y_min      = $builder->get_object("m_y_min");
    my $m_y_max      = $builder->get_object("m_y_max");
    my $m_y_unit     = $builder->get_object("m_y_unit");
    my $m_y2_min     = $builder->get_object("m_y2_min");
    my $m_y2_max     = $builder->get_object("m_y2_max");
    my $m_y2_unit    = $builder->get_object("m_y2_unit");

    #
    # Set m_x_min, m_x_max, m_x_unit and m_logscale_x
    #
    $refcount->hold();
    my $units;
    {
	my ($x_ranges, $x_base_unit) = &parameter_to_x_ranges($parameter);
	$m_x_unit->remove_all();
	if (defined($units = $Units{$x_base_unit})) {
	    foreach my $entry (@{$units}) {
		$m_x_unit->append($entry, $entry);
	    }
	}
	my ($x_min, $x_max, $x_unit, $x_logscale) = @{$x_ranges};
	$m_x_min->set_text($x_min);
	$m_x_max->set_text($x_max);
	if (defined($x_unit)) {
	    $m_x_unit->set_active_id($x_unit);
	} else {
	    $m_x_unit->set_active(-1);
	}
	$m_logscale_x->set_active($x_logscale);
	$m_logscale_x->set_sensitive($x_base_unit eq "F");
    }

    #
    # Set m_y_min, m_y_max, and m_y_unit
    #
    {
	my ($y_ranges, $y_base_unit) = &parameter_to_y_ranges($parameter);
	$m_y_unit->remove_all();
	if (defined($units = $Units{$y_base_unit})) {
	    foreach my $entry (@{$units}) {
		$m_y_unit->append($entry, $entry);
	    }
	}
	my ($y_min, $y_max, $y_unit) = @{$y_ranges};
	$m_y_min->set_text($y_min);
	$m_y_max->set_text($y_max);
	if (defined($y_unit)) {
	    $m_y_unit->set_active_id($y_unit);
	} else {
	    $m_y_unit->set_active(-1);
	}
    }

    #
    # Set m_y2_min, m_y2_max and m_y2_unit
    #
    {
	my ($y2_ranges, $y2_base_unit) = &parameter_to_y2_ranges($parameter);
	my ($y2_min, $y2_max, $y2_unit) = ("", "", undef);
	$m_y2_unit->remove_all();
	if (defined($y2_base_unit)) {
	    if (defined($units = $Units{$y2_base_unit})) {
		foreach my $entry (@{$units}) {
		    $m_y2_unit->append($entry, $entry);
		}
	    }
	    ($y2_min, $y2_max, $y2_unit) = @{$y2_ranges};
	    $m_y2_min->set_text($y2_min);
	    $m_y2_max->set_text($y2_max);
	    $m_y2_min->set_sensitive(1);
	    $m_y2_max->set_sensitive(1);
	} else {
	    $m_y2_min->set_sensitive(0);
	    $m_y2_max->set_sensitive(0);
	}
	$m_y2_min->set_text($y2_min);
	$m_y2_max->set_text($y2_max);
	if (defined($y2_unit)) {
	    $m_y2_unit->set_active_id($y2_unit);
	} else {
	    $m_y2_unit->set_active(-1);
	}
    }
    $refcount->release();
}

#
# m_parameters_changed: respond to parameter type changes
#
sub m_parameters_changed_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $m_parameters = $builder->get_object("m_parameters");
    my $m_title      = $builder->get_object("m_title");
    my $m_normalize  = $builder->get_object("m_normalize");
    my $m_legend     = $builder->get_object("m_legend");

    #
    # Update the title from the shadowed entry.
    #
    my $root_parameter = $m_parameters->get_active_id();
    my $settings = $cur->{m_parameter_settings}{$root_parameter};
    $m_title->set_text($settings->{title});

    #
    # Rebuild the coordinates comboBox.
    #
    &build_m_coordinates();

    #
    # Update the normalize button.
    #
    if ($root_parameter =~ m/^[zy]$/ || $root_parameter eq "zin") {
	$m_normalize->set_sensitive(1);
	$m_normalize->set_active($settings->{normalize});
    } elsif ($root_parameter =~ m/^[abgh]$/) {
	$m_normalize->set_sensitive(0);
	$m_normalize->set_active(1);
    } else {
	$m_normalize->set_sensitive(0);
	$m_normalize->set_active(0);
    }

    #
    # Update the legend position.
    #
    $m_legend->set_active_id($settings->{legend});

    #
    # Rebuild the coordinates comboBox and range units, and replot.
    #
    &update_m_parameter();
    &build_m_range_units();
    &m_plot();
}

#
# build_m_coordinates build the parameters combo-box based on parameter
#
sub build_m_coordinates {
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});

    my $m_parameters = $builder->get_object("m_parameters");
    my $m_coordinates = $builder->get_object("m_coordinates");

    my $root_parameter = $m_parameters->get_active_id();
    my $settings = $cur->{m_parameter_settings}{$root_parameter};

    $refcount->hold();
    $m_coordinates->remove_all();
    if ($root_parameter =~ m/^[abghstuyz]$/ || $root_parameter eq "zin") {
	$m_coordinates->append("ri", "Real-Imaginary");
	$m_coordinates->append("ma", "Magnitude-Angle");
    }
    if ($root_parameter =~ m/^[stu]$/) {
	$m_coordinates->append("db", "dB-Angle");
    }
    $m_coordinates->set_active_id($settings->{coordinates});
    $refcount->release();
}

#
# m_coordinates_changed_cb: respond to coordinate changes
#
sub m_coordinates_changed_cb {
    my ($m_coordinates, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $coordinates = $m_coordinates->get_active_id();
    if (!defined($coordinates)) {
	return;
    }
    my $m_parameters = $builder->get_object("m_parameters");

    my $root_parameter = $m_parameters->get_active_id();
    my $settings = $cur->{m_parameter_settings}{$root_parameter};
    $settings->{coordinates} = $coordinates;
    &update_m_parameter();
    &build_m_range_units();
    &m_plot();
}

#
# m_x_min_focus_out_event_cb: respond to change in m_x_min
#
sub m_x_min_focus_out_event_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $parameter = $cur->{m_parameter};
    my ($x_ranges, $x_base_unit) = &parameter_to_x_ranges($parameter);

    my $m_x_min = $builder->get_object("m_x_min");

    my $text = $m_x_min->get_text();
    $text =~ s/^\s+(.*)\s+$/$1/;
    if ($text eq "" || $text =~ NumberRE) {
	$x_ranges->[0] = $text;
    } else {
	$m_x_min->set_text($x_ranges->[0]);
    }
}

#
# m_x_max_focus_out_event_cb: respond to change in m_x_max
#
sub m_x_max_focus_out_event_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $parameter = $cur->{m_parameter};
    my ($x_ranges, $x_base_unit) = &parameter_to_x_ranges($parameter);

    my $m_x_max = $builder->get_object("m_x_max");

    my $text = $m_x_max->get_text();
    $text =~ s/^\s+(.*)\s+$/$1/;
    if ($text eq "" || $text =~ NumberRE) {
	$x_ranges->[1] = $text;
    } else {
	$m_x_max->set_text($x_ranges->[1]);
    }
}

#
# m_x_unit_changed_cb: respond to change in m_x_unit
#
sub m_x_unit_changed_cb {
    my ($widget) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $parameter = $cur->{m_parameter};
    my ($x_ranges, $x_base_unit) = &parameter_to_x_ranges($parameter);

    my $m_x_unit = $builder->get_object("m_x_unit");
    my $id = $m_x_unit->get_active_id();
    if (defined($id)) {
	$x_ranges->[2] = $id;
    }
}

#
# m_logscale_x_toggled_cb: respond to change in logscale checkbox
#
sub m_logscale_x_toggled_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $parameter = $cur->{m_parameter};
    my ($x_ranges, $x_base_unit) = &parameter_to_x_ranges($parameter);

    my $m_logscale_x = $builder->get_object("m_logscale_x");
    $x_ranges->[3] = $m_logscale_x->get_active();
}

#
# m_y_min_focus_out_event_cb: respond to change in m_y_min
#
sub m_y_min_focus_out_event_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $parameter = $cur->{m_parameter};
    my ($y_ranges, $y_base_unit) = &parameter_to_y_ranges($parameter);

    my $m_y_min = $builder->get_object("m_y_min");

    my $text = $m_y_min->get_text();
    $text =~ s/^\s+(.*)\s+$/$1/;
    if ($text eq "" || $text =~ NumberRE) {
	$y_ranges->[0] = $text;
    } else {
	$m_y_min->set_text($y_ranges->[0]);
    }
}

#
# m_y_max_focus_out_event_cb: respond to change in m_y_max
#
sub m_y_max_focus_out_event_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $parameter = $cur->{m_parameter};
    my ($y_ranges, $y_base_unit) = &parameter_to_y_ranges($parameter);

    my $m_y_max = $builder->get_object("m_y_max");

    my $text = $m_y_max->get_text();
    $text =~ s/^\s+(.*)\s+$/$1/;
    if ($text eq "" || $text =~ NumberRE) {
	$y_ranges->[1] = $text;
    } else {
	$m_y_max->set_text($y_ranges->[1]);
    }
}

#
# m_y_min_focus_out_event_cb: respond to change in m_y_unit
#
sub m_y_unit_changed_cb {
    my ($widget) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $parameter = $cur->{m_parameter};
    my ($y_ranges, $y_base_unit) = &parameter_to_y_ranges($parameter);

    my $m_y_unit  = $builder->get_object("m_y_unit");
    $y_ranges->[2] = $m_y_unit->get_active_id();
}

#
# m_y2_min_focus_out_event_cb: respond to change in m_y2_min
#
sub m_y2_min_focus_out_event_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $parameter = $cur->{m_parameter};
    my ($y2_ranges, $y2_base_unit) = &parameter_to_y2_ranges($parameter);

    my $m_y2_min = $builder->get_object("m_y2_min");

    my $text = $m_y2_min->get_text();
    $text =~ s/^\s+(.*)\s+$/$1/;
    if ($text eq "" || $text =~ NumberRE) {
	$y2_ranges->[0] = $text;
    } else {
	$m_y2_min->set_text($y2_ranges->[0]);
    }
}

#
# m_y2_max_focus_out_event_cb: respond to change in m_y2_max
#
sub m_y2_max_focus_out_event_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $parameter = $cur->{m_parameter};
    my ($y2_ranges, $y2_base_unit) = &parameter_to_y2_ranges($parameter);

    my $m_y2_max = $builder->get_object("m_y2_max");

    my $text = $m_y2_max->get_text();
    $text =~ s/^\s+(.*)\s+$/$1/;
    if ($text eq "" || $text =~ NumberRE) {
	$y2_ranges->[1] = $text;
    } else {
	$m_y2_max->set_text($y2_ranges->[1]);
    }
}

#
# m_y2_min_focus_out_event_cb: respond to change in m_y2_unit
#
sub m_y2_unit_changed_cb {
    my ($widget) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $parameter = $cur->{m_parameter};
    my ($y2_ranges, $y2_base_unit) = &parameter_to_y2_ranges($parameter);

    my $m_y2_unit  = $builder->get_object("m_y2_unit");
    $y2_ranges->[2] = $m_y2_unit->get_active_id();
}

#
# m_normalize_toggled_cb: respond to change in normalize checkbox
#
sub m_normalize_toggled_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }

    my $m_parameters = $builder->get_object("m_parameters");
    my $m_normalize  = $builder->get_object("m_normalize");

    my $root_parameter = $m_parameters->get_active_id();
    my $settings = $cur->{m_parameter_settings}{$root_parameter};
    die unless exists($settings->{normalize});
    $settings->{normalize} = $m_normalize->get_active();
    &update_m_parameter();
    &build_m_range_units();
    &m_plot();
}

#
# m_legend_changed_cb
#
sub m_legend_changed_cb {
    my ($m_legend, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $m_parameters = $builder->get_object("m_parameters");
    my $root_parameter = $m_parameters->get_active_id();
    my $settings = $cur->{m_parameter_settings}{$root_parameter};
    $settings->{legend} = $m_legend->get_active_id();
    &m_plot();
}

#
# m_replot_clicked_cb: handle the replot button
#
sub m_replot_clicked_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    &m_plot();
}

#
# m_graph_size_allocate_cb: detect changes to the graph size and replot
#
sub m_graph_size_allocate_cb {
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    my $m_graph = $builder->get_object("m_graph");
    my $rectangle = $m_graph->get_allocation();
    my $height = $rectangle->{height};
    my $width  = $rectangle->{width};

    if (defined($height) && $height > 0 &&
	    defined($width) && $width > 0 &&
	    ($height != $cur->{m_graph_height} ||
	     $width  != $cur->{m_graph_width})) {
	$cur->{m_graph_height} = $height;
	$cur->{m_graph_width}  = $width;
	m_plot();
    }
}

#
# m_graph_draw_cb: draw the graph image
#
sub m_graph_draw_cb {
    my ($widget, $cairo, $arg) = @_;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{m_main});
    if ($refcount->check_hold()) {
	return;
    }
    if (defined(my $pixbuf = $cur->{m_pixbuf})) {
	Gtk3::Gdk::cairo_set_source_pixbuf($cairo, $pixbuf, 0, 0);
	$cairo->paint();
    }
    return FALSE;
}

sub smith_grid {
    my ($plot, $xmin, $xmax, $ymin, $ymax) = @_;
    my $PI = 3.14159265358979323844;
    my $SGRID_OPTS = "notitle dt 2 lc rgb 'gray'";

    #
    # Set up parametric plot.
    #
    printf $plot ("set parametric\n");
    printf $plot ("set size ratio -1\n");
    printf $plot ("set xrange [%g:%g]\n", $xmin, $xmax);
    printf $plot ("set yrange [%g:%g]\n", $ymin, $ymax);
    printf $plot ("set trange [-pi:pi]\n");
    printf $plot ("set xlabel 'real'\n");
    printf $plot ("set ylabel 'imaginary'\n");

    #
    # Draw the unit circle in heavy black.
    #
    printf $plot ("plot (cos(t)),(sin(t)) notitle lc rgb 'black' lt 2, \\\n");

    #
    # Draw circles of constant resistance.
    #
    my @RValues = (-5.0, -2.0, -1.0, -0.5, -0.2, 0.0, 0.2, 0.5, 1.0, 2.0, 5.0);
    foreach my $R (@RValues) {
	#
	# Suppress the R == -1 (vertical line) assuming that it coincides
	# with a grid line.
	#
	if ($R == -1.0) {
	    next;
	}

	#
	# Special-case R == 0, which is the unit circle we drew above.
	#
	if ($R == 0.0) {
	    next;
	}

	#
	# Calculate center and radius of circle.
	#
	my $x0 = $R / ($R + 1);
	my $r  = 1.0 / sqrt(1.0 + 2.0 * $R + $R * $R);	# radius

	#
	# Hide negative resistance if the top or bottom point of the
	# circle lies outside the bounds.
	#
	if ($R < 0.0) {
	    if ($R > -1.0) {
		if ($x0 - $r < $xmin) {
		    next;
		}
	    } else {
		if ($x0 + $r > $xmax) {
		    next;
		}
	    }
	    if ($r > $ymax && -$r < $ymin) {
		next;
	    }
	}
	printf $plot ("    (%g+%g*cos(t)),(%g*sin(t)) %s, \\\n",
	    $x0, $r, $r, $SGRID_OPTS);
    }

    #
    # Draw circles of constant reactance.
    #
    my @XValues = (-5.0, -2.0, -1.0, -0.5, -0.2, 0.0, 0.2, 0.5, 1.0, 2.0, 5.0);
    foreach my $X (@XValues) {
	#
	# Suppress the X == 0 (horizontal line), assuming it coincides
	# with a grid line.
	#
	if ($X == 0) {
	    next;
	}

	#
	# Calculate center and radius of circle.
	#
	my $x0 = 1.0;
	my $y0 = 1.0 / $X;
	my $r  = abs($y0);
	printf $plot ("    (%g+%g*cos(t)),(%g+%g*sin(t)) %s, \\\n",
	    $x0, $r, $y0, $r, $SGRID_OPTS);
    }
}

#
# m_plot: plot the graph
#
sub m_plot {
    my $cur = \%CurrentSettings;

    my $conversions = $cur->{m_conversions};
    my $parameter = $cur->{m_parameter};
    my $datafile;
    my $plotfile;
    my $svgfile;
    my $width = $cur->{m_graph_width};
    my $height = $cur->{m_graph_height};
    my $ranges = "";

    my $m_title   = $builder->get_object("m_title");
    my $m_graph   = $builder->get_object("m_graph");
    my $m_x_min   = $builder->get_object("m_x_min");
    my $m_x_max   = $builder->get_object("m_x_max");
    my $m_y_min   = $builder->get_object("m_y_min");
    my $m_y_max   = $builder->get_object("m_y_max");
    my $m_legend  = $builder->get_object("m_legend");

    #
    # If conversions isn't defined, instantiate one for the temporary
    # directory.
    #
    if (!defined($conversions)) {
	$conversions = N2PKVNAConvert->new();
	$cur->{m_conversions} = $conversions;
    }

    #
    # Convert parameters.
    #
    $datafile = $conversions->convert($parameter, \&run_command_dialog);
    $plotfile = $conversions->getdir() . "/plot";
    $svgfile = $conversions->getdir() . "/plot.svg";

    #
    # Get ranges and units.
    #
    my ($x_ranges, $x_base_unit) = &parameter_to_x_ranges($parameter);
    my ($y_ranges, $y_base_unit) = &parameter_to_y_ranges($parameter);
    my ($y2_ranges, $y2_base_unit) = &parameter_to_y2_ranges($parameter);

    #
    # If Smith chart and ranges are not defined, set them.
    #
    my $smith_ranges = $cur->{smith_ranges};
    if ($parameter eq "smith") {
	#
	# If smith_ranges not yet known, find them.
	#
	if (!defined($smith_ranges) && defined($datafile)) {
	    $cur->{smith_ranges} = $smith_ranges = {
		xmin => -1.0,
		xmax => +1.0,
		ymin => -1.0,
		ymax => +1.0,
	    };
	    open(my $data_fd, "<${datafile}") || die "${datafile}: $!";
	    while (<$data_fd>) {
		chop;
		if (/^#/) {
		    next;
		}
		my @F = split;
		if ($conversions->{ports} == 1) {
		    die "$_" unless scalar(@F) == 3;
		} else {
		    die "$_" unless scalar(@F) == 9;
		}
		for (my $i = 0; $i < $conversions->{ports}; ++$i) {
		    my $x_idx = $conversions->{ports} * $i + $i + 1;
		    my $y_idx = $x_idx + 1;
		    die "$_" unless $#F >= $y_idx;
		    my $x = $F[$x_idx];
		    my $y = $F[$y_idx];
		    if ($x < $smith_ranges->{xmin}) {
			$smith_ranges->{xmin} = $x;
		    }
		    if ($x > $smith_ranges->{xmax}) {
			$smith_ranges->{xmax} = $x;
		    }
		    if ($y < $smith_ranges->{ymin}) {
			$smith_ranges->{ymin} = $y;
		    }
		    if ($y > $smith_ranges->{ymax}) {
			$smith_ranges->{ymax} = $y;
		    }
		}
	    }
	    close($data_fd);
	    $x_ranges->[0] = "";	# clear to force reset below
	    $x_ranges->[1] = "";
	    $y_ranges->[0] = "";
	    $y_ranges->[1] = "";
	}
    }

    #
    # Break out the ranges and units.
    #
    my ($x_min, $x_max, $x_unit, $x_logscale) = @{$x_ranges};
    my ($y_min, $y_max, $y_unit) = @{$y_ranges};
    my ($y2_min, $y2_max, $y2_unit) = ("", "", undef);
    if (defined($y2_ranges)) {
	($y2_min, $y2_max, $y2_unit) = @{$y2_ranges};
    }
    my $x_scale = defined($x_unit) ? $UnitToScale{$x_unit} : 1.0;
    my $y_scale = defined($y_unit) ? $UnitToScale{$y_unit} : 1.0;
    my $y2_scale = defined($y2_unit) ? $UnitToScale{$y2_unit} : 1.0;

    #
    # If Smith, fill in $x_min, $x_max, $y_min, $y_max, if not specified.
    #
    if ($parameter eq "smith" && defined($smith_ranges)) {
	if ($x_min eq "") {
	    $x_min = $smith_ranges->{xmin};
	}
	if ($x_max eq "") {
	    $x_max = $smith_ranges->{xmax};
	}
	if ($y_min eq "") {
	    $y_min = $smith_ranges->{ymin};
	}
	if ($y_max eq "") {
	    $y_max = $smith_ranges->{ymax};
	}
    }

    #
    # Open plot file, set terminal and set global plot options.
    #
    open(PLOT, ">${plotfile}") || die "${plotfile}: $!";
    printf PLOT ("reset\n");
    printf PLOT ("set term svg size %d,%d\n", $width, $height);
    printf PLOT ("set output '%s'\n", $svgfile);
    printf PLOT ("set grid\n");
    printf PLOT ("set style data lines\n");

    #
    # Create the rest of the plot script.
    #
    if (defined($datafile)) {
	#
	# Set title.
	#
	my $title = $m_title->get_text();
	if (length($title) > 0) {
	    printf PLOT ("set title \"%s\"\n", $title);
	}

	#
	# If not Smith...
	#
	if ($parameter ne "smith") {
	    #
	    # Set xlabel and x ranges.
	    #
	    if (defined($BaseUnitNames{$x_base_unit})) {
		printf PLOT ("set xlabel '%s", $BaseUnitNames{$x_base_unit});
		if (defined($x_unit)) {
		    printf PLOT (" (%s)", $x_unit);
		}
		printf PLOT ("'\n");
	    }
	    if ($x_logscale) {
		printf PLOT ("set logscale x\n");
	    }
	    $ranges .= " [$x_min:$x_max]";

	    #
	    # Set ylabel and y ranges.
	    #
	    if (defined($BaseUnitNames{$y_base_unit})) {
		printf PLOT ("set ylabel '%s", $BaseUnitNames{$y_base_unit});
		if (defined($y_unit)) {
		    printf PLOT (" (%s)", $y_unit);
		}
		printf PLOT ("'\n");
	    }
	    $ranges .= " [$y_min:$y_max]";

	    #
	    # Set y2label and range if y2 exists.
	    #
	    if (defined($y2_base_unit)) {
		if (defined($BaseUnitNames{$y2_base_unit})) {
		    printf PLOT ("set y2label '%s",
			    $BaseUnitNames{$y2_base_unit});
		    if (defined($y2_unit)) {
			printf PLOT (" (%s)'", $y2_unit);
		    }
		    printf PLOT ("'\n");
		}
		printf PLOT ("set y2tics\n");
		if ($y2_min ne "" || $y2_max ne "") {
		    printf PLOT ("set y2range [%s:%s]\n", $y2_min, $y2_max);
		}
	    }
	}

	#
	# Plot the points.
	#
	printf PLOT ("set key %s\n", $m_legend->get_active_id());
	my $key = $conversions->{ports} . $parameter;

	if ($key =~ m/^1([syz])ri/) {
	    my $prefix = $1;

	    printf PLOT ("plot %s '%s' using " .
		    "(\$1/${x_scale}):(\$2/${y_scale}) title '%s11_r', \\\n",
		    $ranges, $datafile, $prefix);
	    printf PLOT ("    '' using (\$1/${x_scale}):(\$3/${y_scale}) title '%s11_i'\n", $prefix);

	} elsif ($key =~ m/^2([stuzyhgab])ri/) {
	    my $prefix = $1;

	    printf PLOT ("plot %s '%s' " .
		    "using (\$1/${x_scale}):(\$2/${y_scale}) title '%s11_r' lt 1 dt solid, \\\n",
		    $ranges, $datafile, $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$3/${y_scale}) title '%s11_i' lt 1 dt 2, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$4/${y_scale}) title '%s12_r' lt 2 dt solid, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$5/${y_scale}) title '%s12_i' lt 2 dt 2, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$6/${y_scale}) title '%s21_r' lt 3 dt solid, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$7/${y_scale}) title '%s21_i' lt 3 dt 2, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$8/${y_scale}) title '%s22_r' lt 4 dt solid, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$9/${y_scale}) title '%s22_i' lt 4 dt 2\n", $prefix);

	} elsif ($key =~ m/^1([syz])(ma|db)/) {
	    my $prefix = $1;

	    printf PLOT ("plot %s '%s' using (\$1/${x_scale}):(\$2/${y_scale}) title '%s11_m', \\\n",
		    $ranges, $datafile, $prefix);
	    printf PLOT ("    '' using (\$1/${x_scale}):(\$3/${y2_scale}) axes x1y2 title '%s11_a'\n", $prefix);

	} elsif ($key =~ m/^2([stuzyhgab])(ma|db)/) {
	    my $prefix = $1;

	    printf PLOT ("plot %s '%s' " .
		    "using (\$1/${x_scale}):(\$2/${y_scale}) title '%s11_m' lt 1 dt solid, \\\n",
		    $ranges, $datafile, $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$3/${y2_scale}) axes x1y2 title '%s11_a' lt 1 dt 2, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$4/${y_scale}) title '%s12_m' lt 2 dt solid, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$5/${y2_scale}) axes x1y2 title '%s12_a' lt 2 dt 2, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$6/${y_scale}) title '%s21_m' lt 3 dt solid, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$7/${y2_scale}) axes x1y2 title '%s21_a' lt 3 dt 2, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$8/${y_scale}) title '%s22_m' lt 4 dt solid, \\\n", $prefix);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$9/${y2_scale}) axes x1y2 title '%s22_a' lt 4 dt 2\n", $prefix);

	} elsif ($key eq "1smith" || $key eq "2smith") {
	    &smith_grid(\*PLOT, $x_min, $x_max, $y_min, $y_max);
	    printf PLOT ("    '%s' using 2:3 title 'S11' " .
			 "with points lt 1 pt 7 ps 0.5",
		    $datafile);
	    if ($conversions->{ports} == 2) {
		printf PLOT (", \\\n");
		printf PLOT ("    '' using 8:9 title 'S22' " .
			     "with points lt 2 pt 7 ps 0.5");
	    }
	    printf PLOT ("\n");

	} elsif ($key =~ m/^1zinri/) {
	    printf PLOT ("plot %s '%s' using (\$1/${x_scale}):(\$2/${y_scale}) title 'Zin_r', \\\n",
		    $ranges, $datafile);
	    printf PLOT ("    '' using (\$1/${x_scale}):(\$3/${y_scale}) title 'Zin_i'\n");

	} elsif ($key =~ m/^2zinri/) {
	    printf PLOT ("plot %s '%s' " .
		    "using (\$1/${x_scale}):(\$2/${y_scale}) title 'Zin1_r' lt 1 dt solid, \\\n",
		    $ranges, $datafile);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$3/${y_scale}) title 'Zin1_i' lt 1 dt 2, \\\n");
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$4/${y_scale}) title 'Zin2_r' lt 2 dt solid, \\\n");
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$5/${y_scale}) title 'Zin2_i' lt 2 dt 2\n");

	} elsif ($key =~ m/^1zinma/) {
	    printf PLOT ("plot %s '%s' using (\$1/${x_scale}):(\$2/${y_scale}) title 'Zin_m', \\\n",
		    $ranges, $datafile);
	    printf PLOT ("    '' using (\$1/${x_scale}):(\$3/${y2_scale}) axes x1y2 title 'Zin_a'\n");

	} elsif ($key =~ m/^2zinma/) {
	    printf PLOT ("plot %s '%s' " .
		    "using (\$1/${x_scale}):(\$2/${y_scale}) title 'Zin1_m' lt 1 dt solid, \\\n",
		    $ranges, $datafile);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$3/${y2_scale}) axes x1y2 title 'Zin1_a' lt 1 dt 2, \\\n");
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$4/${y_scale}) title 'Zin2_m' lt 2 dt solid, \\\n");
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$5/${y2_scale}) axes x1y2 title 'Zin2_a' lt 2 dt 2\n");

	} elsif ($key =~ m/^1[ps]rc/) {
	    printf PLOT ("plot %s '%s' using (\$1/${x_scale}):(\$2/${y_scale}) title 'R', \\\n",
		    $ranges, $datafile);
	    printf PLOT ("    '' using (\$1/${x_scale}):(\$3/${y2_scale}) axes x1y2 title 'C'\n");

	} elsif ($key =~ m/^2[ps]rc/) {
	    printf PLOT ("plot %s '%s' " .
		    "using (\$1/${x_scale}):(\$2/${y_scale}) title 'R1' lt 1 dt solid, \\\n",
		    $ranges, $datafile);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$3/${y2_scale}) axes x1y2 title 'C1' lt 1 dt 2, \\\n");
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$4/${y_scale}) title 'R2' lt 2 dt solid, \\\n");
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$5/${y2_scale}) axes x1y2 title 'C2' lt 2 dt 2\n");

	} elsif ($key =~ m/^1[ps]rl/) {
	    printf PLOT ("plot %s '%s' using (\$1/${x_scale}):(\$2/${y_scale}) title 'R', \\\n",
		    $ranges, $datafile);
	    printf PLOT ("    '' using (\$1/${x_scale}):(\$3/${y2_scale}) axes x1y2 title 'L'\n");

	} elsif ($key =~ m/^2[ps]rl/) {
	    printf PLOT ("plot %s '%s' " .
		    "using (\$1/${x_scale}):(\$2/${y_scale}) title 'R1' lt 1 dt solid, \\\n",
		    $ranges, $datafile);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$3/${y2_scale}) axes x1y2 title 'L1' lt 1 dt 2, \\\n");
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$4/${y_scale}) title 'R2' lt 2 dt solid, \\\n");
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$5/${y2_scale}) axes x1y2 title 'L2' lt 2 dt 2\n");

	} elsif ($key eq "1rl" || $key eq "1vswr") {
	    printf PLOT ("plot %s '%s' using (\$1/${x_scale}):(\$2/${y_scale}) notitle\n",
		    $ranges, $datafile);

	} elsif ($key eq "2rl") {
	    printf PLOT ("plot %s '%s' " .
		    "using (\$1/${x_scale}):(\$2/${y_scale}) title 'RL1', \\\n",
		    $ranges, $datafile);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$3/${y_scale}) title 'RL2'\n");

	} elsif ($key eq "2il") {
	    printf PLOT ("plot %s '%s' " .
		    "using (\$1/${x_scale}):(\$2/${y_scale}) title 'IL12', \\\n",
		    $ranges, $datafile);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$3/${y_scale}) title 'IL21'\n");

	} elsif ($key eq "2vswr") {
	    printf PLOT ("plot %s '%s' " .
		    "using (\$1/${x_scale}):(\$2/${y_scale}) title 'VSWR1', \\\n",
		    $ranges, $datafile);
	    printf PLOT ("    '' " .
		    "using (\$1/${x_scale}):(\$3/${y_scale}) title 'VSWR2'\n");

	} else {
	    die "$key";
	}

    } else {
	#
	# Handle the null plot.
	#
	if (!defined($conversions->{ports})) {
	    printf PLOT ("set title 'No Data'\n");
	} else {
	    printf PLOT ("set title 'Invalid Conversion'\n");
	}
	printf PLOT ("set xlabel 'Frequency (MHz)'\n");
	printf PLOT ("plot [x=0:60] [-1:1] (NaN) notitle\n");
    }
    close(PLOT);
    system("gnuplot ${plotfile}");
    $cur->{m_pixbuf} = Gtk3::Gdk::Pixbuf->new_from_file($svgfile);
    $m_graph->queue_draw();
    $m_graph->show();
    return 0;
}

#
# gen_command: send the generate command to the VNA
#
sub gen_command {
    my $cur = \%CurrentSettings;
    my $rf_frequency = 0.0;
    my $lo_frequency = 0.0;
    my $phase = 0.0;

    if ($cur->{gen_master_enable}) {
	if ($cur->{gen_RF_enable}) {
	    $rf_frequency = $cur->{gen_RF_frequency};
	}
	if ($cur->{gen_LO_enable}) {
	    $lo_frequency = $cur->{gen_LO_frequency};
	    $phase = $cur->{gen_phase};
	}
    }
    if (!&run_command_dialog([ "generate",
		$rf_frequency / 1.0e+6,
		$lo_frequency / 1.0e+6, $phase ], undef, undef, undef, undef)) {
	$cur->{gen_master_enable} = 0;
	my $gen_master_enable = $builder->get_object("gen_master_enable");
	$gen_master_enable->set_active(0);
    }
}

#
# update_gen_display: update the values on the cur page from current values
#
sub update_gen_display {
    my $cur = \%CurrentSettings;
    my $gen_display		= $builder->get_object("gen_display");
    my $gen_RF_frequency	= $builder->get_object("gen_RF_frequency");
    my $gen_LO_frequency	= $builder->get_object("gen_LO_frequency");
    my $gen_RF_unit		= $builder->get_object("gen_RF_unit");
    my $gen_LO_unit		= $builder->get_object("gen_LO_unit");
    my $gen_RF_enable		= $builder->get_object("gen_RF_enable");
    my $gen_LO_enable		= $builder->get_object("gen_LO_enable");
    my $gen_phase		= $builder->get_object("gen_phase");
    my $gen_lock_to_RF		= $builder->get_object("gen_lock_to_RF");
    my $gen_master_enable	= $builder->get_object("gen_master_enable");
    my $gen_attenuation		= $builder->get_object("gen_attenuation");
    my $text;

    #
    # Set RF frequency
    #
    if ($cur->{gen_RF_unit_index} == 2) {		# MHz
	$text = sprintf("%9.6f", $cur->{gen_RF_frequency} / 1.0e+6);
    } elsif ($cur->{gen_RF_unit_index} == 1) {	# kHz
	$text = sprintf("%9.3f", $cur->{gen_RF_frequency} / 1.0e+3);
    } else {
	$text = sprintf("%9.0f", $cur->{gen_RF_frequency});
    }

    #
    # Set LO frequency
    #
    $gen_RF_frequency->set_text($text);
    if ($cur->{gen_LO_unit_index} == 2) {		# MHz
	$text = sprintf("%9.6f", $cur->{gen_LO_frequency} / 1.0e+6);
    } elsif ($cur->{gen_LO_unit_index} == 1) {	# kHz
	$text = sprintf("%9.3f", $cur->{gen_LO_frequency} / 1.0e+3);
    } else {
	$text = sprintf("%9.0f", $cur->{gen_LO_frequency});
    }
    $gen_LO_frequency->set_text($text);

    #
    # Set gen_phase
    #
    $gen_phase->set_text(sprintf("%5.1f", $cur->{gen_phase}));

    #
    # Set units and buttons.
    #
    $gen_RF_unit->set_text($HzIndexToName[$cur->{gen_RF_unit_index}]);
    $gen_LO_unit->set_text($HzIndexToName[$cur->{gen_LO_unit_index}]);
    $gen_RF_enable->set_active($cur->{gen_RF_enable});
    $gen_LO_enable->set_active($cur->{gen_LO_enable});
    $gen_lock_to_RF->set_active($cur->{gen_lock_to_RF});
    $gen_lock_to_RF->set_active($cur->{gen_lock_to_RF});
    $gen_master_enable->set_active($cur->{gen_master_enable});
    $gen_attenuation->set_active($cur->{attenuation});

    $gen_display->show_all();
}

#
# gen_set_lock_to_RF_toggled_cb: handle changes to lock button in dialog
#
sub gen_set_lock_to_RF_toggled_cb {
    my ($widget, $data) = @_;

    my $gen_set_RF_unit      = $builder->get_object("gen_set_RF_unit");
    my $gen_set_LO_frequency = $builder->get_object("gen_set_LO_frequency");
    my $gen_set_LO_unit      = $builder->get_object("gen_set_LO_unit");

    if ($widget->get_active()) {
	$gen_set_LO_frequency->set_sensitive(0);
	$gen_set_LO_frequency->set_text("");
	$gen_set_LO_frequency->show();
	$gen_set_LO_unit->set_sensitive(0);
	$gen_set_LO_unit->set_active($gen_set_RF_unit->get_active());
	$gen_set_LO_unit->show();
    } else {
	$gen_set_LO_frequency->set_sensitive(1);
	$gen_set_LO_unit->set_sensitive(1);
	$gen_set_LO_frequency->show();
    }
}

#
# gen_edit_clicked_cb: bring up the cur edit dialog
#
sub gen_edit_clicked_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;
    my $rv;

    my $gen_edit_dialog	     = $builder->get_object("gen_edit_dialog");
    my $gen_set_RF_frequency = $builder->get_object("gen_set_RF_frequency");
    my $gen_set_LO_frequency = $builder->get_object("gen_set_LO_frequency");
    my $gen_set_RF_unit      = $builder->get_object("gen_set_RF_unit");
    my $gen_set_LO_unit      = $builder->get_object("gen_set_LO_unit");
    my $gen_set_RF_enable    = $builder->get_object("gen_set_RF_enable");
    my $gen_set_LO_enable    = $builder->get_object("gen_set_LO_enable");
    my $gen_set_phase        = $builder->get_object("gen_set_phase");
    my $gen_set_lock_to_RF   = $builder->get_object("gen_set_lock_to_RF");

    #
    # Init the edit dialog entries so that the combobox and checkbox
    # entries reflect the current values and the entry boxes are empty.
    #
    $gen_set_RF_frequency->set_text("");
    $gen_set_LO_frequency->set_text("");
    $gen_set_RF_unit->set_active($cur->{gen_RF_unit_index});
    $gen_set_LO_unit->set_active($cur->{gen_LO_unit_index});
    $gen_set_RF_enable->set_active($cur->{gen_RF_enable});
    $gen_set_LO_enable->set_active($cur->{gen_LO_enable});
    $gen_set_phase->set_text("");
    $gen_set_lock_to_RF->set_active($cur->{gen_lock_to_RF});
    if ($cur->{gen_lock_to_RF}) {
	$gen_set_LO_frequency->set_sensitive(0);
	$gen_set_LO_unit->set_sensitive(0);
    } else {
	$gen_set_LO_frequency->set_sensitive(1);
	$gen_set_LO_unit->set_sensitive(1);
    }

    #
    # Run the dialog until we have valid answers or a canceled.
    #
    $gen_edit_dialog->show_all();
    while (($rv = $gen_edit_dialog->run()) eq 'apply') {
	my $gen_RF_frequency = $gen_set_RF_frequency->get_text();
	my $gen_LO_frequency = $gen_set_LO_frequency->get_text();
	my $RF_unit          = $gen_set_RF_unit->get_active();
	my $LO_unit          = $gen_set_LO_unit->get_active();
	my $gen_RF_enable    = $gen_set_RF_enable->get_active();
	my $gen_LO_enable    = $gen_set_LO_enable->get_active();
	my $gen_phase        = $gen_set_phase->get_text();
	my $gen_lock_to_RF   = $gen_set_lock_to_RF->get_active();

	#
	# Validate the entries.
	#
	if ($gen_RF_frequency ne "") {
	    $gen_RF_frequency *= $HzIndexToMultiplier[$RF_unit];

	    if ($gen_RF_frequency <= 0.0 || $gen_RF_frequency > MaxFrequency) {
		my $error_dialog =
		Gtk3::MessageDialog->new($appwindow, 'destroy-with-parent',
			'error', 'close',
			'RF frequency value is out of range.');
		$error_dialog->run();
		$error_dialog->destroy();
		next;
	    }
	}
	if (!$gen_lock_to_RF && $gen_LO_frequency ne "") {
	    $gen_LO_frequency *= $HzIndexToMultiplier[$LO_unit];

	    if ($gen_LO_frequency <= 0.0 || $gen_LO_frequency > MaxFrequency) {
		my $error_dialog =
		Gtk3::MessageDialog->new($appwindow, 'destroy-with-parent',
			'error', 'close',
			'LO frequency value is out of range.');
		$error_dialog->run();
		$error_dialog->destroy();
		next;
	    }
	}

	#
	# Copy changes to current.
	#
	if ($gen_RF_frequency ne "") {
	    $cur->{gen_RF_frequency} = $gen_RF_frequency;
	}
	$cur->{gen_RF_unit_index} = $RF_unit;
	$cur->{gen_RF_enable}     = $gen_RF_enable;
	$cur->{gen_LO_enable}     = $gen_LO_enable;
	$cur->{gen_lock_to_RF}    = $gen_lock_to_RF;
	if ($gen_lock_to_RF) {
	    $cur->{gen_LO_frequency}  = $cur->{gen_RF_frequency};
	    $cur->{gen_LO_unit_index} = $cur->{gen_RF_unit_index};
	} elsif ($gen_LO_frequency ne "") {
	    $cur->{gen_LO_frequency}  = $gen_LO_frequency;
	    $cur->{gen_LO_unit_index} = $LO_unit;
	}
	if ($gen_phase ne "") {
	    $cur->{gen_phase} = $gen_phase;
	}
	&update_gen_display();
	if ($cur->{gen_master_enable}) {
	    &gen_command();
	}
	last;
    }
    $gen_edit_dialog->hide();
}

#
# gen_master_enable_toggled_cb: enable/disable RF generator
#
sub gen_master_enable_toggled_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;

    my $active = $widget->get_active();
    if ($active != $cur->{gen_master_enable}) {
	$cur->{gen_master_enable} = $active;
	&gen_command();
    }
}

#
# gen_attenuation_changed_cb: handle attenuation change on gen page
#
sub gen_attenuation_changed_cb {
    my ($widget, $data) = @_;
    my $cur = \%CurrentSettings;

    $cur->{attenuation} = $widget->get_active();
    &attenuate_command();
}

#
# stack1_notify_cb: handle screen change
#
sub stack1_notify_visible_child_name_cb {
    my ($widget, $pspec) = @_;
    my $cur = \%CurrentSettings;

    my $newMode = $widget->get_visible_child_name();
    if ($cur->{page} eq "generate") {
	if ($cur->{gen_master_enable}) {
	    $cur->{gen_master_enable} = 0;
	    my $gen_master_enable = $builder->get_object("gen_master_enable");
	    $gen_master_enable->set_active(0);
	    &gen_command();
	}
    }
    $cur->{page} = $newMode;
}

sub cal_build_setup {
    my $cur = \%CurrentSettings;
    my $setups = $cur->{properties}{setups};
    my $refcount = RefCount->new(\$cur->{busy}{cal_main});
    my @setup_names = keys(%{$setups});
    my $cal_setup = $builder->get_object("cal_setup");

    $refcount->hold();
    $cal_setup->remove_all();
    if (scalar(@setup_names) == 0) {
	$cal_setup->append(undef, "VNA Set-up Required");
    } else {
	$cal_setup->append(undef, "Select Setup");
	foreach my $name (@setup_names) {
	    $cal_setup->append($name, $name);
	}
    }
    $cal_setup->set_active(0);
    $cal_setup->show();
    &cal_build_type();
    $refcount->release();
}

sub cal_setup_changed_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;
    my $refcount = RefCount->new(\$cur->{busy}{cal_main});
    if ($refcount->check_hold()) {
	return;
    }
    $cur->{cal_setup} = $widget->get_active_id();
    &cal_build_type();
}

sub cal_build_type {
    my $cur = \%CurrentSettings;
    my $setup_name = $cur->{cal_setup};
    my $cal_type      = $builder->get_object("cal_type");
    my $cal_fmin      = $builder->get_object("cal_fmin");
    my $cal_fmax      = $builder->get_object("cal_fmax");
    my $cal_fmin_unit = $builder->get_object("cal_fmin_unit");
    my $cal_fmax_unit = $builder->get_object("cal_fmax_unit");
    my $cal_steps     = $builder->get_object("cal_steps");
    my $cal_log       = $builder->get_object("cal_log");
    my $old_type = $cur->{cal_type};
    my $setup_ref;
    my $rows;
    my $columns;
    if (defined($setup_name)) {
	$setup_ref = $cur->{properties}{setups}{$setup_name};
	my $dimensions = $setup_ref->{dimensions};
	if ($dimensions =~ m/^(\d)x(\d)$/) {
	    $rows    = $1;
	    $columns = $2;
	}
    }
    my %allowed;
    # ZZ: Suppress cal_type_changed_cb callbacks while changing?
    $cal_type->remove_all();
    if (defined($rows) && defined($columns)) {
	if ($rows <= $columns) {
	    $cal_type->append("T8", "T8");
	    $allowed{"T8"} = 1;
	    if ($columns == 2) {
		$cal_type->append("TE10", "TE10");
		$allowed{"TE10"} = 1;
		$cal_type->append("T16", "T16");
		$allowed{"T16"} = 1;
	    }
	}
	if ($rows >= $columns) {
	    $cal_type->append("U8", "U8");
	    $allowed{"U8"} = 1;
	    if ($rows == 2) {
		$cal_type->append("UE10", "UE10");
		$allowed{"UE10"} = 1;
		$cal_type->append("U16", "U16");
		$allowed{"U16"} = 1;
	    }
	    $cal_type->append("E12", "E12");
	    $allowed{"E12"} = 1;
	}
	if (defined($old_type) && defined($allowed{$old_type})) {
	    $cal_type->set_active_id($old_type);
	} elsif ($rows >= $columns) {
	    $cal_type->set_active_id("E12");
	    $cur->{cal_type} = "E12";
	} elsif ($columns == 2) {
	    $cal_type->set_active_id("TE10");
	    $cur->{cal_type} = "TE10";
	} else {
	    $cal_type->set_active_id("T8");
	    $cur->{cal_type} = "T8";
	}
    }
    $cal_type->show();

    #
    # Init sweep fields.
    #
    if (defined($setup_ref)) {
	my $fmin = $setup_ref->{fmin};
	my $fmax = $setup_ref->{fmax};
	my $min_unit = 0;
	my $max_unit = 0;

	if (!defined($fmin)) {
	    $fmin = 50.e+3;
	}
	if (!defined($fmax)) {
	    $fmax = 60.e+6;
	}
	$cal_log->set_active($fmax / $fmin >= 100);
	$cal_log->show();
	($fmin, $min_unit) = &normalize_Hz($fmin);
	($fmax, $max_unit) = &normalize_Hz($fmax);
	$cal_fmin->set_text($fmin);
	$cal_fmin->show();
	$cal_fmin_unit->set_active($min_unit);
	$cal_fmin_unit->show();
	$cal_fmax->set_text($fmax);
	$cal_fmax->show();
	$cal_fmax_unit->set_active($max_unit);
	$cal_fmax_unit->show();
	$cal_steps->set_text(50);
	$cal_steps->show();
    }
    &cal_update_standards();
}

sub cal_find_default_standards {
    my $cur = \%CurrentSettings;
    my $setup_name = $cur->{cal_setup};
    my $type       = $cur->{cal_type};

    if (!defined($setup_name) || !defined($type)) {
	return "";
    }
    my $setup_ref = $cur->{properties}{setups}{$setup_name};
    my $dimensions = $setup_ref->{dimensions};
    if (!($dimensions =~ m/^(\d)x(\d)$/)) {
	return "";
    }
    my $rows    = $1;
    my $columns = $2;
    if ($rows == 1 && $columns == 1) {
	return CalSingle;
    }
    if (($rows == 1 && $columns == 2) || ($rows == 2 && $columns == 1)) {
	if ($type eq "T16" || $type eq "U16") {
	    return CalHalf16;
	}
	return CalHalf8;
    }
    if ($rows == 2 && $columns == 2) {
	if ($type eq "T16" || $type eq "U16") {
	    return CalFull16;
	}
	if ($type eq "E12") {
	    return CalFull12;
	}
	return CalFull8;
    }
    return "";
}

sub cal_update_standards {
    my $cur = \%CurrentSettings;
    my $old_defaults = $cur->{cal_defaults};
    my $new_defaults = &cal_find_default_standards();

    if ($new_defaults ne $old_defaults) {
	&cal_set_standards($new_defaults);
	&cal_build_standards();
    }
}

sub cal_type_changed_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;

    $cur->{cal_type} = $widget->get_active_id();
    &cal_update_standards();
}

sub cal_build_std_combobox {
    my $standards = shift;
    my $cur = \%CurrentSettings;

    my $combobox = Gtk3::ComboBoxText->new();
    foreach my $standard (@{$standards}) {
	my $name = $standard->{name};
	my $text = $standard->{text};

	if (!defined($name)) {
	    $name = "";
	}
	if (!defined($text)) {
	    $text = $name;
	}
	$combobox->append($name, $text);
    }
    return $combobox;
}

sub cal_stdcb_changed_cb {
    my $widget = shift;
    my $arg    = shift;

    $$arg = $widget->get_active_id();
}

sub cal_delete_clicked_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;

    splice(@{$cur->{cal_standards}{list}}, $arg, 1);
    &cal_build_standards();
}

sub cal_add_single_reflect_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;

    push(@{$cur->{cal_standards}{list}}, "S");
    &cal_build_standards();
}

sub cal_add_double_reflect_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;

    push(@{$cur->{cal_standards}{list}}, [ "S", "" ]);
    &cal_build_standards();
}

sub cal_add_2x2_cb {
    my $widget = shift;
    my $arg    = shift;
    my $cur = \%CurrentSettings;

    push(@{$cur->{cal_standards}{list}}, "T");
    &cal_build_standards();
}

sub cal_set_standards {
    my $string = shift;
    my $cur = \%CurrentSettings;
    my $ports = ($string eq CalSingle) ? 1 : 2;

    my @list;
    if ($ports == 1) {
	@list = split(/,/, $string);
    } else {
	foreach my $item (split(/,/, $string)) {
	    if ($item =~ m/-/) {
		push(@list, [ split(/-/, $item, -1) ]);
	    } else {
		push(@list, $item);
	    }
	}
    }
    $cur->{cal_standards} = {
	ports => $ports,
	list  => \@list
    };
}

sub cal_get_standards {
    my $cur = \%CurrentSettings;
    my $standards = $cur->{cal_standards};
    my $ports = $standards->{ports};
    my $list  = $standards->{list};

    if ($ports == 1) {
	return join(',', @{$list});
    }
    return join(',', map {
	if (ref($_) eq "ARRAY") {
	    join('-', @{$_});
	} else {
	    $_;
	}
    } @{$list});
}

sub cal_build_standards {
    my $cur = \%CurrentSettings;
    my $standards = $cur->{cal_standards};
    my $ports = $standards->{ports};
    my $list  = $standards->{list};
    my $cal_standards = $builder->get_object("cal_standards");

    #
    # Clear table.
    #
    while ($cal_standards->get_child_at(0, 0)) {
	$cal_standards->remove_row(0);
    }

    #
    # Handle reflection only case.
    #
    if ($ports == 1) {
	$cal_standards->attach(Gtk3::Label->new("Step"),     0, 0, 1, 1);
	$cal_standards->attach(Gtk3::Label->new("Standard"), 1, 0, 1, 1);
	$cal_standards->attach(Gtk3::Separator->new("horizontal"), 0, 1, 1, 1);
	$cal_standards->attach(Gtk3::Separator->new("horizontal"), 1, 1, 1, 1);
	for (my $i = 0; $i < scalar(@{$list}); ++$i) {
	    my $label = Gtk3::Label->new($i + 1);
	    my $combobox = &cal_build_std_combobox($cur->{standards_1port});
	    die unless defined($combobox);
	    $combobox->set_active_id($list->[$i]);
	    $combobox->signal_connect(changed => \&cal_stdcb_changed_cb,
		    \$list->[$i]);
	    my $delete = Gtk3::Button->new_from_stock("gtk-delete");
	    $delete->set_always_show_image(1);
	    $delete->signal_connect(clicked => \&cal_delete_clicked_cb, $i);
	    $cal_standards->attach($label,    0, 2 + $i, 1, 1);
	    $cal_standards->attach($combobox, 1, 2 + $i, 1, 1);
	    $cal_standards->attach($delete,   2, 2 + $i, 1, 1);
	}
	my $add = Gtk3::Button->new_from_stock("gtk-add");
	$add->set_always_show_image(1);
	$add->signal_connect(clicked => \&cal_add_single_reflect_cb);
	$cal_standards->attach($add, 0, 2 + scalar(@{$list}), 1, 3);

    } else {
	$cal_standards->attach(Gtk3::Label->new("Step"),   0, 0, 1, 1);
	$cal_standards->attach(Gtk3::Label->new("Port 1"), 1, 0, 1, 1);
	$cal_standards->attach(Gtk3::Label->new("Port 2"), 3, 0, 1, 1);
	$cal_standards->attach(Gtk3::Separator->new("horizontal"), 0, 1, 1, 1);
	$cal_standards->attach(Gtk3::Separator->new("horizontal"), 1, 1, 1, 1);
	$cal_standards->attach(Gtk3::Separator->new("horizontal"), 3, 1, 1, 1);
	for (my $i = 0; $i < scalar(@{$list}); ++$i) {
	    my $label = Gtk3::Label->new($i + 1);
	    $cal_standards->attach($label,    0, 2 + $i, 1, 1);
	    if (ref($list->[$i]) eq "ARRAY") {
		my $cb1 = &cal_build_std_combobox($cur->{standards_1port});
		$cb1->set_active_id($list->[$i][0]);
		$cb1->signal_connect(changed => \&cal_stdcb_changed_cb,
			\$list->[$i][0]);
		my $dash = Gtk3::Label->new("-");
		my $cb2 = &cal_build_std_combobox($cur->{standards_1port});
		$cb2->set_active_id($list->[$i][1]);
		$cb2->signal_connect(changed => \&cal_stdcb_changed_cb,
			\$list->[$i][1]);
		$cal_standards->attach($cb1,  1, 2 + $i, 1, 1);
		$cal_standards->attach($dash, 2, 2 + $i, 1, 1);
		$cal_standards->attach($cb2,  3, 2 + $i, 1, 1);
	    } else {
		my $cb = &cal_build_std_combobox($cur->{standards_2port});
		$cb->set_active_id($list->[$i]);
		$cb->signal_connect(changed => \&cal_stdcb_changed_cb,
			\$list->[$i]);
		$cal_standards->attach($cb,  1, 2 + $i, 3, 1);
	    }
	    my $delete = Gtk3::Button->new_from_stock("gtk-delete");
	    $delete->set_always_show_image(1);
	    $delete->signal_connect(clicked => \&cal_delete_clicked_cb, $i);
	    $cal_standards->attach($delete,   4, 2 + $i, 1, 1);
	}
	my $hbox = Gtk3::Box->new("horizontal", 5);
	my $add1 = Gtk3::Button->new("Add 1-Port Standards");
	$add1->signal_connect(clicked => \&cal_add_double_reflect_cb);
	my $add2 = Gtk3::Button->new("Add 2-Port Standard");
	$add2->signal_connect(clicked => \&cal_add_2x2_cb);
	$hbox->pack_start($add1, 1, 1, 0);
	$hbox->pack_start($add2, 1, 1, 0);
	$cal_standards->attach($hbox, 0, 2 + scalar(@{$list}), 5, 1);
    }
    $cal_standards->show_all();
}

sub cal_run_clicked_cb {
    my $cur = \%CurrentSettings;
    my $cal_name        = $builder->get_object("cal_name");
    my $cal_description = $builder->get_object("cal_description");
    my $cal_fmin        = $builder->get_object("cal_fmin");
    my $cal_fmax        = $builder->get_object("cal_fmax");
    my $cal_fmin_unit   = $builder->get_object("cal_fmin_unit");
    my $cal_fmax_unit   = $builder->get_object("cal_fmax_unit");
    my $cal_steps       = $builder->get_object("cal_steps");
    my $cal_log         = $builder->get_object("cal_log");
    my $standards = &cal_get_standards();

    my $name = $cal_name->get_text();
    if (length($name) == 0) {
	my $error_dialog =
	Gtk3::MessageDialog->new($appwindow, 'destroy-with-parent',
		'error', 'close', "A calibration name must be given.");
	$error_dialog->run();
	$error_dialog->destroy();
	return;
    }
    if ($standards eq "") {
	my $error_dialog =
	Gtk3::MessageDialog->new($appwindow, 'destroy-with-parent',
		'error', 'close', 'No calibration standards specified.');
	$error_dialog->run();
	$error_dialog->destroy();
	return;
    }
    my $description_buffer = $cal_description->get_buffer();
    my $description = $description_buffer->get_text(
	$description_buffer->get_start_iter(),
	$description_buffer->get_end_iter(), 1);
    my @Cmd;
    push(@Cmd, "cal");
    push(@Cmd, $cal_log->get_active() ? "-L" : "-l");
    if (defined($description) && $description ne "") {
	push(@Cmd, "-D", $description);
    }
    my $fmin = $cal_fmin->get_text();
    $fmin *= $HzIndexToMultiplier[$cal_fmin_unit->get_active()] / 1.0e+6;
    my $fmax = $cal_fmax->get_text();
    $fmax *= $HzIndexToMultiplier[$cal_fmax_unit->get_active()] / 1.0e+6;
    push(@Cmd, "-f", sprintf("%e:%e", $fmin, $fmax));
    push(@Cmd, "-n", $cal_steps->get_text());
    push(@Cmd, "-s", $cur->{cal_setup});
    push(@Cmd, "-S", $standards);
    push(@Cmd, "-t", $cur->{cal_type});
    push(@Cmd, $name);
    &run_command_dialog(\@Cmd, undef,
		\&cal_run_command_cb,
		[ $name, 1.0e+6 * $fmin, 1.0e+6 * $fmax ],
		"Measuring...");
}

#
# cal_run_command_cb
#
sub cal_run_command_cb {
    my $arg    = shift;
    my $result = shift;
    my ($name, $fmin, $fmax) = @$arg;

    my $cur = \%CurrentSettings;
    my $i;
    for ($i = 0; $i < scalar(@{$cur->{calibration_files}}); ++$i) {
	if ($cur->{calibration_files}[$i]{calfile} eq $name) {
	    last;
	};
    }

    #
    # When reading the frequency back from the VNA and converting
    # from string to double, we get some error.  If the frequency
    # range is slightly outside of the bounds, nudge it back in.
    #
    foreach my $calibration (@{$result->{calibrations}}) {
	if ($calibration->{fmin} < $fmin &&
		$calibration->{fmin} >= $fmin * 0.999) {
	    $calibration->{fmin} = $fmin;
	}
	if ($calibration->{fmax} > $fmax &&
		$calibration->{fmax} <= $fmin * 1.001) {
	    $calibration->{fmax} = $fmax;
	}
    }
    $cur->{calibration_files}[$i] = {
	calfile		=> $name,
	calibrations	=> $result->{calibrations}
    };
    &m_build_select_calibration();

    my $dialog = Gtk3::MessageDialog->new($appwindow,
	    'destroy-with-parent', 'info', 'close',
	    "Success");
    $dialog->run();
    $dialog->destroy();
}

#
# open_vna_activate_cb
#
sub open_vna_activate_cb {
    my $dialog = Gtk3::MessageDialog->new($appwindow,
	    'destroy-with-parent',
	    'error', 'ok', 'This function is not yet implemented.');
    $dialog->run();
    $dialog->destroy();
}

#
# close_vna_activate_cb
#
sub close_vna_activate_cb {
    my $dialog = Gtk3::MessageDialog->new($appwindow,
	    'destroy-with-parent',
	    'error', 'ok', 'This function is not yet implemented.');
    $dialog->run();
    $dialog->destroy();
}

#
# dark_theme_toggled_cb
#
sub dark_theme_toggled_cb {
    my $object = shift;

    my $value    = $object->get_active;
    my $settings = Gtk3::Settings->get_default();
    $settings->set( 'gtk-application-prefer-dark-theme', $value );
}

#
# about_activate_cb
#
sub about_activate_cb {
    my $dialog = Gtk3::MessageDialog->new($appwindow,
	    'destroy-with-parent', 'info', 'ok',
	    "N2PK VNA ${VERSION}\n" .
	    "https://github.com/https://github.com/scott-guthridge");
    $dialog->run();
    $dialog->destroy();
}

#
# manual_activate_cb: open the manual by messaging the default browswer
#
sub manual_activate_cb {
    open_browser("file:${DOCDIR}/index.html");
}


$CurrentSettings{vna} = N2PKVNA->new();

#
# Set up the UI
#
$builder = Gtk3::Builder->new();
$builder->add_from_file($N2PKVNA_UI);
$builder->connect_signals();
$appwindow = $builder->get_object("appwindow");

&update_gen_display();

my $r = $CurrentSettings{vna}->openVNA();
die "$r->{errors}" if $r->{status} ne "ok";
#printf("debug:\n");
#printf("YAML = [%s]\n", Dump($r));
#for (my $i = 0; $i < scalar(@{$r->{calibration_files}}); ++$i) {
#    printf("%d  %s\n", $i, $r->{calibration_files}[$i]{calfile});
#}
$CurrentSettings{properties}        = $r->{properties};
$CurrentSettings{calibration_files} = $r->{calibration_files};
$CurrentSettings{standards_1port}   = $r->{standards_1port};
$CurrentSettings{standards_2port}   = $r->{standards_2port};
if (!defined($CurrentSettings{properties})) {
    $CurrentSettings{properties} = {};
}
if (!defined($CurrentSettings{properties}{setups})) {
    $CurrentSettings{properties}{setups} = {};
}
if (!defined($CurrentSettings{calibration_files})) {
    $CurrentSettings{calibration_files} = [];
}
if (!defined($CurrentSettings{standards_1port})) {
    $CurrentSettings{standards_1port} = [];
}
if (!defined($CurrentSettings{standards_2port})) {
    $CurrentSettings{standards_2port} = [];
}
&setup_init_menu();
&cal_build_setup();
&m_build_select_calibration();
{
    my $m_parameters = $builder->get_object("m_parameters");
    my $stack1 = $builder->get_object("stack1");

    $m_parameters->set_active_id("s");

    #
    # Set initial screen based on what objects have been
    # created so far.
    #
    if (scalar(@{$CurrentSettings{calibration_files}}) > 0) {
	$stack1->set_visible_child_name("measure");
    } elsif (scalar(%{$CurrentSettings{properties}{setups}}) > 0) {
	$stack1->set_visible_child_name("calibrate");
    } else {
	$stack1->set_visible_child_name("setup");
    }
}

$appwindow->show_all();
Gtk3->main();


###############################################################################
# Package RefCount
###############################################################################

package RefCount;
use Carp;


sub new {
    my $proto       = shift;
    my $counter_ref = shift;
    my $class = ref($proto) || $proto;

    my $self = {
	counter_ref => $counter_ref,
	held        => undef,
    };
    bless($self, $class);
    return $self;
}

sub check_hold {
    my $self = shift;

    die if $self->{held};
    if (${$self->{counter_ref}} > 0) {
	return 1;
    }
    ++${$self->{counter_ref}};
    $self->{held} = 1;
    return 0;
}

sub hold {
    my $self = shift;

    die if $self->{held};
    ++${$self->{counter_ref}};
    $self->{held} = 1;
}

sub release {
    my $self = shift;

    die unless $self->{held};
    die unless ${$self->{counter_ref}};
    --${$self->{counter_ref}};
    $self->{held} = undef;
}

sub DESTROY {
    my $self = shift;

    if ($self->{held}) {
	$self->release();
    }
}


###############################################################################
# Package N2PKVNAConvert
###############################################################################

package N2PKVNAConvert;
use Carp;
use File::Temp qw(tempdir);

sub new {
    my $proto = shift;

    my $class = ref($proto) || $proto;
    my $self = {
	directory	=> tempdir(CLEANUP => 1),
	typeset		=> {},
	ports		=> undef
    };
    bless($self, $class);
    return $self;
}

sub getdir {
    my $self = shift;

    return $self->{directory};
}

sub set_ready {
    my $self = shift;
    my $ports = shift;

    $self->{ports} = $ports;
    $self->{typeset}{sri} = 1;
}

sub valid_conversion {
    my $self      = shift;
    my $type      = shift;

    my %TwoPortOnly = (
	tri	=> 1,
	tdb	=> 1,
	tma	=> 1,
	uri	=> 1,
	uma	=> 1,
	udb	=> 1,
	hri_n	=> 1,
	hma_n	=> 1,
	gri_n	=> 1,
	gma_n	=> 1,
	ari_n	=> 1,
	ama_n	=> 1,
	bri_n	=> 1,
	bma_n	=> 1,
	il	=> 1
    );
    return $self->{ports} == 2 ||
	($self->{ports} == 1 && !$TwoPortOnly{$type});
}

sub convert {
    my $self      = shift;
    my $type      = shift;
    my $dialog    = shift;

    if ($type eq "smith") {
	$type = "sri";
    }
    if (defined($self->{typeset}{$type})) {
	return $self->{directory} . "/" . $type . ".npd";
    }
    if (!defined($self->{typeset}{sri})) {
	return undef;
    }
    if (!$self->valid_conversion($type)) {
	return undef;
    }
    my $parameter = $type;
    my $normalize;
    if ($parameter =~ s/_n$//) {
	$normalize = 1;
    }
    my @Cmd = ("convert", "-p", $parameter);
    if ($normalize) {
	push(@Cmd, "-z", "1");
    }
    my $directory = $self->{directory};
    my $inputfile = $directory . "/sri.npd";
    my $outputfile = $directory . "/" . $type . ".npd";
    push(@Cmd, $inputfile, $outputfile);
    if (&{$dialog}(\@Cmd, undef, undef, undef, undef)) {
	$self->{typeset}{$type} = 1;
	return $self->{directory} . "/" . $type . ".npd";
    }
    return undef;
}

###############################################################################
# Package N2PKVNA
###############################################################################

package N2PKVNA;
use threads;
use Carp;
use File::Temp qw(tempfile);
use FileHandle;
use IPC::Open2;
use YAML::XS;
use Thread::Queue;
use Glib 'TRUE', 'FALSE';
use Glib;
use strict;
use warnings;

#
# response->{status} values:
#   ok
#   error
#   canceled
#   needsACK
#   needsMeasuredF
#   needsUnitExit
#   errorExit
#   closed
#

#
# new: constructor
#
sub new {
    my $proto = shift;

    my $class = ref($proto) || $proto;
    my $self = {
	# State:
	#     ready		ready for command
	#     sent		waiting for response
	#     needsACK		needs single line response
	#     needsmeasuredF	needs a measured frequency value
	#     exited		program has exited
	#     closed		connection to command closed and cleaned up
	state		=> "closed",

	#
	# Child PID, and input and output (to this program) streams
	#
	thread		=> undef,
	requestQ	=> Thread::Queue->new(),
	responseQ	=> Thread::Queue->new(),
    };
    bless($self, $class);
    $self->{thread} = threads->create(\&_worker, $self);

    return $self;
}

#
# open: open the VNA
#
sub openVNA {
    my $self = shift;
    my @args = @_;

    if ($self->{state} ne "closed") {
	croak("N2PK VNA open: not in closed state: $self->{state}");
    }

    $self->{requestQ}->enqueue(Dump([ "open", \@args ]));
    my $response = $self->{responseQ}->dequeue();
    if (!defined($response)) {
	return undef;
    }
    $response = Load($response);
    if ($response->{status} eq "ok") {
	$self->{state} = "ready";
    }
    return $response;
}

#
# close: close the VNA
#
sub closeVNA {
    my $self = shift;

    $self->{requestQ}->enqueue(Dump([ "close" ]));
    $self->{state} = "closed";
    for (;;) {
	my $response = $self->{responseQ}->dequeue();
	if (!defined($response)) {
	    last;
	}
	$response = Load($response);
	if ($response->{status} eq "closed") {
	    last;
	}
    }
}

#
# send: send a command to the VNA
#
sub sendCommand {
    my $self         = shift;
    my $command      = shift;
    my $input        = shift;

    if ($self->{state} ne "ready") {
	croak("N2PK VNA status(1): $self->{state}");
    }
    $self->{requestQ}->enqueue(Dump([ "send", $command, $input ]));
    $self->{state} = "sent";
}

#
# send: send a command to the VNA
#
sub sendACK {
    my $self         = shift;
    my $arguments    = shift;
    my $input        = shift;

    if ($self->{state} ne "needsACK" && $self->{state} ne "needsMeasuredF") {
	croak("N2PK VNA status(2): $self->{state}");
    }
    $self->{requestQ}->enqueue(Dump([ "send", $arguments, $input ]));
    $self->{state} = "sent";
}

#
# receiveResponse: wait/check for a response
#
sub receiveResponse {
    my $self     = shift;
    my $nonblock = shift;

    if ($self->{state} ne "sent") {
	croak("N2PK VNA status: $self->{state}");
    }
    my $response;
    if ($nonblock) {
	$response = $self->{responseQ}->dequeue_nb();
	if (!defined($response)) {
	    return undef;
	}
    } else {
	$response = $self->{responseQ}->dequeue();
    }
    die unless defined $response;
    $response = Load($response);

    #
    # Update the state.
    #
    my $status = $response->{status};
    if ($status eq "needsACK") {
	$self->{state} = "needsACK";
    } elsif ($status eq "needsMeasuredF") {
	$self->{state} = "needsMeasuredF";
    } elsif ($status eq "needsUnitExit" || $status eq "errorExit") {
	$self->{state} = "exited";
    } else {
	$self->{state} = "ready";
    }

    die unless ref($response) eq "HASH";
    return $response;
}

#
# shutdown: stop the background thread
#
sub shutdown {
    my $self = shift;
    $self->{requestQ}->enqueue(Dump([ "shutdown" ]));
    $self->{thread}->join();
    $self->{thread} = undef;
}

#
# _quote: convert an argument list into a string with quoted arguments
#
sub _quote {
    my $quoted = "";
    while (scalar(@_)) {
	my $arg = shift(@_);

	if ($quoted ne "") {
	    $quoted .= " ";
	}
	if (!($arg =~ m/[\s'"\\]/)) {		# no special characters
	    $quoted .= $arg;

	} elsif (!($arg =~ m/'/)) {		# no single quotes
	    $quoted .= "\'" . $arg . "\'";

	} else {				# general case
	    $arg =~ s/[\$`"\\]/\\$&/g;
	    $quoted .= '"' . $arg . '"';
	}
    }
    return $quoted;
}

#
# _parse_response: parse YAML response from the n2pkvna program
#
sub _parse_response {
    my $stdout = shift;
    my $stderr = shift;

    #
    # Expect a YAML document on stdout.
    #
    my $yaml;
    my $exited = 1;
    while (<$stdout>) {
	$yaml .= $_;
	if (/^\.\.\./) {
	    $exited = 0;
	    last;
	}
    }
    if ($exited) {
	my $response = {
	    status => "errorExit"
	};
	my $errors = "";
	while (<$stderr>) {
	    $errors .= $_;
	}
	if ($errors ne "") {
	    $response->{errors} = $errors;
	}
	return $response;
    }

    #
    # Parse YAML.
    #
    my $response = Load($yaml);
    if (!defined($response)) {
	$response = {
	    status => "error",
	    errors => "Bad YAML:\n" . $yaml,
	};
    }
    return $response;
}

#
# _worker: worker thread body
#
sub _worker {
    my $self = shift;

    my $pid;
    my $stdin;
    my $stdout;
    my $stderr;
    my $error_filename;

    while (my $item = $self->{requestQ}->dequeue()) {
	$item = Load($item);
	my ($operation, $arguments, $input) = @$item;
	my $response = {
	    status => "error"
	};

	#
	# Send command or ACK.
	#
	if ($operation eq "send") {
	    my $command = &_quote(@{$arguments});
	    printf $stdin ("%s\n", $command);
	    if (defined($input)) {
		printf $stdin ("%s", Dump($input) . "...\n");
	    }
	    $response = &_parse_response($stdout, $stderr);
	    die unless ref($response) eq "HASH";
	    $self->{responseQ}->enqueue(Dump($response));
	    next;
	}

	#
	# Open the VNA.
	#
	if ($operation eq "open") {
	    #
	    # Make temporary file to catch stderr.
	    #
	    if (!defined($error_filename)) {
		if (!(($stderr, $error_filename) = tempfile())) {
		    $response->{errors} = $!;
		    die unless ref($response) eq "HASH";
		    $self->{responseQ}->enqueue(Dump($response));
		    next;
		}
	    }

	    #
	    # Start the command.
	    #
	    my $cmd = $main::N2PKVNA;
	    $cmd = &_quote($cmd, @{$arguments});
	    $cmd .= " -Y";
	    $cmd .= " 2> " . $error_filename;
	    if (!($pid = open2($stdout, $stdin, $cmd))) {
		$response->{errors} = $!;
		return $response;
	    }

	    #
	    # Get the open response.
	    #
	    $response = &_parse_response($stdout, $stderr);
	    die unless ref($response) eq "HASH";
	    $self->{responseQ}->enqueue(Dump($response));
	    next;
	}

	#
	# Close the VNA
	#
	if ($operation eq "close") {
	    close($stdin);
	    $stdin = undef;
	    close($stdout);
	    $stdout = undef;
	    waitpid($pid, 0);
	    close($stderr);
	    $stderr = undef;
	    unlink($error_filename);
	    $error_filename = undef;
	    $self->{responseQ}->enqueue(Dump({ status => "closed" }));
	    next;
	}

	#
	# Shut down
	#
	if ($operation eq "shutdown") {
	    last;
	}

	die "_worker: unknown operation: ${operation}";
    }
}
