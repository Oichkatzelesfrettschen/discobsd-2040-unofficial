#!/usr/bin/env python3
"""Mutation check for the port's heatshrink decoder.

hsx_decoder_diff compares hsx_decoder.c and hsx_stream.c with the vendored
heatshrink decoder. This runner shows the comparison has teeth: it builds
the test once from the unmodified sources and requires a pass, then
rewrites one fragment of hsx_decoder.c or hsx_stream.c per mutant (each
fragment occurs exactly once), rebuilds, runs the test with -q -s, and
requires a failure. A failure is a nonzero exit, a fault from a guard
page, or a run that exceeds the time limit. Every child runs in its own
process group, and the group is killed on a timeout.

    hsx_decoder_mutants.py --cc cc --topsrc DIR [--images DIR]
"""
import argparse
import os
import signal
import subprocess
import sys
import tempfile

DEC = "hsx_decoder.c"
STR = "hsx_stream.c"

# (name, file, fragment, replacement), grouped by what the mutant breaks.
MUTANTS = (
    # bit reader refill
    ("refill shift one bit high", DEC,
     "acc |= (unsigned) *ip++ << sh;\n                sh -= 8;",
     "acc |= (unsigned) *ip++ << (sh + 1);\n                sh -= 8;"),
    ("refill adds seven bits per byte", DEC,
     "sh -= 8;", "sh -= 7;"),
    ("refill skipped at 17 valid bits", DEC,
     "if (sh > 6) {", "if (sh > 7) {"),
    ("refill reads past the chunk end", DEC,
     "while (sh >= 0 && ip != ie) {", "while (sh >= 0) {"),
    ("refill drops the byte when full", DEC,
     "acc |= (unsigned) *ip++ << sh;", "acc = (unsigned) *ip++ << sh;"),
    # token fields
    ("tag polarity reversed", DEC,
     "if ((int) acc < 0) {", "if ((int) acc >= 0) {"),
    ("literal from the wrong bits", DEC,
     "(unsigned char) (acc >> 23)", "(unsigned char) (acc >> 22)"),
    ("literal consumes eight bits", DEC,
     "acc <<= 9;\n            sh += 9;", "acc <<= 8;\n            sh += 9;"),
    ("literal accounting off by one", DEC,
     "acc <<= 9;\n            sh += 9;", "acc <<= 9;\n            sh += 8;"),
    # back-reference offset and length
    ("distance without the plus one", DEC,
     "(HSXD_WINDOW - 1)) + 1;", "(HSXD_WINDOW - 1));"),
    ("distance mask of 8 bits", DEC,
     "& (HSXD_WINDOW - 1)) + 1;", "& 255) + 1;"),
    ("length without the plus one", DEC,
     "((1u << lb) - 1)) + 1;", "((1u << lb) - 1));"),
    ("length field one bit wide", DEC,
     "((1u << lb) - 1)) + 1;", "((1u << (lb - 1)) - 1)) + 1;"),
    ("token consumes one bit too few", DEC,
     "acc <<= 1 + HSXD_WINDOW_BITS + lb;\n        sh += 1 + HSXD_WINDOW_BITS + lb;",
     "acc <<= HSXD_WINDOW_BITS + lb;\n        sh += 1 + HSXD_WINDOW_BITS + lb;"),
    ("length limit by output space dropped", DEC,
     "if (n > cnt)\n            n = cnt;", ""),
    ("copy past the output space", DEC,
     "n = (unsigned) (lim - op);", "n = (unsigned) (lim - op) + 1;"),
    ("pending back-reference lost", DEC,
     "if (cnt != 0)\n        goto copy;", "if (0)\n        goto copy;"),
    ("pending length not saved", DEC,
     "d->cnt = cnt;", "d->cnt = 0;"),
    ("bit buffer not saved", DEC,
     "d->acc = acc;", "d->acc = 0;"),
    ("position not advanced", DEC,
     "d->pos += n;", "d->pos += n ? 1 : 0;"),
    # window wrap and history before the first byte
    ("window index not wrapped", DEC,
     "*op++ = base[src++ & mask];", "*op++ = base[src++];"),
    ("history before the start not zero", DEC,
     "while (n != 0 && pos < off) {", "while (0) {"),
    ("history boundary off by one", DEC,
     "while (n != 0 && pos < off) {", "while (n != 0 && pos <= off) {"),
    ("history boundary one early", DEC,
     "while (n != 0 && pos < off) {", "while (n != 0 && pos + 1 < off) {"),
    # end of input
    ("literal accepted with eight bits", DEC,
     "if (sh > 15)\n                break;", "if (sh > 16)\n                break;"),
    ("literal refused with nine bits", DEC,
     "if (sh > 15)\n                break;", "if (sh > 14)\n                break;"),
    ("token accepted one bit short", DEC,
     "if (sh > 14 - (int) lb)", "if (sh > 15 - (int) lb)"),
    ("token refused when complete", DEC,
     "if (sh > 14 - (int) lb)", "if (sh > 13 - (int) lb)"),
    # stream loop
    ("trailing token after the last byte ignored", STR,
     "else if (room == 0)\n                    room = 1;",
     "else if (room == 0)\n                    break;"),
    ("short stream accepted", STR,
     "if (out != rlen)\n        return -1;", "if (out > rlen)\n        return -1;"),
    ("last input byte dropped", STR,
     "hsx_dec_input (d, w->stage, avail);", "hsx_dec_input (d, w->stage, avail - 1);"),
    ("one run per chunk", STR,
     "} while (n == room);", "} while (0);"),
    ("ring run crosses the ring end", STR,
     "if (room > HSXD_WINDOW - (out & mask))",
     "if (room > HSXD_WINDOW + 8 - (out & mask))"),
    ("crc over one byte too few", STR,
     "c = crc32_update (c, base + (out & mask), n);",
     "c = crc32_update (c, base + (out & mask), n - 1);"),
    ("input consumed twice", STR,
     "in += avail;", "in += avail + (avail > 1);"),
)


