#!/usr/bin/perl

@stat_list = ();

while($line = <STDIN>)
{
    $line =~ s/\n//;
    @words = split(' ', $line);
    #print "$line ==> @words[0] -> @words[1]\n";
    push(@stat_list, [@words]);
}

@stat_list_sorted = reverse sort {${$a}[1] <=> ${$b}[1]} @stat_list;

foreach $line (@stat_list_sorted)
{
    print "${$line}[0] ${$line}[1]\n";
}
