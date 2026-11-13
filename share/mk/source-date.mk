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
.if !defined(SOURCE_DATE_EPOCH) || empty(SOURCE_DATE_EPOCH)
SOURCE_DATE_EPOCH!=	git log -1 --format=%ct 2>/dev/null || true
.endif
.if !empty(SOURCE_DATE_EPOCH)
.export SOURCE_DATE_EPOCH
.endif
