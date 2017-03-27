#!/usr/bin/perl -w
#
# robopatch.pl
#
# Author: Phil Carmody <phil.carmody@partner.samsung.com>
# Copyright 2013- Samsung Electronics Ltd.
# Licensed under the terms of the GNU GPL License version 2

# Attempt to clean files mentioned in checkpatch output.
# It relies on checkpatch having all the intelligence,
# isolating the problems very specifically, so the actual
# fixes can be done relatively safely. However, you should
# always check the output.

# Error handling is on a fail-early and fail-often policy.
# Verbose logging goes to stderr presently, while this is WIP.

# Typical usage:
# $ scripts/checkpatch.pl WHATEVER > /tmpcheckpatch.log
# $ scripts/robopatch.pl < /tmp/checkpatch.log
# $ git diff
#

use strict qw(refs subs vars);
use warnings;

# This script edits files in place unless --dryrun is set
my $dryrun=0;

# TODO: permit filtering of which rules you want to act upon.
my $filtered=0;

while(@ARGV and $ARGV[0] =~ m/^-/) {
	my $arg=shift(@ARGV);
	if($arg =~ m/--dryrun/) {
		$dryrun=1;
	}
	if($arg =~ m/--filter=(.+)/) {
		# TODO: parse a filter file
		$filtered=1;
	}
	if($arg =~ m/--/) { last; }
}

my %aliases;    # short name -> full path
my %files;      # short name -> array of changes
my @files;      # short name in order seen

my $lastfile=undef;
my $lastline=undef;
my $state=0;
my $build=undef;
while(<>) {
	chomp;
	my $handled=0;
	if($state==0
			and m/^total: (\d+) errors, (\d+) warnings, (\d+) lines checked/) {
		if(defined($lastfile)) {
			print STDERR "$lastfile: E/W/L=$1/$2/$3\n" if($1 or $2 or $3);
			$state=4;
		} else {
		$state=5;
	}
		$handled=1;
	} elsif($state==0 and m/^(?:WARNING|ERROR): .*/) {
		die("unknown state before: $_") if($build);
		$build={ message => $_ };
		$state=1;
		$handled=1;
	} elsif($state==0 and
		m/^(If any of these errors are false|them to the maintainer)/) {
		$handled=1;
	} elsif($state==1 and m/^#\d+: FILE: (.+):(\d+):$/) {
		my ($f, $l)=($1, $2);
		die("file confusion, lf=$lastfile f=$f") if(defined($lastfile) and $lastfile ne $f);
		if(!defined($files{$f})) { push @files, $f; $files{$f}=[]; }
		if(defined($lastfile) and defined($lastline) and ($lastline eq $l)
				and ($files{$lastfile}->[-1]->{'message'} eq $build->{'message'})) {
			# This message is identical to the previous one, just tally.
			$build->{'tally'}=$files{$lastfile}->[-1]->{'tally'}+1;
			pop(@{$files{$lastfile}});
		} else {
			$build->{'tally'}=1;
		}
		$build->{'file'}=$f;
		$build->{'line'}=$l;
		$build->{'fileline'}=$_;
		$build->{'lines'}=[];
		$lastfile=$f;
		$lastline=$l;
		$state=2;
		$handled=1;
	} elsif($state==2 and m/^(\+|\[\.\.\.\]$)/) {
		push @{$build->{'lines'}}, $_;
		$handled=1;
	} elsif($state==2 and m/^ (\s*)\^/) {
		if(@{$build->{'lines'}} != 1 or defined($build->{'column'})) {
			die("column confusion")
		}
		$build->{'column'}=$_;
		$handled=1;
	} elsif(($state==2 or $state==3) and m/^$/) {
		push @{$files{$lastfile}}, $build;
		$build=undef;
		$state=0;
		$handled=1;
	} elsif($state==4 and m/^(.+) has style problems, please review/) {
		$aliases{$lastfile}=$1;
		$lastfile=undef;
		$lastline=undef;
		$state=0;
		$handled=1;
	} elsif($state==4 or $state==5 or $state==0 and m/^$/) {
		$handled=1;
	} else {
		die("in state $state (lf=$lastfile), what's: $_");
	}
}

