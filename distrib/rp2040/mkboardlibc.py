#!/usr/bin/env python3
"""
Build the board's reduced /usr/lib/libc.a: the archive members named in
boardlibc-members, compiled with the tree's exact libc flags (see
`bmake -n MACHINE=rp2040 -C lib/libc`) plus -fno-jump-tables, since the
board carries no libgcc to supply __gnu_thumb1_case_* for a jump table.
Each member is compiled or preprocessed to plain Thumb-1 assembly with
the host's arm-none-eabi-gcc, then assembled by the tree's own `as` (the
same assembler distrib/rp2040/cc runs on the board) and archived by the
tree's own `ar`, so the result is an a.out archive from end to end,
never an ELF one -- lib/libc.a stays ELF-only, for the host cross build.

usr.bin/as/tests/thumb-link.sh proves this same as/ar pair against the
full 329-member library; this script narrows the member list to the
closure distrib/rp2040/libc-sink.c pulls (see research/board-libc.md
for how that closure was read off a linker map) and adds
-DNO_DOPRNT_FLOATFMT to doprnt.c's compile so its %f/%e/%g branch never
reaches the double comparison and classification libgcc would otherwise
have to supply (__aeabi_dcmp*, __eqdf2, __ledf2): see the
NO_DOPRNT_FLOATFMT block in lib/libc/stdio/doprnt.c.
"""

import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOPSRC = os.path.abspath(os.path.join(HERE, "..", ".."))

# member -> (directory relative to TOPSRC, kind)
#   "c"       a .c source, compiled with -S then assembled
#   "s_cpp"   a hand-written .S source with SYS.h-style cpp macros,
#             preprocessed with -E -x assembler-with-cpp then assembled
#   "sysobj"  no source file at all: SYS.h's SYS() macro generates the
#             syscall trap sequence from the member's own name
DIRS = {
    "gen": "lib/libc/gen",
    "stdio": "lib/libc/stdio",
    "stdlib": "lib/libc/stdlib",
    "string": "lib/libc/string",
    "compat": "lib/libc/compat",
    "arm_sys": "lib/libc/arm/sys",
    "arm_gen": "lib/libc/arm/gen",
    "arm_string": "lib/libc/arm/string",
}

SYSOBJS = set(
    """
__sysctl accept access adjtime bind chdir chflags chmod chown chroot close
connect dup dup2 execve fchdir fchflags fchmod fchown fcntl flock fork fstat
fsync ftruncate getdtablesize getgroups getitimer getsockname getpeername
getpriority getrlimit getrusage getsockopt gettimeofday ioctl kill killpg
link listen lstat mkdir mknod mount open pselect quota read readlink readv
reboot recv recvfrom recvmsg rename rmdir select send sendmsg sendto
setgroups setitimer setpgrp setpriority setquota setuid seteuid setgid
setegid setrlimit setsockopt settimeofday shutdown sigaltstack socket
socketpair stat symlink sigprocmask sigstack sigwait statfs fstatfs
getfsstat truncate umount unlink utimes wait4 write writev lseek sigsuspend
getgid getegid getpgrp getpid getppid getuid geteuid profil sigpending sync
ufetch ustore ucall umask vfork vhangup rdglob wrglob msec kmemdev
""".split()
)

ARM_SYS_COBJS = set("sbrk execl execle execv".split())
ARM_SYS_ASMOBJS = set("_exit _brk pipe ptrace sigaction".split())
with open(os.path.join(TOPSRC, "lib/libc/arm/gen/rom_float_members")) as fh:
    ROM_FLOAT_S = {
        line.strip()
        for line in fh
        if line.strip() and not line.lstrip().startswith("#")
    }
ARM_GEN_S = set("_setjmp aeabi_div htonl htons setjmp sigsetjmp".split())
ARM_GEN_S.update(ROM_FLOAT_S)
ARM_STRING_S = set("memmove strcmp".split())

GEN_C = set(
    """
abort alarm atof atoi atol basename bcmp bcopy bzero calloc closedir crypt
ctime ctype_ daemon devname dirname ecvt err execvp fakcu ffs frexp fstab
gcvt getenv getgrent getgrgid getgrnam getgrouplist gethostname getloadavg
getlogin getmntinfo getpagesize getpass getpwent getttyent getttynam
getusershell getwd index initgroups isatty isinff isnanf knlist ldexp malloc
mktemp modff modf ndbm nlist opendir perror popen psignal qsort random
readdir regex rindex scandir seekdir setenv sethostname setmode siginterrupt
siglist signal sigsetops sleep strcasecmp strcat strcmp strcpy strdup
strftime strlen strncat strncmp strncpy swab sysctl syslog system telldir
time timezone ttyname ttyslot ualarm uname usleep wait3 wait waitpid
""".split()
)

