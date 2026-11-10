#!/bin/sh -
#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#

# Things which need version number upgrades:
#	sys/conf/newvers.sh:
#		OSR
#	sys/sys/param.h:
#		DiscoBSD
#		DiscoBSD_X_X
#	share/mk/sys.mk:
#		OSMAJOR
#		OSMINOR
#
# After a release, move to S="-current" and keep same version number.
# Before a release, move to S="-beta" and increment version number.
# For a release, move to S="" and keep same version number.

OST="DiscoBSD"
OSR="2.7"

#S="-current"
#S="-beta"
S=""

if [ ! -r .compileversion -o ! -s .compileversion ]
then
    echo 0 >.compileversion
fi
CV=`cat .compileversion`
CV=`expr $CV + 1`

if [ ! -r .oldversion -o ! -s .oldversion ]
then
    echo 0 >.oldversion
fi
OV=`cat .oldversion`

GITREV=`git rev-list --count HEAD --`

if [ "x$GITREV" = "x" ]
then
    GITREV="Untracked"
fi

if [ "x$GITREV" != "x$OV" ]
then
    CV=1
fi
echo $CV >.compileversion
echo $GITREV >.oldversion

ID=`basename "${PWD}"`

# The date comes from date(1): strftime() is a gawk extension that the
# awk on macOS and the BSDs does not have.
NOW=`date`

echo $GITREV $CV $OST $OSR $ID $S| \
awk -v date="$NOW" '{
    gitrev = $1;
    cv = $2;
    ost = $3;
    osr = $4;
    id = toupper($5);
    status = $6;
    printf "const char version[] = \"%s %s%s (%s) #%d %s: %s\\n\";\n", \
        ost, osr, status, id, cv, gitrev, date;
    printf "const char ostype[] = \"%s\";\n", ost;
    printf "const char osversion[] = \"%s#%d\";\n", id, cv;
    printf "const char osrelease[] = \"%s\";\n", osr;
}'