# Stub - currently process everything possible
sub msg_is_OK($)
{
	if(!$filtered) { return 1; }
	return 1;
}

sub handle_message($$$$)
{
	my ($rfn,$whref,$inref,$outref)=@_;
	my $handled=0;
	my ($msg, $line)=@{$whref}{'message', 'line'};
	my $tail=$#{$whref->{'lines'}};

	print("msg = $msg\n");
	# Zillions of these, and really can't or don't want to fix them - ignore them
	if($msg =~ m/^WARNING: line over \d+ characters/ or
			$msg =~ m/^WARNING: quoted string split across lines/ or
			$msg =~ m/^WARNING: Too many leading tabs - consider code refactoring/ or
			$msg =~ m/^WARNING: Avoid CamelCase: / or
			$msg =~ m{^ERROR: do not use C99 // comments}) {
		return 0;
	}

# Purely for debugging...
	print STDERR ("$rfn:$line: ($whref->{'tally'}) $msg\n",
	join("\n", @{$whref->{'lines'}}),"\n");
	if($whref->{'column'}) {
		print STDERR ("$whref->{'column'}\n");
	}

	# Now the ones we can handle

	if($msg =~ m/^ERROR: trailing whitespace/) {
		# cleanfile should clean these
		my $work = $outref->[$line] // $inref->[$line];
		if($work !~ m/\s+$/) {
			print STDERR "$rfn:$line: no trailing space to kill\n";
			return 0;
		}
		$work =~ s/\s+$/\n/;
		$outref->[$line]=$work;
		$handled=1;
	}

	elsif($msg =~ m/^ERROR: code indent should use tabs where possible/) {
		# cleanfile doesn't fix these.
		my $work = $outref->[$line] // $inref->[$line];
		if($work !~ m/ \s/) { # two spaces or space tab
			print STDERR "$rfn:$line: no indenting space to kill\n";
			return 0;
		}
		# Fix excessive spaces after known tab-aligned columns
		while($work =~ s/(^|\t)        /$1\t/) { $handled=1; }
		# This kills spaces at known tab-aligned columns before other tabs
		while($work =~ s/(^|\t) +\t/$1\t/) { $handled=1; }
		# The combination attempts to not mess up ascii art/tables.
		$outref->[$line]=$work if($handled);
	}

	elsif($msg =~ m/^WARNING: please, no space before tabs/) {
		# cleanfile will fix these, and do a better job.
		my $work = $outref->[$line] // $inref->[$line];
		if($work !~ m/ \t/) {
			# the above code indent fix may already have kicked in
			print STDERR "$rfn:$line: no pre-tab space to kill\n"
			if(!defined($outref->[$line]));
			return 0;
		}
		# tabify excessive spaces before tabs
		while($work =~ s/        \t/\t\t/) { $handled=1; }
		# kill or tabify short runs of spaces
		while($work =~ m/^(.*\t?)([^\t]*)( +)(\t.*)/s) {
			my $addtab = (length($2)%8+length($3))>=8 ? "\t" : "";
			$work = "$1$2$addtab$4";
			$handled=1;
		}
		# The combination attempts to not mess up ascii art/tables.
		$outref->[$line]=$work if($handled);
	}

	elsif($msg =~ m/^WARNING: please, no spaces at the start of a line/) {
		# cleanfile doesn't fix these.
		my $work = $outref->[$line] // $inref->[$line];
		if($work !~ m/^( +)/) {
			# code indent should use tabs may already have operated
			print STDERR "$rfn:$line: no leading space to kill\n"
			if(!defined($outref->[$line]));
			return 0;
		}
		my $spaces=length($1)%8;
		my $tabs=(length($1)-$spaces)/8;
		if(!$tabs) { $tabs=1; $spaces=0; }
		my $subst=("\t"x$tabs).(" "x$spaces);
		$work =~ s/^ +/$subst/;
		$outref->[$line] = $work;
		$handled=1;
	}

	elsif($msg =~ m{^WARNING: Use #include <linux/(\w+)\.h> instead of <asm/(\w+)\.h>}) {
		my $hunt="<asm/$1.h>";
		my $replace="<linux/$2.h>";
		my $work = $outref->[$line] // $inref->[$line];
		my $index=index($work, $hunt);
		if($index<0) {
			print STDERR "$rfn:$line: erronious asm/ include not found\n";
			return 0;
		}
		substr($work,$index,length($hunt)) = $replace;
		$outref->[$line] = $work;
		$handled=1;
	}

	elsif($msg =~ m/WARNING: braces {} are not necessary for single statement blocks/) {
		my $work = $outref->[$line] // $inref->[$line];
		my $work2 = $outref->[$line+$tail] // $inref->[$line+$tail];
		if($tail <= 1 or
				$work !~ m/\s*{\s*$/ or
				$work2 !~ m/^\s*}\s*$/) {
			print STDERR "$rfn:$line: cannot handle single statement braces\n";
			return 0;
		}
		$work =~ s/\s*{\s*$/\n/;
		$outref->[$line] = $work;
		$outref->[$line+$tail] = '';
		$handled=1;
	}

	elsif($msg =~ m/^ERROR: that open brace { should be on the previous line/) {
		my $work = $outref->[$line+$tail-1] // $inref->[$line+$tail-1];
		my $work2 = $outref->[$line+$tail] // $inref->[$line+$tail];
		if(!$tail or $work2 !~ m/^\s*\{\s*(\\?)$/) {
			print STDERR "$rfn:$line: cannot move open brace to previous line\n";
			return 0;
		}
		my $backslash=$1;
		if(!$backslash) {
			$work =~ s/\s*$/ {/;
		} else {
			$work =~ s/\s*(\\?)$/ { \\/;
		}
		$outref->[$line+$tail-1] = $work;
		$outref->[$line+$tail] = '';
		$handled=1;
	}

	elsif($msg =~ m/ERROR: return is not a function, parentheses are not required/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($tail or ($work !~ m/return\s*\((.*)\)\s*;/) or ($1 =~ m/[()]/)) {
			print STDERR "$rfn:$line: cannot simplify return\n";
			return 0;
		}
		$work =~ s/return\s*\((.*)\)\s*;/return $1;/;
		$outref->[$line] = $work;
		$handled=1;
	}

	elsif($msg =~ m/^ERROR: "foo \* bar" should be "foo \*bar"/) {
		my $work = $outref->[$line] // $inref->[$line];
		my @segments=split(/(\s+\*\s+)/, $work);
		if($tail or (scalar(@segments)>>1 != $whref->{'tally'})) {
			print STDERR "$rfn:$line: cannot fix $whref->{'tally'} 'foo * bar' instances in $work {", join(":", @segments), "}\n";
			return 0;
		}
		for(my $i=1; $i<scalar(@segments); $i+=2) {
			$segments[$i] = ' *';
		}
		$outref->[$line] = join('', @segments);
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/ERROR: spaces required around that '(\S+?)' \(ctx:(.x.)\)/) {
		# the column is given to us, if the line's otherwise unmodified
		# however, we can probably just work it out ourselves
		my $work = $outref->[$line] // $inref->[$line];
		my $op=$1;
		if($tail or !$whref->{'column'} or
				($work !~ m/(\S(\Q$op\E)\S?|\Q$op\E\S)/)) {
			print STDERR "$rfn:$line: cannot find '$op' without spacing\n";
			return 0;
		}
		my $opstart=$-[2] // $-[1]; # = start of op
		if(defined($2)) {
			# have no leading space, may have trailing space
			substr($work, $opstart, length($op)) = " $op" .
			((length($1)==length($op)+1) ? "" : " ");
		} else {
			# have leading space, can't have trailing space
			substr($work, $opstart, length($op)) = "$op ";
		}
		$outref->[$line]=$work;
		$handled=1;
	}

	elsif($msg =~ m/ERROR: space required before that '(\S+?)' \(ctx:(.)x.\)/) {
		# the column is given to us, if the line's otherwise unmodified
		# however, we can probably just work it out ourselves.
		# foo==-1 will be fixed by the above correction of '=='
		my $work = $outref->[$line] // $inref->[$line];
		my ($op, $prior)=($1,$2);
		if($tail or !$whref->{'column'} or ($work !~ m/\S(\Q$op\E)/)
				or ($work =~ m/\S\Q$op\E.*\S\Q$op\E/)) {
			print STDERR "$rfn:$line: cannot identify '$op' without spacing\n"
			if($prior ne 'O' and !defined($outref->[$line]));
			return 0;
		}
		# This is fragile, naively it will separate perfectly valid a->b.
		# Ensure that if we know we're after an operator, we don't even
		# think about changing a->b
		if($prior eq 'O') {
			$work =~ s{([-*/%+=])\Q$op\E}{$1 $op} and $handled=1;
		} else {
			$work =~ s/(\S)\Q$op\E/$1 $op/ and $handled=1;
		}
		# We really should use the column info we're given.
		$outref->[$line]=$work if($handled);
	}

	if($msg =~ m/^ERROR: space prohibited before that '(\S+)' \(ctx:(.x.)\)/) {
		# the column is given to us, if the line's otherwise unmodified
		# however, we can probably just work it out ourselves.
		my $work = $outref->[$line] // $inref->[$line];
		my $op=$1;
		if($tail or !$whref->{'column'} or ($work !~ m/\s+\Q$op\E/) or
				($work =~ m/\s+\Q$op\E.*?\s+\Q$op\E/)) {
			print STDERR "$rfn:$line: cannot identify '$op' with spacing\n";
			return 0;
		}
		$work =~ s/\s+\Q$op\E/$op/;
		$outref->[$line]=$work;
		$handled=1;
	}

	elsif($msg =~ m/^ERROR: space prohibited after that '(\S+)' \(ctx:(.x.)\)/) {
		# the column is given to us, if the line's otherwise unmodified
		# however, we can probably just work it out ourselves.
		my $work = $outref->[$line] // $inref->[$line];
		my $op=$1;
		if($tail or !$whref->{'column'} or ($work !~ m/\Q$op\E\s+/) or
				($work =~ m/\Q$op\E\s+.*?\Q$op\E\s+/)) {
			print STDERR "$rfn:$line: cannot identify '$op' with spacing\n";
			return 0;
		}
		$work =~ s/\Q$op\E\s+/$op/;
		$outref->[$line]=$work;
		$handled=1;
	}

	elsif($msg =~ m/^WARNING: space prohibited before semicolon/) {
		my $work = $outref->[$line] // $inref->[$line];
		my @segments = split(/(\s+;)/, $work);
		if($tail or (scalar(@segments)>>1 != $whref->{'tally'})) {
			# FIXME: Alas this fails to correct for(... ; ... ; ...)
			# which only only one checkpatch warning.
			print STDERR "$rfn:$line: cannot fix $whref->{'tally'} semicolons"
					. " in $work {", join(":", @segments), "}\n";
			return 0;
		}
		for(my $i=1; $i<scalar(@segments); $i+=2) {
			# Don't squish ';'s next to comment ends
			$segments[$i] = ';' if($segments[$i-1] !~ m{\*/});
		}
		$outref->[$line] = join('', @segments);
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/^WARNING: space prohibited between function name and open parenthesis '\('/) {
		my $work = $outref->[$line] // $inref->[$line];
		my @segments = split(/(\b\s+\()/, $work);
		# Try first, bail later
		my $notkeyword=0;
		for(my $i=1; $i<scalar(@segments); $i+=2) {
			if($segments[$i-1] !~ m/\b(?:if|while|for|switch)/) {
				$segments[$i] = '(';
				++$notkeyword;
			}
		}
		# Did that work?
		if($tail or ($notkeyword != $whref->{'tally'})) {
			print STDERR "$rfn:$line: cannot fix $whref->{'tally'} function calls"
					. " in $work {", join(":", @segments), "}\n";
			return 0;
		}
		$outref->[$line] = join('', @segments);
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/^ERROR: switch and case should be at the same indent/) {
		my $switch_len=0;
		my $switch_indent=0;
		my $case_len=0;
		my $case_indent=0;
		my $diff=0;
		my $j=0;

		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ m/\bswitch/){
			($switch_len, $switch_indent) = line_stats($work);
		}

		for(my $i=1; $i<=$tail+2; ++$i) {
			$work = $outref->[$line + $i] // $inref->[$line + $i];
			($case_len, $case_indent) = line_stats($work);
			if($work =~ m/\b(case|default:)/) {
				$diff = $case_indent - $switch_indent;
			}
			my @segments = split(/\t/, $work);

			$work = "";
			for($j=1; $j<=($case_indent-$diff)/8+1; ++$j) {
				$work .= "\t";
			}
			for($j=1; $j<scalar(@segments); ++$j) {
				$work .= $segments[$j];
			}
			$outref->[$line + $i] = $work;
		}
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/^ERROR: do not initialise globals to 0 or NULL/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /\s=/) {
			$outref->[$line] = $`.";\n";
		} elsif($work =~ /=/) {
			$outref->[$line] = $`.";\n";
		}
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/^ERROR: do not initialise statics to 0 or NULL/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /\s=/) {
			$outref->[$line] = $`.";\n";
		} elsif ($work =~ /=/) {
			$outref->[$line] = $`.";\n";
		}
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/ERROR: open brace '{' following function declarations go on the next line/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /\s{/) {
			$outref->[$line] = $`."\n{".$';
		} elsif($work =~ /{/) {
			$outref->[$line] = $`.";\n{".$';
		}
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/ERROR: open brace '{' following enum go on the same line/ ||
			$msg =~ m/ERROR: open brace '{' following union go on the same line/ ||
			$msg =~ m/ERROR: open brace '{' following struct go on the same line/) {
		my $work = $outref->[$line-1] // $inref->[$line-1];
		$work =~ s/\n/ /;
		$outref->[$line-1] = $work;
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/ERROR: space required after that close brace '}'/) {
		my $work = $outref->[$line] // $inref->[$line];
		$work =~ /}/;
		$outref->[$line] = $`.$&." ".$';
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/ERROR: space required before the open parenthesis '\('/) {
		my $work = $outref->[$line] // $inref->[$line];
		$work =~ /\(/;
		$outref->[$line] = $`." ".$&.$';
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/ERROR: trailing statements should be on next line/) {
		my $len=0;
		my $indent=0;
		my $tab="";
		my $work = $outref->[$line] // $inref->[$line];
		($len, $indent) = line_stats($work);

		if($work =~ /\}\s*if/) {
			for(my $i=2; $i<=$indent/8+2; ++$i) {
				$tab .= "\t";
			}
			$outref->[$line] = $`."}\n".$tab."if".$';
		} elsif($work =~ /(?<c>(case)\s*.\:)|(?<d>(default)\:)/){
			for(my $i=1; $i<=$indent/8+2; ++$i) {
				$tab .= "\t";
			}
			$outref->[$line] = $`."$+{c}$+{d}\n".$tab.$';
		} elsif($work =~ /\)\s/) {
			for(my $i=1; $i<=$indent/8+2; ++$i) {
				$tab .= "\t";
			}
			$outref->[$line] = $`.")\n".$tab.$';
		} elsif($work =~ /\)/) {
			for(my $i=1; $i<=$indent/8+2; ++$i) {
				$tab .= "\t";
			}
			$outref->[$line] = $`.")\n".$tab.$';
		} elsif($work =~ /(else)\s/){
			for(my $i=1; $i<=$indent/8+2; ++$i) {
				$tab .= "\t";
			}
			$outref->[$line] = $`."else\n".$tab.$';
		}
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/ERROR: else should follow close brace '}'/) {
		my $work = $outref->[$line-1] // $inref->[$line-1];
		my $work2 = $outref->[$line] // $inref->[$line];
		$work2 =~ s/^\s+//;
		my $backslash=$1;
		if(!$backslash) {
			$work =~ s/\s*$/ $work2/;
		} else {
			$work =~ s/\s*(\\?)$/ $work2 \\/;
		}
		$outref->[$line-1] = $work;
		$outref->[$line+$tail-1] = "\n";
		$handled=1;
	}

	elsif($msg =~ m/ERROR: while should follow close brace '}'/) {
		my $work = $outref->[$line-1] // $inref->[$line-1];
		my $tmp_str = $outref->[$line] // $inref->[$line];
		my $backslash=$1;

		$tmp_str =~ s/\s*$//;

		if(!$backslash) {
			$work =~ s/\s*$/$tmp_str/;
		} else {
			$work =~ s/\s*(\\?)$/$tmp_str\\/;
		}

		$tmp_str =~ s/\s*$//;
		$outref->[$line-1] = $work;
		$outref->[$line+$tail-1] = "\n";
		$handled=1;
	}

	elsif($msg =~ m/^WARNING: unnecessary whitespace before a quoted newline/) {
		my $work = $outref->[$line] // $inref->[$line];
		$work =~ /\s+\\n/;
		$outref->[$line] = $`.$';
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/^WARNING: adding a line without newline at end of file/) {
		my $work = $outref->[$line] // $inref->[$line];
		$outref->[$line] = $work."\n\n";
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/Using\s(__dev(init|exit)(data|const|))\sis unnecessary/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /\s(__dev(init|exit)(data|const|))\b/) {
			$outref->[$line] = $`.$';
			$handled=$whref->{'tally'};
		}
	}

	elsif($msg =~ m/^WARNING: static const char \* array should probably be static const char \* const/) {
		my $work = $outref->[$line] // $inref->[$line];
		$work =~ /\bstatic\s+const\s+char\s*/;
		$outref->[$line] = $`.$&."const ".$';
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/^WARNING: static char array declaration should probably be static const char/) {
		my $work = $outref->[$line] // $inref->[$line];
		$work =~ /\bstatic\s+char\s*/;
		$outref->[$line] = $`.$&."const ".$';
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/^WARNING: Prefer pr_warn\(... to pr_warning\(.../) {
		my $work = $outref->[$line] // $inref->[$line];
		$work =~ /\bpr_warning/;
		$outref->[$line] = $`."pr_warn".$';
		$handled=$whref->{'tally'};
	}

	elsif($msg =~ m/WARNING: braces {} are not necessary for any arm of this statement/) {
		my $work = $outref->[$line] // $inref->[$line];
		my $work2 = $outref->[$line+$tail] // $inref->[$line+$tail];
		if($tail <= 1 or
				$work !~ m/\s*{\s*$/ or
				$work2 !~ m/^\s*}\s*$/) {
			print STDERR "$rfn:$line: cannot handle single statement braces\n";
			return 0;
		}
		$work =~ s/\s*{\s*$/\n/;
		$outref->[$line] = $work;
		$outref->[$line+$tail] = '';
		$handled=1;
	}

	elsif($msg =~ m/^WARNING: plain inline is preferred over __inline/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /\b__inline__/) {
			$outref->[$line] = $`."inline".$';
			$handled=$whref->{'tally'};
		}
		elsif($work =~ /\b__inline/) {
			$outref->[$line] = $`."inline".$';
			$handled=$whref->{'tally'};
		}
	}

	elsif($msg =~ m/^WARNING: __packed is preferred over __attribute__\(\(packed\)\)/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /__attribute__\(\(packed\)\)/) {
			$outref->[$line] = $`."__packed".$';
			$handled=$whref->{'tally'};
		}
	}

	elsif($msg =~ m/^WARNING: __aligned\(size\) is preferred over __attribute__\(\(aligned\(size\)\)\)/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /__attribute__\(\(aligned\(/) {
			$work = $`."__aligned(".$';
			if($work =~ /\)\)\)/) {
				$outref->[$line] = $`.")".$';
				$handled=$whref->{'tally'};
			}
		}
	}

	elsif($msg =~ m/^WARNING: struct spinlock should be spinlock_t/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /struct spinlock/) {
			$outref->[$line] = $`."spinlock_t".$';
			$handled=$whref->{'tally'};
		}
	}

	elsif($msg =~ m/^WARNING: Statements terminations use 1 semicolon/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /\;+/) {
			$outref->[$line] = $`.";".$';
			$handled=$whref->{'tally'};
		}
	}

	elsif($msg =~ m/^WARNING: __func__ should be used instead of gcc specific __FUNCTION__/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /__FUNCTION__/) {
			$outref->[$line] = $`."__func__".$';
			$handled=$whref->{'tally'};
		}
	}

	elsif($msg =~ m/^WARNING: please use device_initcall\(\) instead of __initcall\(\)/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /__initcall/) {
			$outref->[$line] = $`."__device_initcall".$';
			$handled=$whref->{'tally'};
		}
	}

	elsif($msg =~ m/^WARNING: struct \w+ should normally be const/) {
		my $work = $outref->[$line] // $inref->[$line];
		if($work =~ /struct/) {
			$outref->[$line] = $`."const struct".$';
			$handled=$whref->{'tally'};
		}
	}

	elsif($msg =~ m/^WARNING: \%Ld\/\%Lu are not-standard C, use \%lld\/\%llu/) {
		my $work = $outref->[$line] // $inref->[$line];
		$work =~ s/\%Lu/\%llu/g;
		$work =~ s/\%Ld/\%lld/g;
		$outref->[$line] = $work;
		$handled=$whref->{'tally'};
	}

	# Purely for debugging, let's see before and after
	if($handled) {
		print STDERR ("CHANGED:\n");
		for(my $i=0; $i<=$tail; ++$i) {
			print STDERR
					(defined($outref->[$line+$i])?"-":" ",
					$inref->[$line+$i]);
		}
		for(my $i=0; $i<=$tail; ++$i) {
			print STDERR
					(!defined($outref->[$line+$i]) ? " $inref->[$line+$i]"
					: !length($outref->[$line+$i]) ? ""
					: "+$outref->[$line+$i]");
		}
		print("\n");
	}
	return $handled;
}

