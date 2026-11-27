#!/bin/sh
#
#	@(#)updatedb.csh	4.6.1	(2.11BSD)	1996/10/23
#
set -eu

SRCHPATHS="/"			# directories to be put in the database
LIBDIR=/usr/libexec		# for subprograms
FINDHONCHO=root			# for error messages
FCODES=/var/db/find.codes	# the database

PATH=$LIBDIR:/bin
umask 077
work=
for suffix in 0 1 2 3 4 5 6 7 8 9
do
	candidate=/tmp/updatedb.$$.$suffix
	if mkdir "$candidate" 2>/dev/null
	then
		work=$candidate
		break
	fi
done
if [ -z "$work" ]
then
	echo 'updatedb: cannot create private temporary directory' >&2
	exit 1
fi
bigrams=$work/bigrams
filelist=$work/list
errs=$work/errors
trap 'rm -f "$bigrams" "$filelist" "$errs"; rmdir "$work" 2>/dev/null || :' 0 1 2 3 15

# Make a file list and compute common bigrams.
# Alphabetize '/' before any other char with 'tr'.
# If the system is very short of sort space, 'bigram' can be made
# smarter to accumulate common bigrams directly without sorting
# ('awk', with its associative memory capacity, can do this in several
# lines, but is too slow, and runs out of string space on small machines).

nice +10
find ${SRCHPATHS} -print | tr '/' '\001' | \
   (set +e; sort -f; status=$?; echo "$status" > "$errs"; exit "$status") | \
   tr '\001' '/' > "$filelist"
$LIBDIR/bigram < "$filelist" | \
   (set +e; sort; status=$?; echo "$status" >> "$errs"; exit "$status") | \
   uniq -c | sort -nr | \
   awk '{ if (NR <= 128) print $2 }' | tr -d '\012' > "$bigrams"

# code the file list

if grep -s -v 0 "$errs"
then
	echo 'squeeze error: out of sort space' | mail $FINDHONCHO
else
	$LIBDIR/code "$bigrams" < "$filelist" > "$FCODES"
	chmod 644 "$FCODES"
fi
