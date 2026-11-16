#!/bin/sh
# check_archive_identity.sh AR RANLIB
#
# The host builds of usr.bin/ar and usr.bin/ranlib record, under a
# well-formed SOURCE_DATE_EPOCH, a date bounded by that value, user 0,
# group 0 and mode 0644 or 0755 for every member and for the symbol table,
# so equal members give equal archive bytes whoever builds them. The check
# archives the same two files under two umasks and two modification times
# and, where the host grants an unprivileged user namespace (unshare -r),
# under a second user id, and requires identical bytes and the recorded
# header fields. The same builds without SOURCE_DATE_EPOCH must differ: that
# run shows the comparison sees the builder's umask in the header.
#
# Output: one PASS line naming the compared identities, or FAIL lines on
# standard error and exit status 1. A host without unshare -r reports the
# second user id as SKIP and compares the remaining identities.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: check_archive_identity.sh AR RANLIB" >&2
    exit 2
fi
case $1 in /*) ar=$1 ;; *) ar=$PWD/$1 ;; esac
case $2 in /*) ranlib=$2 ;; *) ranlib=$PWD/$2 ;; esac
epoch=1700000000

work=$(mktemp -d "${TMPDIR:-/tmp}/discobsd-archive-identity.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cd "$work"

cat > build.sh <<'EOF'
#!/bin/sh
# build.sh AR RANLIB OUTPUT MASK STAMP: archive two members created under
# umask MASK with modification time STAMP (touch -t form)
set -eu
umask "$4"
rm -rf "members.$$"
mkdir "members.$$"
printf 'first member\n' > "members.$$/one.o"
printf 'second member, executable\n' > "members.$$/two.o"
chmod +x "members.$$/two.o"
touch -t "$5" "members.$$/one.o" "members.$$/two.o"
rm -f "$3"
(cd "members.$$" && "$1" cr "../$3" one.o two.o)
"$2" "$3"
rm -rf "members.$$"
EOF

fail=0
SOURCE_DATE_EPOCH=$epoch sh build.sh "$ar" "$ranlib" first.a 022 202401010000
SOURCE_DATE_EPOCH=$epoch sh build.sh "$ar" "$ranlib" second.a 007 202406150000
identities="two umasks, two member dates"
if ! cmp -s first.a second.a; then
    echo "FAIL archive-identity: umask or member date reaches the archive" >&2
    fail=1
fi

if unshare -r true 2>/dev/null &&
    [ "$(unshare -r id -u)" != "$(id -u)" ]; then
    SOURCE_DATE_EPOCH=$epoch unshare -r \
        sh build.sh "$ar" "$ranlib" third.a 022 202401010000
    identities="$identities, two user ids"
    if ! cmp -s first.a third.a; then
        echo "FAIL archive-identity: the builder's user id reaches the archive" >&2
        fail=1
    fi
else
    echo "SKIP archive-identity second user id: unshare -r unavailable"
fi

# Header fields of the symbol table and both members, as ar_hdr lays them
# out: name, date, user id, group id, mode.
headers=$(LC_ALL=C grep -a -o \
    '[_.A-Za-z][_.A-Za-z0-9]*  *[0-9]\{10\}  *[0-9][0-9]*  *[0-9][0-9]*  *[0-7][0-7]* ' \
    first.a | tr -s ' ' | sed 's/ $//')
expected="__.SYMDEF $((epoch + 3)) 0 0 644
one.o $epoch 0 0 100644
two.o $epoch 0 0 100755"
if [ "$headers" != "$expected" ]; then
    echo "FAIL archive-identity: header fields are" >&2
    echo "$headers" >&2
    fail=1
fi

# Calibration: with the variable absent the two umasks must show. The build
# exports the variable to every recipe, so it is removed here by name.
(
    unset SOURCE_DATE_EPOCH
    sh build.sh "$ar" "$ranlib" plain-first.a 022 202401010000
    sh build.sh "$ar" "$ranlib" plain-second.a 007 202401010000
)
if cmp -s plain-first.a plain-second.a; then
    echo "FAIL archive-identity: archives agree without SOURCE_DATE_EPOCH," \
        "so the comparison does not see the header" >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
echo "PASS archive-identity: equal bytes across $identities"
