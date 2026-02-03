#!/usr/bin/perl
# Aegis D-Bus interface library

use strict;
use warnings;
use JSON::PP;

our $aegis_user = "yourusername";  # CHANGE THIS
our $dbus_address = &get_dbus_address();

sub get_dbus_address {
    # Get the D-Bus session bus address for the user running Aegis
    my $uid = `id -u $aegis_user`;
    chomp($uid);
    return "unix:path=/run/user/$uid/bus";
}

sub aegis_call {
    my ($interface, $method, @args) = @_;

    # Build qdbus command
    my $service = "org.mpris.MediaPlayer2.aegis";
    my $object = ($interface eq "org.mpris.MediaPlayer2.Player") ?
                 "/org/mpris/MediaPlayer2" : "/org/aegis/Admin";

    my $cmd = "sudo -u $aegis_user DBUS_SESSION_BUS_ADDRESS=$dbus_address ";
    $cmd .= "qdbus $service $object $interface.$method";

    foreach my $arg (@args) {
        $arg =~ s/'/'\\''/g;
        $cmd .= " '$arg'";
    }

    $cmd .= " 2>&1";

    my $output = `$cmd`;
    chomp($output);

    # Check for errors
    if ($output =~ /Error|error|Cannot connect/i) {
        return undef, $output;
    }
    return $output, undef;
}

sub get_status {
    my ($data, $err) = &aegis_call("org.aegis.Admin", "getPlaybackStatus");
    return undef, $err if $err;

    eval {
        return decode_json($data);
    };
    return undef, "JSON parse error: $@";
}

sub get_library_stats {
    my ($data, $err) = &aegis_call("org.aegis.Admin", "getLibraryStats");
    return undef, $err if $err;

    eval {
        return decode_json($data);
    };
    return undef, "JSON parse error: $@";
}

sub control {
    my ($action) = @_;
    my %actions = (
        'playPause' => 'playPause',
        'stop' => 'stop',
        'next' => 'next',
        'previous' => 'previous'
    );

    return "Invalid action" unless exists $actions{$action};

    my ($res, $err) = &aegis_call("org.aegis.Admin", $actions{$action});
    return $err if $err;
    return undef;
}

sub set_volume {
    my ($vol) = @_;
    $vol = 0 if $vol < 0;
    $vol = 100 if $vol > 100;

    my ($res, $err) = &aegis_call("org.aegis.Admin", "setVolume", $vol);
    return $err if $err;
    return undef;
}

sub scan_directory {
    my ($path) = @_;
    return "Invalid path" unless -d $path;

    my ($res, $err) = &aegis_call("org.aegis.Admin", "scanDirectory", $path);
    return $err if $err;
    return undef;
}
