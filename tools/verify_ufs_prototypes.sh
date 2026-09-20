#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 KERNEL_BUILD" >&2
    exit 2
fi

kernel_build=$(cd "$1" && pwd)
make_command=${MAKE:-bmake}

make_value()
{
    # The verifier owns these read-only sub-makes after the parent closes its
    # jobserver descriptors, so inherited -J flags cannot name usable pipes.
    env -u MAKEFLAGS -u MFLAGS "$make_command" -V "$1"
}

source_files='ufs_alloc.c ufs_bio.c ufs_bmap.c ufs_dsort.c ufs_fio.c
ufs_inode.c ufs_mount.c ufs_namei.c ufs_subr.c ufs_syscalls.c
ufs_syscalls2.c vfs_vnops.c sys_inode.c'

(
    cd "$kernel_build"
    compiler=$(make_value '${CC}')
    compiler_flags=$(make_value '${CFLAGS}')
    include_flags=$(make_value '${INCLUDES}')
    configuration_flags=$(make_value '${PARAM}')
    source_root=$(make_value '${S}')

    printf '%s\n' \
        '#if !defined(__STDC_VERSION__) || __STDC_VERSION__ != 201710L' \
        '#error "RP2040 UFS requires GNU17"' \
        '#endif' |
        "$compiler" $compiler_flags $include_flags $configuration_flags \
            -DKERNEL -x c -fsyntax-only -

    for source_file in $source_files; do
        # Config generates these trusted repository-owned argument vectors.
        # shellcheck disable=SC2086
        "$compiler" $compiler_flags $include_flags $configuration_flags \
            -DKERNEL -Wstrict-prototypes -Wold-style-definition -Werror \
            -fsyntax-only "$source_root/kern/$source_file"
    done
)

echo "GNU17 UFS prototype check passed: 13 translation units"
