#!/usr/bin/perl
use Math::Trig;
use Graphics::Fig;
use strict;
use warnings;

#
# smith: draw a Smith chart of given length (square)
#
sub smith {
    my $fig    = shift;
    my $length = shift;

    $fig->begin();

    #
    # Draw constant resistance circles for the given real gamma
    # values.
    #
    foreach my $Gr ( -1, -0.5, 0, 0.5 ) {
	my $x_center = ($Gr + 1.0) / 2.0;
	my $radius   = ($Gr - 1.0) / 2.0;

	$fig->circle({ center => [ $x_center, 0 ], r => $radius });
    }

    #
    # Draw constant reactance arcs for the given angles.
    #
    foreach my $degrees ( 135, 90, 45 ) {
	my $theta = $degrees / 180.0 * pi;
	my $y_center = tan($theta / 2.0);
	my $radius   = $y_center;
	my $angle = atan2(1.0 - cos($theta), $y_center - sin($theta));

	$fig->arc({ points => [[ 1, 0 ], [ cos($theta),  sin($theta) ]],
		angle =>  $angle * 180 / pi });
	$fig->arc({ points => [[ 1, 0 ], [ cos($theta), -sin($theta) ]],
		angle => -$angle * 180 / pi });
    }

    #
    # Draw the zero reactance line.
    #
    $fig->polyline([[ -1, 0 ], [ 1, 0 ]]);

    $fig->translate([ 1, 1 ]);
    $fig->scale(0.5 * $length);
    $fig->end("group");
}

my $fig = Graphics::Fig->new({ units => "inch" });

#
# Draw the background (depth 100).
#
#$fig->polyline([[ 0, 0 ], [ 0, 1 ], [ 1, 1 ], [ 1, 0 ], [ 0, 0 ]],
#    { depth => 99, lineThickness => "1 pt", penColor => "black" } );

$fig->box([[ 0, 0 ], [ 1, 1 ]],
    { areaFill => "full", fillColor => "yellow", depth => 100,
      lineThickness => "1 pt", penColor => "black" } );

#
# Draw the Smith chart (depth 75)
#
$fig->begin({ color => "SlateGray", depth => 75 });
&smith($fig, 0.74);
$fig->translate([ 0.25, 0.25 ]);
$fig->end("merge");

#
# Draw the text (depth 50)
#
$fig->text("N2PK", { position => [ 0.025, 0.24 ],
	fontSize => 30, color => "blue" });
$fig->text("VNA", { position => [ 0.025, 0.44 ],
	fontSize => 24, color => "black" });

#$fig->save("icon.fig");
$fig->export("n2pkvna.svg");
