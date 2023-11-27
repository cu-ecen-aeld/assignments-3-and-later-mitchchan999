#!/bin/sh
# Script for finding text in files
# Author: Dan Macumber

set -e
set -u

FILESDIR=""
SEARCHSTR=""

if [ $# -lt 2 ];
then
	echo "Usage: finder.sh filesdir searchstr"
	exit 1
else
	FILESDIR=$1
	SEARCHSTR=$2
fi

if [[ ! -d "$FILESDIR" ]];
then
	echo "Directory '$FILESDIR' does not exist"
	exit 1
fi

FILECOUNT=0
LINECOUNT=0
for FILENAME in $FILESDIR/*; do
  if [[ -d "$FILENAME" ]]; then
    # skip directory entries
    continue
  fi
  FILECOUNT=$((FILECOUNT+1))
  COUNT=$(grep -c "$SEARCHSTR" "$FILENAME" || true)
  LINECOUNT=$((LINECOUNT+COUNT))
done

echo "The number of files are $FILECOUNT and the number of matching lines are $LINECOUNT"
exit 0