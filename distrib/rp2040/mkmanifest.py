#!/usr/bin/env python3
"""
Compose the RP2040 root filesystem manifest for one profile, and refuse a
composition whose image would be broken.

distrib/rp2040/mi.rp2040 brackets each optional feature between
"#closure NAME" and "#endclosure NAME"; distrib/rp2040/profiles declares
which closures each profile keeps and which closures and paths a closure
depends on. This script emits the base lines plus the lines of the
selected closures, in manifest order, with the markers removed.
sys/arch/rp2040/doc/PROFILES.md is the authority for the scheme.

The refusal is the point. tools/fsutil/fsutil.c's add_hardlink() reports
"link source not found" on stderr and returns, leaving fsutil's exit
status at 0, so a profile that keeps "link /usr/games/keen" without
"pack /usr/games/gamebox" yields an image that boots with a name that
opens nothing. A missing /usr/lib/libc.a is quieter still: nothing in the
build reads it, and the failure surfaces as a compile on the board that
cannot link. Both are decided here, before fsutil runs, against the
composed manifest:

  duplicate path      two directives name the same inode
  missing parent      a path whose directory no selected line creates
  dangling hard link  a "link" whose target no earlier line installs
  dangling symlink    a "symlink" whose resolved target the image lacks
  unmet closure       a selected closure whose "needs closure" is absent
  unmet path          a selected closure whose "needs path" is absent

--selftest is the calibration: it feeds the same checker compositions
that must be rejected and the declared profiles that must pass, so a
checker that stopped deciding fails the gate rather than reporting a
clean run. The root Makefile's check-fs-profiles runs both halves.
"""

import argparse
import os
import posixpath
import sys

MARK_OPEN = "#closure"
MARK_CLOSE = "#endclosure"

# The object directives tools/fsutil/manifest.c accepts, and the entry
# type each one produces. A directive outside these two sets is a
# manifest error here rather than a line passed through unread.
OBJECT_DIRECTIVES = {
    "dir": "d",
    "file": "f",
    "pack": "p",
    "link": "l",
    "symlink": "s",
    "bdev": "b",
    "cdev": "c",
}
ATTRIBUTE_DIRECTIVES = frozenset(
    [
        "default",
        "owner",
        "group",
        "dirmode",
        "filemode",
        "mode",
        "major",
        "minor",
        "target",
        "source",
    ]
)

# A hard link resolves against an inode fsutil has already created, so
# only these entry types can be a link target.
LINKABLE = frozenset(["f", "p", "l"])


class ManifestError(Exception):
    """A manifest, profile file or command line the script cannot read."""


class Line:
    """One manifest line, with the closure whose block encloses it."""

    __slots__ = ("text", "closure", "origin", "lineno")

    def __init__(self, text, closure, origin, lineno):
        self.text = text
        self.closure = closure
        self.origin = origin
        self.lineno = lineno

    def where(self):
        return "%s:%d" % (self.origin, self.lineno)


class Entry:
    """One filesystem object the composed manifest installs."""

    __slots__ = ("kind", "path", "target", "line")

    def __init__(self, kind, path, line):
        self.kind = kind
        self.path = path
        self.target = None
        self.line = line


def read_marked(path):
    """Read a manifest, returning its lines and the closure names it opens."""
    lines = []
    closures = []
    stack = []
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    for lineno, raw in enumerate(text.splitlines(), 1):
        words = raw.strip().split()
        if words and words[0] == MARK_CLOSE:
            if len(words) != 2:
                raise ManifestError(
                    "%s:%d: %s takes the closure name" % (path, lineno, MARK_CLOSE)
                )
            if not stack:
                raise ManifestError(
                    "%s:%d: %s %s closes no open closure" % (path, lineno, MARK_CLOSE, words[1])
                )
            opened = stack.pop()
            if opened != words[1]:
                raise ManifestError(
                    "%s:%d: %s %s closes the open closure %s"
                    % (path, lineno, MARK_CLOSE, words[1], opened)
                )
            continue
        if words and words[0] == MARK_OPEN:
            if len(words) != 2:
                raise ManifestError(
                    "%s:%d: %s takes the closure name" % (path, lineno, MARK_OPEN)
                )
            if stack:
                raise ManifestError(
                    "%s:%d: closure %s opens inside closure %s"
                    % (path, lineno, words[1], stack[0])
                )
            stack.append(words[1])
            if words[1] not in closures:
                closures.append(words[1])
            continue
        lines.append(Line(raw, stack[0] if stack else None, path, lineno))
    if stack:
        raise ManifestError("%s: closure %s reaches end of file open" % (path, stack[0]))
    return lines, closures