sub process_file($)
{
	my $fn=$_[0];
	my $rfn=$aliases{$fn};
	my @inp=(undef);
	my @outp=(0);

	open(FH, "<$rfn") or die("can't open $rfn");
	while(<FH>) { push @inp, $_; }
	close(FH);
	my $changed=0;
	my $aref=$files{$fn};
	foreach my $whref (@$aref) {
		if(!msg_is_OK($whref->{'message'})) {
			next;
		}
		my $tally=$whref->{'tally'};
		while($tally>0) {
			my $handled=handle_message($rfn, $whref, \@inp, \@outp);
			last if (!$handled);
			$changed+=$handled;
			$tally-=$handled;
		}
	}
	if($changed && !$dryrun) {
		my $tmpname="$rfn.tmp.$$.bocp";
		open(FH, ">$tmpname") or die("can't open $tmpname");
		for(my $i=1; $i<=$#inp; ++$i) {
			print FH ($outp[$i] // $inp[$i]);
		}
		close(FH);
		if(!-s $tmpname) { die("couldn't write $tmpname"); }
		rename($tmpname, $rfn);
	}
}

# Now actually do the work...
foreach my $fn (@files) {
	process_file($fn);
}

sub expand_tabs {
	my ($str) = @_;

	my $res = '';
	my $n = 0;
	for my $c (split(//, $str)) {
		if ($c eq "\t") {
			$res .= ' ';
			$n++;
			for (; ($n % 8) != 0; $n++) {
				$res .= ' ';
			}
			next;
		}
		$res .= $c;
		$n++;
	}

	return $res;
}

sub line_stats {
	my ($line) = @_;

	# Drop the diff line leader and expand tabs
	$line =~ s/^.//;
	$line = expand_tabs($line);

	# Pick the indent from the front of the line.
	my ($white) = ($line =~ /^(\s*)/);

	return (length($line), length($white));
}

