#!/usr/bin/perl

use 5.018;
use strict;
use warnings;

use List::Util qw/any/;

my ($iv, $ov, $il, $ol, $ow, $oh, $ofs, $ovs, $dflag, $crf, $ecoef, $cbr); # input video, output video, input layout, output layout, output width, output height, output fragment shader, output vertex shader, debug flag

for my $arg (@ARGV) {
    $iv = $1 if $arg =~ /iv=([^\s]+)/;
    $ov = $1 if $arg =~ /ov=([^\s]+)/;
    $il = $1 if $arg =~ /il=([^\s]+)/;
    $ol = $1 if $arg =~ /ol=([^\s]+)/;
    ($ow, $oh) = ($1, $2) if $arg =~ /res=(\d+)x(\d+)/;
    $ofs = $1 if $arg =~ /ofs=([^\s]+)/;
    $ovs = $1 if $arg =~ /ovs=([^\s]+)/;
    $dflag = $1 if $arg =~ /dflag=([^\s]+)/;
    $ecoef = $1 if $arg =~ /ecoef=([^\s]+)/;
    $crf = $1 if $arg =~ /crf=([^\s]+)/;
    $cbr = $1 if $arg =~ /cbr=([^\s]+)/;
}


if( any { !defined $_ } ( $iv, $ol, $ow, $oh ) ) {
    say "Must specify options for iv/ol/ow/oh!";
    &usage();
    exit;
}


$ov = "out.mp4" unless defined $ov;
$il = "cube.lt" unless defined $il;
$ofs = "eqdis.glsl" unless defined $ofs;
$ovs = "vertex.glsl" unless defined $ovs;
$dflag = "info" if !defined $dflag or $dflag !~ /(info|debug)/;
$crf = defined $crf ? "-crf $crf" : "";
$cbr = defined $cbr ? "-b:v $cbr" : "";

sub usage {
    say 'usage: ./remap.pl $option=value';
    say 'options (default values):';
    say '  iv: input video';
    say '  ov: output video (out.mp4)';
    say '  il: input layout (cube.lt)';
    say '  ol: output layout';
    say '  res: output width and height, use like res=1280x1080';
    say '  ofs: output fragment shader (eqdis.glsl)';
    say '  ovs: output vertex shader (vertex.glsl)';
    say '  dflag: debug flag (info)';
    say '  crf: the compression level, where 0 is the best quality';
    say '  ecoef: the expansion ecoefficient';
    say '  cbr: constant bitrate';
}

say "iv=$iv, ov=$ov, il=$il, ol=$ol, ow=$ow, oh=$oh, ofs=$ofs, ovs=$ovs, dflag=$dflag, crf=$crf, ecoef=$ecoef cbr=$cbr" if $dflag eq "debug";

# extract frame rate of the input video;
my $fps = 0;
my $q_arg = "";
my $two_pass = 0;
unless($iv =~ /(\.jpg|\.bmp|\.png)/){
    $fps = $1 if `ffprobe $iv 2>&1` =~ /([^\s]+)\s+fps/;
    $fps = int($fps+0.5);
    say "Input video fps: $fps";
    $q_arg = "-c:v libx264 -x264opts 'keyint=${fps}:min-keyint=${fps}:no-scenecut' ";
    if ($cbr){
        $two_pass = 1;
    }elsif($crf){
        $q_arg .= $crf;
    }else{
        $q_arg .= "-crf 18";
    }
}else{
    $q_arg = "-q:v 2";
}

my @ol;
open my $fh, "<", $ol or die $!;
while(<$fh>){
    chomp;
    push @ol, $_;
}
close $fh;