def read_profiles(path):
    """Read the profile and closure declarations."""
    profiles = {}
    order = []
    needs_closure = {}
    needs_path = {}
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    for lineno, raw in enumerate(text.splitlines(), 1):
        words = raw.split("#", 1)[0].split()
        if not words:
            continue
        where = "%s:%d" % (path, lineno)
        if words[0] == "profile":
            if len(words) < 2:
                raise ManifestError("%s: profile takes a name" % where)
            name = words[1]
            if name in profiles:
                raise ManifestError("%s: profile %s is declared twice" % (where, name))
            selected = []
            for closure in words[2:]:
                if closure in selected:
                    raise ManifestError(
                        "%s: profile %s names closure %s twice" % (where, name, closure)
                    )
                selected.append(closure)
            profiles[name] = selected
            order.append(name)
        elif words[0] == "closure":
            if len(words) < 5 or words[2] != "needs" or words[3] not in ("closure", "path"):
                raise ManifestError(
                    "%s: expected 'closure NAME needs closure|path ARG ...'" % where
                )
            name = words[1]
            if words[3] == "closure":
                needs_closure.setdefault(name, []).extend(words[4:])
            else:
                for required in words[4:]:
                    if not required.startswith("/"):
                        raise ManifestError("%s: %s is not an absolute path" % (where, required))
                needs_path.setdefault(name, []).extend(words[4:])
        else:
            raise ManifestError("%s: unknown directive '%s'" % (where, words[0]))
    if not profiles:
        raise ManifestError("%s: no profile is declared" % path)
    return profiles, order, needs_closure, needs_path


def check_declarations(profiles, needs_closure, needs_path, closures):
    """Hold the profile file and the manifest markers to the same closure set."""
    known = set(closures)
    errors = []
    for name, selected in sorted(profiles.items()):
        for closure in selected:
            if closure not in known:
                errors.append("profile %s names closure %s, which the manifest never opens"
                              % (name, closure))
    for table in (needs_closure, needs_path):
        for name in sorted(table):
            if name not in known:
                errors.append("closure %s carries a declaration and no manifest block" % name)
    for name in sorted(needs_closure):
        for other in needs_closure[name]:
            if other not in known:
                errors.append("closure %s needs closure %s, which the manifest never opens"
                              % (name, other))
    for closure in closures:
        if not any(closure in selected for selected in profiles.values()):
            errors.append("closure %s reaches no profile" % closure)
    return errors


def compose(lines, selected):
    """Return the manifest lines a profile keeps, in manifest order."""
    keep = set(selected)
    return [line for line in lines if line.closure is None or line.closure in keep]


def parse_entries(lines):
    """Walk the composed lines the way tools/fsutil/manifest.c walks them."""
    entries = []
    pending = None
    for line in lines:
        stripped = line.text.strip()
        if not stripped or stripped.startswith("#"):
            continue
        words = stripped.split(None, 1)
        directive = words[0]
        argument = words[1].strip() if len(words) > 1 else ""
        if directive in OBJECT_DIRECTIVES:
            if not argument:
                raise ManifestError("%s: %s takes a path" % (line.where(), directive))
            pending = Entry(OBJECT_DIRECTIVES[directive], argument, line)
            entries.append(pending)
        elif directive == "target":
            if pending is None:
                raise ManifestError("%s: target belongs to no object" % line.where())
            pending.target = argument
        elif directive == "default":
            pending = None
        elif directive in ATTRIBUTE_DIRECTIVES:
            continue
        else:
            raise ManifestError("%s: unknown directive '%s'" % (line.where(), directive))
    return entries


