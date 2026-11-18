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

# A fixed UTC date makes version payloads comparable across clean builds.
# date(1) uses -d on GNU systems and -r on BSD and macOS.
# The version string carries a date. A caller's SOURCE_DATE_EPOCH selects
# it; otherwise the committer date of HEAD does, so two builds of one commit
# produce the same vers.c. Outside a git checkout the date is the build time.
if [ "${SOURCE_DATE_EPOCH+x}" != x ]; then
    HEAD_EPOCH=$(git log -1 --format=%ct 2>/dev/null) || HEAD_EPOCH=""
    case "$HEAD_EPOCH" in
        ''|*[!0-9]*) ;;
        *) SOURCE_DATE_EPOCH=$HEAD_EPOCH ;;
    esac
fi

if [ "${SOURCE_DATE_EPOCH+x}" = x ]; then
    case "$SOURCE_DATE_EPOCH" in
        ''|*[!0-9]*) echo "invalid SOURCE_DATE_EPOCH" >&2; exit 1 ;;
    esac
    if LC_ALL=C TZ=UTC date -u -d "@0" >/dev/null 2>&1; then
        NOW=$(LC_ALL=C TZ=UTC date -u -d "@$SOURCE_DATE_EPOCH" 2>/dev/null) || {
            echo "SOURCE_DATE_EPOCH is outside the supported date range" >&2
            exit 1
        }
    else
        NOW=$(LC_ALL=C TZ=UTC date -u -r "$SOURCE_DATE_EPOCH" 2>/dev/null) || {
            echo "SOURCE_DATE_EPOCH is outside the supported date range" >&2
            exit 1
        }
    fi
else
    NOW=$(date) || exit 1
fi

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

# The revision is the commit count of the repository rooted at the source
# tree this script belongs to. A source export has none, and one unpacked
# beneath an unrelated work tree must not report that tree's count: only
# that absence selects 0, the value share/mk/sys.mk gives BUILD there. A
# repository that is present and cannot be counted stops the script with
# git's diagnostic, so a damaged checkout never reads as an export. The
# variables that select another repository are cleared for the count.
TOP=`dirname "$0"`/../..
if [ -e "$TOP/.git" ]
then
    GITREV=`(unset GIT_DIR GIT_WORK_TREE GIT_COMMON_DIR GIT_INDEX_FILE
        git -C "$TOP" rev-list --count HEAD --)` || exit 1
else
    GITREV="0"
fi

if [ "x$GITREV" = "x" ]
then
    echo "newvers.sh: empty commit count" >&2
    exit 1
fi

if [ "x$GITREV" != "x$OV" ]
then
    CV=1
fi
echo $CV >.compileversion
echo $GITREV >.oldversion

ID=`basename "${PWD}"`

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
