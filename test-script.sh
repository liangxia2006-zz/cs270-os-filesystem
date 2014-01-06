#!/bin/bash

# run this script from inside the FS - all paths are relative to the current directory
set -x

# create a dir, cd into it
# cd ~ec2-user/jasen/CS270FS/tmp
mkdir tdir1
cd tdir1
pwd

# write to existing file
touch tfile1
echo test test test > tfile1
cat tfile1

# write to new file
echo test test test test > tfile2
cat tfile2

# make sure deleted files are really deleted
echo rm me > rmfile1
cat rmfile1
rm rmfile1
touch rmfile1
cat rmfile1

# test nested directories
mkdir tdir2
cd tdir2
BASENAME="mfile"
for (( i=0 ; i < 10 ; i++ ))
do
	echo test data > $BASENAME$i
done
mkdir tdir3
cd tdir3
for (( i=0 ; i < 10 ; i++ ))
do
	echo more test data > $BASENAME$i
done
# cd ~ec2-user/jasen/CS270FS/tmp
cd ../../..
rm -rf tdir1

# test mkdir -p 
mkdir -p this/is/a/nice/test
cd this/is/a/nice/test
pwd

# cleanup
cd ../../../../..
rm -rf this

# profit
echo Testing complete.