def verify(entries, selected, needs_closure, needs_path):
    """Decide the composed manifest; return the reasons it cannot ship."""
    errors = []
    dirs = {"/"}
    linkable = set()
    installed = {}
    for entry in entries:
        path = entry.path
        where = entry.line.where()
        if path in installed:
            errors.append(
                "%s: %s is installed twice (first at %s)" % (where, path, installed[path])
            )
        parent = posixpath.dirname(path) or "/"
        if parent not in dirs:
            errors.append("%s: %s needs directory %s, which no selected line creates"
                          % (where, path, parent))
        if entry.kind == "l":
            if not entry.target:
                errors.append("%s: link %s names no target" % (where, path))
            elif entry.target not in linkable:
                errors.append(
                    "%s: hard link %s has no source: %s is not in this profile"
                    % (where, path, entry.target)
                )
        elif entry.kind == "s":
            if not entry.target:
                errors.append("%s: symlink %s names no target" % (where, path))
            else:
                resolved = entry.target
                if not resolved.startswith("/"):
                    resolved = posixpath.join(parent, resolved)
                resolved = posixpath.normpath(resolved)
                if resolved not in installed and resolved not in dirs:
                    errors.append(
                        "%s: symlink %s points at %s, which is not in this profile"
                        % (where, path, resolved)
                    )
        installed[path] = where
        if entry.kind == "d":
            dirs.add(path)
        if entry.kind in LINKABLE:
            linkable.add(path)
    chosen = set(selected)
    for closure in selected:
        for other in needs_closure.get(closure, []):
            if other not in chosen:
                errors.append(
                    "closure %s needs closure %s, which this profile does not select"
                    % (closure, other)
                )
        for required in needs_path.get(closure, []):
            if required not in installed:
                errors.append(
                    "closure %s needs %s, which this profile does not install"
                    % (closure, required)
                )
    return errors


def report(profile, errors):
    """Print a verdict; return True when the composition ships."""
    if not errors:
        return True
    print("mkmanifest: profile %s does not compose:" % profile, file=sys.stderr)
    for error in errors:
        print("  %s" % error, file=sys.stderr)
    return False


def emit(lines, output):
    """Write the composed manifest, replacing any earlier one atomically."""
    body = "".join(line.text + "\n" for line in lines)
    temporary = output + ".new"
    with open(temporary, "w", encoding="utf-8") as fh:
        fh.write(body)
    os.replace(temporary, output)


def load(args):
    """Read both inputs and hold their closure sets to each other."""
    lines, closures = read_marked(args.manifest)
    appended = []
    for extra in args.append:
        more, opened = read_marked(extra)
        if opened:
            raise ManifestError("%s: closure markers belong in %s" % (extra, args.manifest))
        appended.extend(more)
    profiles, order, needs_closure, needs_path = read_profiles(args.profiles)
    errors = check_declarations(profiles, needs_closure, needs_path, closures)
    if errors:
        raise ManifestError(
            "%s and %s disagree:\n  %s" % (args.profiles, args.manifest, "\n  ".join(errors))
        )
    return lines, appended, profiles, order, needs_closure, needs_path


def run_profile(profile, lines, appended, profiles, needs_closure, needs_path):
    """Compose one declared profile and decide it; return (kept, errors)."""
    selected = profiles[profile]
    kept = compose(lines, selected)
    errors = verify(parse_entries(kept + appended), selected, needs_closure, needs_path)
    return kept, errors


def summarize(profile, kept, appended):
    """Print what the composition installs, for the build log."""
    entries = parse_entries(kept + appended)
    counts = {}
    for entry in entries:
        counts[entry.kind] = counts.get(entry.kind, 0) + 1
    print(
        "mkmanifest: profile %s: %d directories, %d files, %d devices, %d links, %d symlinks"
        % (
            profile,
            counts.get("d", 0),
            counts.get("f", 0) + counts.get("p", 0),
            counts.get("b", 0) + counts.get("c", 0),
            counts.get("l", 0),
            counts.get("s", 0),
        )
    )


def drop(lines, text):
    """Return the lines with one directive removed, for the calibration."""
    wanted = text.split()
    kept = []
    removed = 0
    for line in lines:
        if line.text.split() == wanted:
            removed += 1
            continue
        kept.append(line)
    if removed != 1:
        raise ManifestError("selftest: '%s' matches %d lines" % (text, removed))
    return kept