def run(cmd, timeout, **kw):
    """Run cmd in its own process group; kill the group on a timeout."""
    proc = subprocess.Popen(cmd, start_new_session=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, **kw)
    try:
        out, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        out, _ = proc.communicate()
        return None, out
    return proc.returncode, out


def compile_obj(cc, flags, src, obj):
    rc, out = run([cc] + flags + ["-c", "-o", obj, src], 300)
    if rc != 0:
        sys.stderr.write(out)
        raise SystemExit("compile failed: " + src)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc", default="cc")
    ap.add_argument("--topsrc", required=True)
    ap.add_argument("--images", default=None)
    ap.add_argument("--timeout", type=int, default=120)
    args = ap.parse_args()
    top = args.topsrc
    hs = os.path.join(top, "sys/arch/rp2040/heatshrink")
    krp = os.path.join(top, "sys/arch/rp2040/rp2040")
    srcs = {
        DEC: os.path.join(krp, DEC),
        STR: os.path.join(krp, STR),
    }
    flags = ["-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-idirafter", os.path.join(top, "tools/hsaout"),
             "-idirafter", os.path.join(top, "sys"), "-I" + hs,
             "-DHEATSHRINK_DYNAMIC_ALLOC=1"]
    with tempfile.TemporaryDirectory() as tmp:
        objs = {}
        for name, path in (
                ("test", os.path.join(top, "tests/hsaout/hsx_decoder_diff.c")),
                ("crc", os.path.join(top, "sys/kern/subr_crc32.c")),
                ("refdec", os.path.join(hs, "heatshrink_decoder.c")),
                ("refenc", os.path.join(hs, "heatshrink_encoder.c")),
                (DEC, srcs[DEC]), (STR, srcs[STR])):
            objs[name] = os.path.join(tmp, name + ".o")
            extra = ["-Wno-implicit-fallthrough"] if name.startswith("ref") else []
            compile_obj(args.cc, flags + extra, path, objs[name])

        def link_and_run(use):
            exe = os.path.join(tmp, "t.exe")
            order = ["test", "crc", "refdec", "refenc", DEC, STR]
            rc, out = run([args.cc, "-o", exe] + [use.get(k, objs[k]) for k in order], 300)
            if rc != 0:
                sys.stderr.write(out)
                raise SystemExit("link failed")
            cmd = [exe, "-q", "-s"]
            if args.images:
                cmd.append(args.images)
            return run(cmd, args.timeout)

        rc, out = link_and_run({})
        if rc != 0:
            sys.stdout.write(out)
            print("FAIL: the unmodified decoder does not pass its own test")
            return 1
        print("unmodified decoder: pass")
        escaped = 0
        for n, (name, fname, frag, repl) in enumerate(MUTANTS):
            with open(srcs[fname], encoding="utf-8") as f:
                text = f.read()
            if text.count(frag) != 1:
                print("FAIL: mutant %r fragment occurs %d times" %
                      (name, text.count(frag)))
                return 1
            mpath = os.path.join(tmp, "m%d_%s" % (n, fname))
            with open(mpath, "w", encoding="utf-8") as f:
                f.write(text.replace(frag, repl))
            mobj = os.path.join(tmp, "m%d.o" % n)
            rc, out = run([args.cc] + flags + ["-Wno-error", "-c", "-o", mobj, mpath], 300)
            if rc != 0:
                print("FAIL: mutant %r does not compile:\n%s" % (name, out))
                return 1
            rc, out = link_and_run({fname: mobj})
            if rc is None:
                verdict = "rejected (timeout)"
            elif rc < 0:
                verdict = "rejected (signal %d)" % -rc
            elif rc != 0:
                verdict = "rejected"
            else:
                verdict = "ESCAPED"
                escaped += 1
            print("mutant %-44s %s" % (name, verdict))
        if escaped:
            print("FAIL: %d mutants escaped" % escaped)
            return 1
        print("hsx_decoder: all %d mutants rejected" % len(MUTANTS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
