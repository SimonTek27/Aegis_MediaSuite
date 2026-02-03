#!/usr/bin/perl
# Main Webmin interface for Aegis

require './aegis-lib.pl';
require 'header.cgi';

my $info = &get_library_stats();
my $status = &get_status();

# Handle form submissions
if ($in{'action'}) {
    my $err = &control($in{'action'});
    print "<script>window.location.href='index.cgi';</script>\n" unless $err;
    print "<div class='alert alert-danger'>Error: $err</div>\n" if $err;
}

if (defined $in{'volume'}) {
    my $err = &set_volume($in{'volume'});
    print "<div class='alert alert-success'>Volume set to $in{'volume'}%</div>\n" unless $err;
}

if ($in{'scan'}) {
    my $err = &scan_directory($in{'scanpath'});
    print "<div class='alert alert-info'>Scanning started...</div>\n" unless $err;
    print "<div class='alert alert-danger'>Scan error: $err</div>\n" if $err;
}

print "<h2>Aegis Media Control</h2>\n";

# Playback Status Card
print "<div class='row'>\n";
print "<div class='col-md-6'>\n";
print "<div class='panel panel-default'>\n";
print "<div class='panel-heading'><h3 class='panel-title'>Now Playing</h3></div>\n";
print "<div class='panel-body'>\n";

if ($status) {
    my $state = $status->{playing} ? "<span class='label label-success'>Playing</span>" :
                                     "<span class='label label-default'>Stopped</span>";
    print "<p><strong>Status:</strong> $state</p>\n";
    print "<p><strong>File:</strong> ", &html_escape($status->{currentFile} || "None"), "</p>\n";
    print "<p><strong>Position:</strong> ", int($status->{position} || 0), "s / ",
          int($status->{duration} || 0), "s</p>\n";
    print "<p><strong>Volume:</strong> ", int($status->{volume} || 0), "%</p>\n";
} else {
    print "<div class='alert alert-warning'>Cannot connect to Aegis. Is it running?</div>\n";
}

print "</div></div></div>\n";

# Library Stats Card
print "<div class='col-md-6'>\n";
print "<div class='panel panel-default'>\n";
print "<div class='panel-heading'><h3 class='panel-title'>Library Statistics</h3></div>\n";
print "<div class='panel-body'>\n";

if ($info) {
    print "<p><strong>Total Tracks:</strong> ", $info->{trackCount} || 0, "</p>\n";
    print "<p><strong>Database Size:</strong> ",
          &nice_size($info->{dbSizeBytes} || 0), "</p>\n";
    print "<p><strong>Scanning:</strong> ",
          ($info->{scanning} ? "Yes" : "No"), "</p>\n";
    print "<p><small class='text-muted'>Last updated: $info->{timestamp}</small></p>\n";
}

print "</div></div></div>\n";
print "</div>\n";

# Control Buttons
print "<div class='panel panel-default'>\n";
print "<div class='panel-heading'>Playback Controls</div>\n";
print "<div class='panel-body'>\n";
print "<form class='form-inline' action='index.cgi'>\n";
print "<button type='submit' name='action' value='playPause' class='btn btn-primary'>Play/Pause</button>\n";
print "<button type='submit' name='action' value='stop' class='btn btn-danger'>Stop</button>\n";
print "<button type='submit' name='action' value='previous' class='btn btn-default'>Previous</button>\n";
print "<button type='submit' name='action' value='next' class='btn btn-default'>Next</button>\n";
print "</form>\n";
print "</div></div>\n";

# Volume Control
print "<div class='panel panel-default'>\n";
print "<div class='panel-heading'>Volume</div>\n";
print "<div class='panel-body'>\n";
print "<form class='form-inline' action='index.cgi'>\n";
print "<input type='number' name='volume' value='", int($status->{volume} || 50),
      "' min='0' max='100' class='form-control' style='width:100px'>\n";
print "<button type='submit' class='btn btn-default'>Set Volume</button>\n";
print "</form>\n";
print "</div></div>\n";

# Library Scan
print "<div class='panel panel-default'>\n";
print "<div class='panel-heading'>Library Management</div>\n";
print "<div class='panel-body'>\n";
print "<form class='form' action='index.cgi'>\n";
print "<div class='form-group'>\n";
print "<label>Scan Directory:</label>\n";
print "<input type='text' name='scanpath' class='form-control' placeholder='/home/user/Music'>\n";
print "</div>\n";
print "<button type='submit' name='scan' value='1' class='btn btn-success'>Scan Directory</button>\n";
print "</form>\n";
print "</div></div>\n";

&footer("index.cgi", "Aegis");