def selftest(lines, appended, profiles, needs_closure, needs_path):
    """Calibrate the checker: every case below must land on its stated side."""
    cases = []
    for name in sorted(profiles):
        cases.append(("accept", "declared profile %s" % name, profiles[name], None, None))
    cases.append(
        (
            "reject",
            "compiler without its link library",
            ["coremark", "pdp11", "v6disk", "toolchain", "games"],
            None,
            "needs closure toolchainlib",
        )
    )
    cases.append(
        (
            "reject",
            "link library without its compiler",
            ["toolchainlib"],
            None,
            "needs closure toolchain",
        )
    )
    cases.append(
        (
            "reject",
            "emulator without its V6 pack",
            ["pdp11", "toolchain", "toolchainlib"],
            None,
            "needs closure v6disk",
        )
    )
    cases.append(
        (
            "reject",
            "gamebox entry points without gamebox",
            ["games"],
            "pack /usr/games/gamebox",
            "hard link /usr/games/keen has no source",
        )
    )
    cases.append(
        (
            "reject",
            "compiler without /usr/lib/libc.a",
            ["toolchain", "toolchainlib"],
            "file /usr/lib/libc.a",
            "needs /usr/lib/libc.a",
        )
    )
    cases.append(
        (
            "reject",
            "gamebox without its directory",
            ["games"],
            "dir /usr/games",
            "needs directory /usr/games",
        )
    )
    failures = 0
    for verdict, label, selected, removed, expected in cases:
        kept = compose(lines, selected)
        if removed:
            kept = drop(kept, removed)
        errors = verify(parse_entries(kept + appended), selected, needs_closure, needs_path)
        if verdict == "accept":
            if errors:
                failures += 1
                print("selftest: %s must compose, and does not:" % label, file=sys.stderr)
                for error in errors:
                    print("  %s" % error, file=sys.stderr)
            else:
                print("selftest: accept %s" % label)
        else:
            joined = "\n".join(errors)
            if not errors:
                failures += 1
                print("selftest: %s must be rejected, and composed" % label, file=sys.stderr)
            elif expected not in joined:
                failures += 1
                print(
                    "selftest: %s is rejected, but not for '%s':\n%s" % (label, expected, joined),
                    file=sys.stderr,
                )
            else:
                print("selftest: reject %s -- %s" % (label, expected))
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("--manifest", required=True, help="the closure-marked manifest")
    parser.add_argument("--profiles", required=True, help="the profile and closure declarations")
    parser.add_argument(
        "--append",
        action="append",
        default=[],
        help="a manifest the build concatenates after this one, checked but not emitted",
    )
    parser.add_argument("--profile", help="compose this profile")
    parser.add_argument("--output", help="write the composed manifest here")
    parser.add_argument("--check-all", action="store_true", help="decide every declared profile")
    parser.add_argument("--selftest", action="store_true", help="calibrate the checker")
    parser.add_argument("--list", action="store_true", help="print the declared profile names")
    args = parser.parse_args()

    try:
        lines, appended, profiles, order, needs_closure, needs_path = load(args)
    except (OSError, ManifestError) as error:
        print("mkmanifest: %s" % error, file=sys.stderr)
        return 2

    if args.list:
        for name in order:
            print(" ".join([name] + profiles[name]))
        return 0

    try:
        if args.selftest:
            return 1 if selftest(lines, appended, profiles, needs_closure, needs_path) else 0

        if args.check_all:
            failures = 0
            for name in order:
                kept, errors = run_profile(
                    name, lines, appended, profiles, needs_closure, needs_path
                )
                if report(name, errors):
                    summarize(name, kept, appended)
                else:
                    failures += 1
            return 1 if failures else 0

        if not args.profile:
            print("mkmanifest: name a profile, --check-all, --selftest or --list", file=sys.stderr)
            return 2
        if args.profile not in profiles:
            print(
                "mkmanifest: %s declares no profile %s; it declares %s"
                % (args.profiles, args.profile, " ".join(order)),
                file=sys.stderr,
            )
            return 2
        kept, errors = run_profile(
            args.profile, lines, appended, profiles, needs_closure, needs_path
        )
        if not report(args.profile, errors):
            return 1
        summarize(args.profile, kept, appended)
        if args.output:
            emit(kept, args.output)
    except (OSError, ManifestError) as error:
        print("mkmanifest: %s" % error, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
