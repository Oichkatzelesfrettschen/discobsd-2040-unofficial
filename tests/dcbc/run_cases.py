"""Run the dc and bc cases of cases.py against built programs.

usage: run_cases.py --dc DC [--bc BC] [--golden golden.json] [--dc-only]
                    [--corpus-limit N] [--timeout S]
       run_cases.py --peak DC --peak-limits peak.json

DC and BC are command lines (split with shlex), so "qemu-arm ./dc" works.
The programs name themselves from argv[0]; the file in the command must be
called dc or bc for the messages to match golden.json.

A case with an explicit expectation must exit 0 with that stdout and an empty
stderr. A golden case must match golden.json in exit status, stdout and
stderr. Exit status 0 means every selected case passed.
"""

import argparse
import json
import os
import shlex
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cases as case_module  # noqa: E402
import procgroup  # noqa: E402


def execute(command, case, timeout):
    """Run one case; return (rc, stdout, stderr) or rc "timeout".

    The program runs as a session leader (procgroup.run_group), so a timeout
    also ends the dc that bc forks.
    """
    env = dict(os.environ, LC_ALL="C")
    with tempfile.TemporaryDirectory(prefix="dcbc.") as work:
        for name, data in case["files"].items():
            with open(os.path.join(work, name), "wb") as handle:
                handle.write(data)
        argv = (
            [os.path.basename(command[0])] + command[1:] + case["args"]
            if len(command) == 1
            else command + case["args"]
        )
        rc, out, err, timed_out = procgroup.run_group(
            argv,
            cwd=work,
            env=env,
            stdin_data=case["stdin"],
            timeout=timeout,
            executable=command[0],
        )
    if timed_out:
        return "timeout", b"", b""
    return rc, out, err


def command_line(text):
    """Split a command line, making existing paths absolute for the cwd change."""
    return [os.path.abspath(t) if os.path.exists(t) else t for t in shlex.split(text)]


def golden_record(rc, out, err):
    return {
        "rc": rc,
        "out": out.decode("latin-1"),
        "err": err.decode("latin-1"),
    }


def select(args):
    chosen = []
    corpus_seen = 0
    for case in case_module.all_cases():
        if args.dc_only and case["kind"] != "dc":
            continue
        if case["tier"] == "corpus":
            corpus_seen += 1
            if args.corpus_limit is not None and corpus_seen > args.corpus_limit:
                continue
        if case["tier"] == "host" and args.no_host_text:
            continue
        chosen.append(case)
    return chosen


def check(case, result, golden):
    rc, out, err = result
    if case["expect"] is not None:
        problems = []
        if rc != 0:
            problems.append("exit status %s, expected 0" % (rc,))
        if out != case["expect"]:
            problems.append("stdout %r, expected %r" % (out[:200], case["expect"][:200]))
        if err:
            problems.append("unexpected stderr %r" % (err[:200],))
        return problems
    want = golden.get(case["name"])
    if want is None:
        return ["no golden record"]
    got = golden_record(rc, out, err)
    if got == want:
        return []
    problems = []
    for key in ("rc", "out", "err"):
        if got[key] != want[key]:
            problems.append("%s %r, expected %r" % (key, str(got[key])[:200], str(want[key])[:200]))
    return problems


def run_cases(args):
    golden = {}
    if args.golden:
        with open(args.golden, encoding="utf-8") as handle:
            golden = json.load(handle)
    dc = command_line(args.dc)
    bc = command_line(args.bc) if args.bc else None
    chosen = select(args)
    failures = 0
    for case in chosen:
        command = dc if case["kind"] == "dc" else bc
        if command is None:
            print("FAIL %s: no bc program given" % case["name"])
            failures += 1
            continue
        problems = check(case, execute(command, case, args.timeout), golden)
        if problems:
            failures += 1
            print("FAIL %s: %s" % (case["name"], "; ".join(problems)))
    print(
        "dcbc cases=%d failures=%d explicit=%d"
        % (len(chosen), failures, sum(1 for c in chosen if c["expect"] is not None))
    )
    if failures:
        return 1
    print("PASS dcbc")
    return 0


def run_peak(args):
    with open(args.peak_limits, encoding="utf-8") as handle:
        limits = json.load(handle)
    failures = 0
    for item in limits:
        case = {
            "args": item["args"],
            "stdin": item["stdin"].encode("latin-1"),
            "files": {},
        }
        with tempfile.NamedTemporaryFile(prefix="peak.") as peak:
            os.environ["DCBC_PEAK_FILE"] = peak.name
            rc, out, err = execute(command_line(args.peak), case, args.timeout)
            record = open(peak.name, encoding="ascii").read().strip()
        measured = int(record.split()[0].split("=")[1]) if record else -1
        status = "ok"
        if rc != 0 or measured < 0 or measured > item["limit"]:
            status = "FAIL"
            failures += 1
        print(
            "%s peak %s: rc=%s peak=%d limit=%d (%s)"
            % (status, item["name"], rc, measured, item["limit"], item["note"])
        )
    if failures:
        return 1
    print("PASS dcbc peak")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dc")
    parser.add_argument("--bc")
    parser.add_argument("--golden")
    parser.add_argument("--dc-only", action="store_true")
    parser.add_argument("--corpus-limit", type=int)
    parser.add_argument(
        "--no-host-text",
        action="store_true",
        help="skip cases whose golden text comes from the host libc",
    )
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--peak")
    parser.add_argument("--peak-limits")
    parser.add_argument("--write-golden", help="write golden records from this run")
    args = parser.parse_args()
    if args.peak:
        return run_peak(args)
    if not args.dc:
        parser.error("--dc is required")
    if args.write_golden:
        return write_golden(args)
    return run_cases(args)


def write_golden(args):
    dc = command_line(args.dc)
    bc = command_line(args.bc) if args.bc else None
    record = {}
    for case in select(args):
        if case["expect"] is not None:
            continue
        command = dc if case["kind"] == "dc" else bc
        result = execute(command, case, args.timeout)
        if result[0] == "timeout":
            print("TIMEOUT %s" % case["name"], file=sys.stderr)
            continue
        record[case["name"]] = golden_record(*result)
    with open(args.write_golden, "w", encoding="utf-8") as handle:
        json.dump(record, handle, indent=0, sort_keys=True)
        handle.write("\n")
    print("wrote %d golden records" % len(record))
    return 0


if __name__ == "__main__":
    sys.exit(main())
