#!/usr/bin/perl

use 5.018;
use strict;
use warnings;


open my $fh, "<", $ARGV[0] or die $!;

my $line = <$fh>;
chomp $line;
my ($w, $h) = ($1, $2) if $line =~ /(\d+):(\d+)/;

while(<$fh>){
    chomp;
    my ($tw, $th, $fovy, $other, $tu, $tv) = ($1, $2, $3, $4, $5, $6) if $_ =~ /^(\d+):(\d+):([^:]+):(.*):(\d+):(\d+)$/;
    $tw /= $w;
    $th /= $h;
    $tu /= $w;
    $tv /= $h;

    $other =~ s/:(eqdis|eqdeg|uneqdeg|eqdis-ecoef)//g;

    say join(":", ($tw, $th, $fovy, $fovy, $other, $tu, $tv));
}


close $fh;
