#!/bin/sh
# Auto fix the errors of coding rule checker
# Copyright (C) 2013, Han Jun-Yeong <junyeong.han@lge.com>

echo "$1"
result="checkpatch.pl_result"

perl scripts/checkpatch.pl --fix -f $1 > $result

fixedfilename=$1".EXPERIMENTAL-checkpatch-fixes"
if [ -f $fixedfilename ]
then
	mv $fixedfilename $1
	perl scripts/checkpatch.pl -f $1 > $result
fi

perl scripts/robopatch.pl $result
rm $result
