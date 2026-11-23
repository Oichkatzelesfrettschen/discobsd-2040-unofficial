#!/bin/sh -
#
# Copyright (c) 1980 Regents of the University of California.
# All rights reserved.  The Berkeley software License Agreement
# specifies the terms and conditions for redistribution.
#

# Things which need version number upgrades:
#	sys/conf/newvers.sh:
#		OSR
#		OSRANK
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

OST="GizmoBSD"
OSR="0.1.0-alpha"

# kern.osrev is generated from OSR and OSRANK, the one version source, and
# is never edited by hand. OSR reads MAJOR.MINOR.PATCH with an optional
# -LABEL of lower-case letters, such as 0.1.0-alpha or 0.1.0. OSRANK names the
# label of OSR and the digit that ranks its stage, such as alpha=1, and is
# empty while OSR has no label. A named stage takes a digit from 1 to 8 that
# orders it inside its release, and a release without a label takes 9, so
# every stage packs below its release. The owner picks the digit when a stage
# is named; no tool holds a list of stages. The packed value is
#	MAJOR * 100000000 + MINOR * 1000000 + PATCH * 10 + STAGE
# with MAJOR at most 20, MINOR at most 99 and PATCH at most 99999, which
# keeps the largest value, 2099999999, inside a signed 32-bit long.
# A rejected OSR or OSRANK stops the script before it writes anything, so
# the kernel link keeps the vers.c of the last good build, and the checks
# vers.c repeats as _Static_assert fail the compile of an edited one.
OSRANK="alpha=1"

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

osrev_fail()
{
	echo "newvers.sh: $*" >&2
	exit 1
}

# $1 names a field of OSR, $2 holds its text and $3 is its largest value.
# The text is checked as digits without a leading zero and at most six
# of them before the shell reads it as a number, so no field is read as
# octal and none overflows the arithmetic below.
osrev_field()
{
	case "$2" in
	''|*[!0-9]*) osrev_fail "OSR $1 \"$2\" is not a decimal number" ;;
	0?*) osrev_fail "OSR $1 \"$2\" has a leading zero" ;;
	esac
	[ ${#2} -le 6 ] || osrev_fail "OSR $1 \"$2\" has more than six digits"
	[ "$2" -le "$3" ] || osrev_fail "OSR $1 $2 exceeds $3"
}

case "$OSR" in
*-)	osrev_fail "OSR \"$OSR\" ends in a hyphen" ;;
*-*)	OSREV_CORE=${OSR%%-*}
	OSREV_LABEL=${OSR#*-} ;;
*)	OSREV_CORE=$OSR
	OSREV_LABEL= ;;
esac
case "$OSREV_LABEL" in
*[!a-z]*) osrev_fail "OSR label \"$OSREV_LABEL\" is not lower-case letters" ;;
esac
set -f
OSREV_IFS=$IFS
IFS=.
set -- $OSREV_CORE
IFS=$OSREV_IFS
set +f
[ $# -eq 3 ] || osrev_fail "OSR \"$OSR\" is not MAJOR.MINOR.PATCH"
osrev_field major "$1" 20
osrev_field minor "$2" 99
osrev_field patch "$3" 99999
OSREV_MAJOR=$1
OSREV_MINOR=$2
OSREV_PATCH=$3
if [ -z "$OSREV_LABEL" ]
then
	[ -z "$OSRANK" ] || osrev_fail "OSRANK \"$OSRANK\" ranks a label and OSR has none"
	OSREV_STAGE=9
else
	case "$OSRANK" in
	"$OSREV_LABEL"=[1-8]) OSREV_STAGE=${OSRANK#*=} ;;
	*) osrev_fail "OSRANK \"$OSRANK\" is not \"$OSREV_LABEL=DIGIT\" with a digit from 1 to 8" ;;
	esac
fi
OSREV_PACKED=$((OSREV_MAJOR * 100000000 + OSREV_MINOR * 1000000 + OSREV_PATCH * 10 + OSREV_STAGE))

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
}'

# The release string and the packed number come from the fields above and
# agree at compile time: each _Static_assert fails the build of a vers.c
# whose macros were edited apart from the others.
OSREV_PRE=
[ -z "$OSREV_LABEL" ] || OSREV_PRE="-$OSREV_LABEL"
cat <<EOF
#define OSREV_MAJOR	$OSREV_MAJOR
#define OSREV_MINOR	$OSREV_MINOR
#define OSREV_PATCH	$OSREV_PATCH
#define OSREV_STAGE	$OSREV_STAGE
#define OSREV_PRE	"$OSREV_PRE"
#define OSREV_PACKED	${OSREV_PACKED}L
#define OSREV_STRING	"$OSR"
#define OSREV_XSTR(x)	#x
#define OSREV_STR(x)	OSREV_XSTR(x)
_Static_assert(OSREV_MAJOR >= 0 && OSREV_MAJOR <= 20, "OSREV major exceeds 20");
_Static_assert(OSREV_MINOR >= 0 && OSREV_MINOR <= 99, "OSREV minor exceeds 99");
_Static_assert(OSREV_PATCH >= 0 && OSREV_PATCH <= 99999, "OSREV patch exceeds 99999");
_Static_assert(OSREV_STAGE >= 1 && OSREV_STAGE <= 9, "OSREV stage is not from 1 to 9");
_Static_assert((sizeof(OSREV_PRE) == 1) == (OSREV_STAGE == 9),
    "OSREV stage disagrees with the label");
_Static_assert(OSREV_PACKED == OSREV_MAJOR * 100000000L + OSREV_MINOR * 1000000L +
    OSREV_PATCH * 10L + OSREV_STAGE, "OSREV packed value disagrees with its fields");
_Static_assert(__builtin_strcmp(OSREV_STRING, OSREV_STR(OSREV_MAJOR) "." OSREV_STR(OSREV_MINOR) "."
    OSREV_STR(OSREV_PATCH) OSREV_PRE) == 0, "OSREV string disagrees with its fields");
const char osrelease[] = OSREV_STRING;
const long osrev = OSREV_PACKED;
EOF
