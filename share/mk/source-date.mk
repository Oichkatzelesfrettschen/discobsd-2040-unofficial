# The date a build records in its products.
#
# A caller's SOURCE_DATE_EPOCH stands; otherwise the committer date of HEAD
# is exported, so two builds of one commit record the same date. Its readers:
# GCC and Clang expand __DATE__ and __TIME__ from it (usr.bin/pdc prints its
# compile time); the host ar and ranlib bound archive member and table dates
# by it; tools/fsutil bounds every time in the root image by it; the root
# Makefile dates /etc/release from it; and sys/conf/newvers.sh applies the
# same default to the kernel version string. Outside a git checkout the
# variable stays unset and each reader uses the build time.
#
# FSUTIL_CLOCK, read by tools/fsutil alone, replaces that bound with a fixed
# clock: with it set to a count of seconds from 0 to 4294967295, every time
# in the root image is that count, whatever SOURCE_DATE_EPOCH holds. It builds
# an image dated after January 2038 (2415919104 is 0x90000000) for a boot
# test. The variable is for test images: the ar and ranlib dates, the release
# stamp and the kernel version keep SOURCE_DATE_EPOCH, so a file in an image
# built with it can be newer than the table date of an archive.
.if !defined(SOURCE_DATE_EPOCH) || empty(SOURCE_DATE_EPOCH)
SOURCE_DATE_EPOCH!=	git log -1 --format=%ct 2>/dev/null || true
.endif
.if !empty(SOURCE_DATE_EPOCH)
.export SOURCE_DATE_EPOCH
.endif