for(my $i = 0; $i < @ol; $i++){
    my @args1 = split /:/, $ol[$i];
    my ($t1, $b1, $l1, $r1) = ($args1[8], $args1[8] + $args1[1], $args1[7], $args1[7] + $args1[0]);
    for(my $j = $i+1; $j < @ol; $j++){
        my @args2 = split /:/, $ol[$j];
        my ($t2, $b2, $l2, $r2) = ($args2[8], $args2[8] + $args2[1], $args2[7], $args2[7] + $args2[0]);
        if(!check_overlap($t1, $b1, $l1, $r1, $t2, $b2, $l2, $r2)){
            say "WARNING: tiles defined in line " . ($i+1) . " and " . ($j+1) . " overlap";
            say "  " . ($i + 1) . ": " . $ol[$i];
            say "  " . ($j + 1) . ": " . $ol[$j];
        }
    }
}

sub check_overlap {
    my ($t1, $b1, $l1, $r1, $t2, $b2, $l2, $r2) = (@_);
    my $err = 0.0000001;
    return ($t1 >= $b2 - $err) || ($b1 <= $t2 + $err) || ($r1 <= $l2 + $err) || ($l1 >= $r2 - $err);
}



my $prefix_args = "-y -loglevel 'info' -i $iv";
my $filter_args = "-filter_complex \"nullsrc=";
unless($iv =~ /(\.jpg|\.bmp)/){
    $filter_args .= "rate=$fps:";
}
$filter_args .= "size=${ow}x${oh} [base];";
my ($project_args, $overlay_args);

for(my $i = 0; $i < @ol; $i++){
    # w:h:xfov:yfov:xr:yr:zr:u:v
    # 0:1:2   :3   :4 :5 :6 :7:8
    my @args = split /:/, $ol[$i];

    my $w = int($args[0] * $ow);
    my $h = int($args[1] * $oh);
    my $x = int($args[7] * $ow);
    my $y = int($args[8] * $oh);
    my $xfov = $args[2];
    my $yfov = $args[3];
    my $others = join ":", @args[4 .. 6];

    if($w % 8 != 0){
        $w += (8 - $w % 8);
    }

    if($h % 8 != 0){
        $h += (8 - $h % 8);
    }

    if($x % 8 != 0){
        $x += (8 - $x % 8);
    }

    if($y % 8 != 0){
        $y += (8 - $y % 8);
    }


    my $proj_arg = join ":", ($w, $h, $xfov, $yfov, $others, $ovs, $ofs, "", $il);
    $proj_arg .= ":0:${ecoef}" if defined $ecoef;
    $project_args .= "[0:v] project=${proj_arg} [pos${i}]; ";

    if($i == 0){
        $overlay_args .= "[base][pos${i}] overlay=shortest=1:x=${x}:y=${y}";
    }else{
        $overlay_args .= ("[tmp" . ($i-1) . "][pos${i}] overlay=shortest=1:x=${x}:y=${y}");
    }

    if($i != $#ol){
        $overlay_args .= " [tmp${i}]; ";
    }else{
        $overlay_args .= "\"";
    }

    say $proj_arg;

}

if(!$two_pass){
    my $ffmpeg_cmd;
    $ffmpeg_cmd = join " ", ("./ffmpeg", $prefix_args, $filter_args, $project_args, $overlay_args, $q_arg, $ov);
    say $ffmpeg_cmd;

    open my $cmd_fh, "$ffmpeg_cmd |";
    while(<$cmd_fh>){
    say $_;
    }
    close $cmd_fh;
}else{
    my $ffmpeg_cmd1 = join " ", ("./ffmpeg", $prefix_args, $filter_args, $project_args, $overlay_args, $q_arg, $cbr, "-pass 1 -f mp4 /dev/null");
    say $ffmpeg_cmd1;

    open my $cmd_fh1, "$ffmpeg_cmd1 |";
    while(<$cmd_fh1>){
        say $_;
    }
    close $cmd_fh1;

    my $ffmpeg_cmd2 = join " ", ("./ffmpeg", $prefix_args, $filter_args, $project_args, $overlay_args, $q_arg, $cbr, "-pass 2", $ov);
    say $ffmpeg_cmd2;

    open my $cmd_fh2, "$ffmpeg_cmd2 |";
    while(<$cmd_fh2>){
        say $_;
    }
    close $cmd_fh2;
}