STDIO_C = set(
    """
clrerr doprnt_float doprnt doscan exit fdopen feof ferror fgetc fgets filbuf
fileno findiop flsbuf fopen fprintf fputc fputs fread freopen fseek ftell
fwrite getchar gets getw printf putchar puts putw remove rew scanf setbuffer
setbuf setvbuf snprintf sprintf strout ungetc vfprintf vprintf vsprintf
""".split()
)

STDLIB_C = set("getopt getsubopt strtod strtol strtoul".split())

STRING_C = set(
    """
strcspn strerror strlcat strlcpy strpbrk strsep strspn strstr strtok
strtok_r
""".split()
)

COMPAT_C = set(
    """
creat ftime gethostid memccpy memchr memcmp memcpy memset nice pause rand
sethostid setregid setreuid setrgid setruid sigcompat strchr strrchr times
tmpnam utime
""".split()
)


def classify(stem):
    """Return (dirkey, kind) for a member's base name, arm overrides first."""
    if stem in ARM_STRING_S:
        return "arm_string", "s_cpp"
    if stem in ARM_GEN_S:
        return "arm_gen", "s_cpp"
    if stem in ARM_SYS_ASMOBJS:
        return "arm_sys", "s_cpp"
    if stem in ARM_SYS_COBJS:
        return "arm_sys", "c"
    if stem in SYSOBJS:
        return "arm_sys", "sysobj"
    if stem in GEN_C:
        return "gen", "c"
    if stem in STDIO_C:
        return "stdio", "c"
    if stem in STDLIB_C:
        return "stdlib", "c"
    if stem in STRING_C:
        return "string", "c"
    if stem in COMPAT_C:
        return "compat", "c"
    raise SystemExit("mkboardlibc: %s is not in any known libc directory" % stem)


def run(cmd, **kw):
    subprocess.run(cmd, check=True, **kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gccprefix", default="/usr/bin/arm-none-eabi")
    ap.add_argument("--cpu", default="cortex-m0plus")
    ap.add_argument("--toolbindir", default=os.path.join(TOPSRC, "tools/bin"))
    ap.add_argument("--include", default=os.path.join(TOPSRC, "include"))
    ap.add_argument("--workdir", required=True)
    ap.add_argument("--members", default=os.path.join(HERE, "boardlibc-members"))
    ap.add_argument("--out", required=True, help="output libc.a path")
    args = ap.parse_args()

    os.makedirs(args.workdir, exist_ok=True)
    gcc = [
        "%s-gcc" % args.gccprefix,
        "-std=gnu17",
        "-mcpu=%s" % args.cpu,
        "-mabi=aapcs",
        "-mlittle-endian",
        "-mthumb",
        "-mfloat-abi=soft",
        "-nostdinc",
        "-I%s" % args.include,
    ]
    cflags = [
        "-Os",
        "-fcommon",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wformat=2",
        "-ffreestanding",
        "-fno-jump-tables",
    ]
    as_tool = os.path.join(args.toolbindir, "as")
    ar_tool = os.path.join(args.toolbindir, "ar")
    ranlib_tool = os.path.join(args.toolbindir, "ranlib")

    members = [
        m.strip()
        for m in open(args.members)
        if m.strip() and not m.strip().startswith("#")
    ]

    objs = []
    for m in members:
        dirkey, kind = classify(m)
        srcdir = os.path.join(TOPSRC, DIRS[dirkey])
        sfile = os.path.join(args.workdir, m + ".s")
        ofile = os.path.join(args.workdir, m + ".o")

        if kind == "c":
            cfile = os.path.join(srcdir, m + ".c")
            extra = ["-DNO_DOPRNT_FLOATFMT"] if m == "doprnt" else []
            run(gcc + cflags + extra + ["-S", cfile, "-o", sfile])
        elif kind == "s_cpp":
            sourcefile = os.path.join(srcdir, m + ".S")
            with open(sfile, "w") as fh:
                run(
                    gcc
                    + [
                        "-x",
                        "assembler-with-cpp",
                        "-E",
                        "-P",
                        "-I%s" % srcdir,
                        sourcefile,
                    ],
                    stdout=fh,
                )
        elif kind == "sysobj":
            snippet = '#include "SYS.h"\nSYS(%s)\n' % m
            with open(sfile, "w") as fh:
                run(
                    gcc
                    + ["-x", "assembler-with-cpp", "-E", "-P", "-I%s" % srcdir, "-"],
                    input=snippet.encode(),
                    stdout=fh,
                )
        else:
            raise SystemExit("unreachable")

        run([as_tool, sfile, "-o", ofile])
        objs.append(ofile)

    if os.path.exists(args.out):
        os.remove(args.out)
    run([ar_tool, "rc", args.out] + objs)
    run([ranlib_tool, args.out])
    print(
        "mkboardlibc: %d members, %d bytes" % (len(objs), os.path.getsize(args.out)),
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
