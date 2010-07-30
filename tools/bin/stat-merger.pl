#!/usr/bin/perl

%stat_list = ();

while($line = <STDIN>)
{
    if($line =~ /xtm/)
    {
        $line =~ s/\n//;
        @words = split(' ', $line);
        #print "$line ==> @words[0] -> @words[1]\n";
        if(!exists($stat_list{@words[0]}))
        {
            $stat_list{@words[0]} = @words[1];
        }
        else
        {
            $stat_list{@words[0]} += @words[1];
        }
    }
}

foreach $stat_name (keys %stat_list)
{
    print "$stat_name $stat_list{$stat_name}\n";
}
