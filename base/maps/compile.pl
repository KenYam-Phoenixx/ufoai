#!/usr/bin/perl
use File::stat;
use strict;

# If you have a "make" program installed, you're likely to prefer the
# Makefile to this script, which cannot run jobs in parallel, is not
# able to rebuild only the maps that changed since last time you
# built, and does not handle the case when qrad3 gets interrupted
# mid-way.

my $extra = "-bounce 0 -extra";

my $ufo2map = "../../ufo2map";

if ($^O =~ /windows/i || $^O =~ /mswin/i) {
	$ufo2map .= ".exe";
}

sub readDir
{
	my $dir = shift || die "No dir given in sub readDir\n";
	my $status = opendir (DIR, "$dir");
	my @files = ();
	unless ( $status )
	{
		print "Could not read $dir\n";
		return ();
	}

	@files = readdir ( DIR );
	closedir (DIR);
	@files;
}

sub compile
{
	my $dir = shift || die "No dir given in sub compile\n";
	my $found = 0;
	my $stat1;
	my $stat2;
	my $map_path;		# Path to the "source" file of the map (i.e. the *.map file)
	my $compile_path;	# Path to the compiled map (i.e. the *.bsp file)

	print "...entering $dir\n";
	foreach ( readDir( $dir ) )
	{
		# The foreach/readDir combination sets "$_" to the next file or directory in $dir

		next if $_ =~ /^\./;
		if ( -d "$dir/$_" )
		{
			print "...found dir $_\n";
			$found += compile("$dir/$_");
			print "...dir $dir/$_ finished\n";
			next;
		}
		next unless $_ =~ /\.map$/;
		$map_path = $_;
		next if $map_path =~ /^(tutorial)|(prefab)|(autosave)/i;

		$map_path = "$dir/$map_path";		# results in something like "$dir/xxx.map"
		$compile_path = $map_path;
		$compile_path =~ s/\.map$/.bsp/;	# results in something like "$dir/xxx.bsp"

		# print "DEBUG: ", $compile_path; #DEBUG
		if (-e $compile_path)
		{
			$stat1 = stat($map_path);
			$stat2 = stat($compile_path);

			if ($stat1->mtime > $stat2->mtime)
			{
				unlink($compile_path);
			}
		}

		unless ( -e $compile_path )
		{
			print "..found $dir/$_\n";
			if (system("$ufo2map $extra $dir/$_") != 0)
			{
				die "ufo2map failed";
			}
			$found++;
		}
		else
		{
			print "..already compiled $_\n";
		}
	}
	return $found;
}

#read the given dir
my $dir = $ARGV[0] || ".";
print "=====================================================\n";
print "Mapcompiler for UFO:AI (http://sf.net/projects/ufoai)\n";
print "Giving base/maps as parameter or start from base/maps\n";
print "will compile all maps were no bsp-file exists\n";
print "Keep in mind that ufo2map needs to be in path\n";
print "=====================================================\n";
print "Running at: $^O\n";

die "Dir $dir does not exists\n" if ( !-e $dir );

if (!-e "$ufo2map") {
	print "$ufo2map wasn't found\n";
	$ufo2map = "ufo2map";
}

print "Found ufo2map in \"$ufo2map\"\n";
print "Found ". compile($dir) ." maps\n";

print "...finished\n"
