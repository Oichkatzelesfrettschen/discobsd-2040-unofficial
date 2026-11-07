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
  empty multicall     a selected multicall binary none of whose members is
  unlinked member     a multicall member whose dispatch name has no link

A profile names closures and groups; --with and --without add and remove
names for one build (the Make variables IMAGE_WITH and IMAGE_WITHOUT) and
are refused when a name is unknown or one closure is both added and
removed. A dependency is checked, never resolved: removing a closure whose
dependent stays selected fails with the dependent's "needs" message.
--members prints the members of a multicall binary the selection keeps, which
the games Makefiles take as the set to link into one program;
--list-closures prints each closure, its groups, dependencies and cost.

--selftest is the calibration: it feeds the same checker compositions
that must be rejected and the declared profiles that must pass, so a
checker that stopped deciding fails the gate rather than reporting a
clean run. The root Makefile's check-fs-profiles runs both halves.
"""

import argparse
import copy
import os
import posixpath
import re
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

    __slots__ = ("kind", "path", "target", "mode", "line")

    def __init__(self, kind, path, mode, line):
        self.kind = kind
        self.path = path
        self.target = None
        self.mode = mode
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



# A closure, group or multicall member name. The Make variables IMAGE_WITH
# and IMAGE_WITHOUT reach the shell unquoted, so Makefile.inc holds them to
# this alphabet too.
NAME_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9_-]*\Z")


class Declarations:
    """Everything the profile files declare, merged across the files read."""

    def __init__(self):
        self.profiles = {}
        self.order = []
        self.needs_closure = {}
        self.needs_path = {}
        self.groups = {}
        self.group_order = []
        self.multicalls = {}
        self.costs = {}
        self.closures = []
        self.texts = []

    def copy(self):
        return copy.deepcopy(self)


def parse_name(word, where, what):
    if NAME_PATTERN.match(word) is None:
        raise ManifestError("%s: %s '%s' is not a name (letters, digits, - and _)"
                            % (where, what, word))
    return word


def parse_declarations(decl, origin, text):
    """Add the directives of one profile file to decl."""
    decl.texts.append((origin, text))
    for lineno, raw in enumerate(text.splitlines(), 1):
        words = raw.split("#", 1)[0].split()
        if not words:
            continue
        where = "%s:%d" % (origin, lineno)
        if words[0] == "profile":
            if len(words) < 2:
                raise ManifestError("%s: profile takes a name" % where)
            name = words[1]
            if name in decl.profiles:
                raise ManifestError("%s: profile %s is declared twice" % (where, name))
            selected = []
            for closure in words[2:]:
                if closure in selected:
                    raise ManifestError(
                        "%s: profile %s names closure %s twice" % (where, name, closure)
                    )
                selected.append(closure)
            decl.profiles[name] = selected
            decl.order.append(name)
        elif words[0] == "closure":
            if len(words) < 5 or words[2] != "needs" or words[3] not in (
                "closure",
                "path",
            ):
                raise ManifestError(
                    "%s: expected 'closure NAME needs closure|path ARG ...'" % where
                )
            name = words[1]
            if words[3] == "closure":
                decl.needs_closure.setdefault(name, []).extend(words[4:])
            else:
                for required in words[4:]:
                    if not required.startswith("/"):
                        raise ManifestError(
                            "%s: %s is not an absolute path" % (where, required)
                        )
                decl.needs_path.setdefault(name, []).extend(words[4:])
        elif words[0] == "group":
            if len(words) < 3:
                raise ManifestError("%s: expected 'group NAME MEMBER ...'" % where)
            name = parse_name(words[1], where, "group")
            if name in decl.groups:
                raise ManifestError("%s: group %s is declared twice" % (where, name))
            members = []
            for member in words[2:]:
                parse_name(member, where, "member")
                if member in members:
                    raise ManifestError("%s: group %s names %s twice" % (where, name, member))
                members.append(member)
            decl.groups[name] = members
            decl.group_order.append(name)
        elif words[0] == "multicall":
            if len(words) < 4 or not words[2].startswith("/"):
                raise ManifestError(
                    "%s: expected 'multicall CLOSURE /PATH MEMBER ...'" % where
                )
            host = parse_name(words[1], where, "multicall closure")
            if host in decl.multicalls:
                raise ManifestError("%s: multicall %s is declared twice" % (where, host))
            members = []
            for member in words[3:]:
                parse_name(member, where, "member")
                if member in members:
                    raise ManifestError(
                        "%s: multicall %s names %s twice" % (where, host, member)
                    )
                members.append(member)
            decl.multicalls[host] = (words[2], members)
            # A dispatch name works only through its binary, so a member
            # carries the host as a checked dependency.
            for member in members:
                needs = decl.needs_closure.setdefault(member, [])
                if host not in needs:
                    needs.append(host)
        elif words[0] == "cost":
            if len(words) != 4:
                raise ManifestError("%s: expected 'cost NAME BLOCKS INODES'" % where)
            name = parse_name(words[1], where, "cost target")
            if name in decl.costs:
                raise ManifestError("%s: cost of %s is declared twice" % (where, name))
            try:
                figures = (int(words[2]), int(words[3]))
            except ValueError as error:
                raise ManifestError("%s: cost figures are whole numbers" % where) from error
            if min(figures) < 0:
                raise ManifestError("%s: cost figures are not negative" % where)
            decl.costs[name] = figures
        else:
            raise ManifestError("%s: unknown directive '%s'" % (where, words[0]))
    return decl


def read_profiles(paths):
    """Read and merge the profile and closure declaration files."""
    decl = Declarations()
    for path in paths:
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        parse_declarations(decl, path, text)
    if not decl.profiles:
        raise ManifestError("%s: no profile is declared" % ", ".join(paths))
    return decl


def expand(decl, name, trail=()):
    """Return the closures a closure or group name stands for, in order."""
    if name in decl.groups:
        if name in trail:
            raise ManifestError(
                "group %s contains itself (%s)" % (name, " > ".join(trail + (name,)))
            )
        leaves = []
        for member in decl.groups[name]:
            for leaf in expand(decl, member, trail + (name,)):
                if leaf not in leaves:
                    leaves.append(leaf)
        return leaves
    if name in decl.closures:
        return [name]
    raise ManifestError("unknown closure or group %s" % name)


def expand_all(decl, names):
    leaves = []
    for name in names:
        for leaf in expand(decl, name):
            if leaf not in leaves:
                leaves.append(leaf)
    return leaves


def groups_of(decl, closure):
    """Return the groups, nested ones included, that stand for a closure."""
    found = []
    for name in decl.group_order:
        try:
            if closure in expand(decl, name):
                found.append(name)
        except ManifestError:
            continue
    return found


def closure_entries(lines, closure):
    return parse_entries([line for line in lines if line.closure == closure])


def check_declarations(decl, lines, selected_extra):
    """Hold the profile file and the manifest markers to the same closure set."""
    known = set(decl.closures)
    errors = []
    for closure in selected_extra:
        if closure not in known:
            errors.append(
                "selected closure %s has no manifest block" % closure
            )
    for name in decl.group_order:
        if name in known:
            errors.append("group %s is also a closure" % name)
        for member in decl.groups[name]:
            if member not in known and member not in decl.groups:
                errors.append(
                    "group %s names %s, which is neither a closure nor a group"
                    % (name, member)
                )
        try:
            expand(decl, name)
        except ManifestError as error:
            if "contains itself" in str(error):
                errors.append(str(error))
    resolvable = known | set(decl.groups)
    reached = set()
    for name, selected in sorted(decl.profiles.items()):
        for closure in selected:
            if closure not in resolvable:
                errors.append("profile %s names closure %s, which the manifest never opens"
                              % (name, closure))
            else:
                try:
                    reached.update(expand(decl, closure))
                except ManifestError:
                    continue
    for table in (decl.needs_closure, decl.needs_path):
        for name in sorted(table):
            if name not in known:
                errors.append("closure %s carries a declaration and no manifest block" % name)
    for name in sorted(decl.needs_closure):
        for other in decl.needs_closure[name]:
            if other not in resolvable:
                errors.append("closure %s needs closure %s, which the manifest never opens"
                              % (name, other))
    for name in sorted(decl.costs):
        if name not in resolvable:
            errors.append("cost names %s, which the manifest never opens" % name)
    claimed = {}
    for host, (path, members) in sorted(decl.multicalls.items()):
        if host not in known:
            errors.append("multicall %s names a closure the manifest never opens" % host)
            continue
        host_entries = closure_entries(lines, host)
        if not any(entry.path == path and entry.kind in ("f", "p") for entry in host_entries):
            errors.append("multicall %s: closure %s installs no file %s" % (host, host, path))
        for member in members:
            if member not in known:
                errors.append("multicall %s: member %s is no closure" % (host, member))
                continue
            if member in claimed:
                errors.append("multicall %s: member %s already belongs to %s"
                              % (host, member, claimed[member]))
            claimed[member] = host
            link = posixpath.join(posixpath.dirname(path), member)
            if not any(entry.path == link and entry.kind == "l" and entry.target == path
                       for entry in closure_entries(lines, member)):
                errors.append("multicall %s: closure %s installs no link %s to %s"
                              % (host, member, link, path))
    for closure in decl.closures:
        if closure not in selected_extra and closure not in reached:
            errors.append("closure %s reaches no profile" % closure)
    return errors


class SelectionError(ManifestError):
    """A --with or --without list the selection cannot honor."""


def select(decl, names, selected_extra=(), with_names=(), without_names=()):
    """Apply per-build additions and removals to a profile's closure names.

    Names may be closures or groups. An unknown name, and a closure that
    one list adds and the other removes, are refused; dependencies are not
    resolved here -- verify() decides them against the result.
    """
    chosen = expand_all(decl, names)
    added = {}
    removed = {}
    for label, source, table in (("--with", with_names, added),
                                 ("--without", without_names, removed)):
        for name in source:
            if name not in decl.closures and name not in decl.groups:
                raise SelectionError(
                    "%s names unknown closure or group %s; closures are %s, groups are %s"
                    % (label, name, " ".join(decl.closures), " ".join(decl.group_order))
                )
            for leaf in expand(decl, name):
                table.setdefault(leaf, name)
    for leaf in sorted(set(added) & set(removed)):
        raise SelectionError(
            "closure %s is both added (as %s) and removed (as %s)"
            % (leaf, added[leaf], removed[leaf])
        )
    selected = [closure for closure in chosen if closure not in removed]
    for closure in list(added) + list(selected_extra):
        if closure not in selected:
            selected.append(closure)
    return selected


def selected_members(decl, selected, host):
    """Return the members of a multicall binary the selection keeps."""
    if host not in decl.multicalls:
        raise ManifestError("no multicall closure %s is declared; declared: %s"
                            % (host, " ".join(sorted(decl.multicalls)) or "none"))
    if host not in selected:
        return []
    return [member for member in decl.multicalls[host][1] if member in selected]


def compose(lines, selected):
    """Return the manifest lines a profile keeps, in manifest order."""
    keep = set(selected)
    return [line for line in lines if line.closure is None or line.closure in keep]


def parse_entries(lines):
    """Walk the composed lines the way tools/fsutil/manifest.c walks them."""
    entries = []
    pending = None
    default_filemode = 0o664
    default_dirmode = 0o775
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
            kind = OBJECT_DIRECTIVES[directive]
            mode = default_dirmode if kind == "d" else default_filemode
            pending = Entry(kind, argument, mode, line)
            entries.append(pending)
        elif directive == "target":
            if pending is None:
                raise ManifestError("%s: target belongs to no object" % line.where())
            pending.target = argument
        elif directive == "default":
            pending = None
        elif directive in ("filemode", "dirmode"):
            if pending is not None:
                raise ManifestError("%s: %s is allowed only in a default section" %
                                    (line.where(), directive))
            try:
                mode = int(argument.split()[0], 8)
            except (IndexError, ValueError) as error:
                raise ManifestError("%s: invalid %s value" %
                                    (line.where(), directive)) from error
            if directive == "filemode":
                default_filemode = mode
            else:
                default_dirmode = mode
        elif directive == "mode":
            if pending is None:
                raise ManifestError("%s: mode belongs to no object" % line.where())
            try:
                pending.mode = int(argument.split()[0], 8)
            except (IndexError, ValueError) as error:
                raise ManifestError("%s: invalid mode value" % line.where()) from error
        elif directive in ATTRIBUTE_DIRECTIVES:
            continue
        else:
            raise ManifestError("%s: unknown directive '%s'" % (line.where(), directive))
    return entries




def verify(entries, selected, decl):
    """Decide the composed manifest; return the reasons it cannot ship."""
    errors = []
    dirs = {"/"}
    linkable = set()
    installed = {}
    entries_by_path = {}
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
        entries_by_path[path] = entry
        if entry.kind == "d":
            dirs.add(path)
        if entry.kind in LINKABLE:
            linkable.add(path)
    required_modes = {"/usr/bin/passwd": 0o4755, "/usr/bin/su": 0o4751}
    for path, expected_mode in required_modes.items():
        entry = entries_by_path.get(path)
        if entry is None:
            errors.append("profile does not install required privileged executable %s" % path)
        elif entry.kind != "p" or entry.mode != expected_mode:
            errors.append(
                "%s: %s mode is %04o, expected packed mode %04o"
                % (entry.line.where(), path, entry.mode, expected_mode)
            )
    chosen = set(selected)
    for closure in selected:
        for other in decl.needs_closure.get(closure, []):
            if not set(expand(decl, other)) <= chosen:
                errors.append(
                    "closure %s needs closure %s, which this profile does not select"
                    % (closure, other)
                )
        for required in decl.needs_path.get(closure, []):
            if required not in installed:
                errors.append(
                    "closure %s needs %s, which this profile does not install"
                    % (closure, required)
                )
    for host, (path, members) in sorted(decl.multicalls.items()):
        if host not in chosen:
            continue
        kept = [member for member in members if member in chosen]
        if not kept:
            errors.append(
                "closure %s is selected with none of its members (%s); the binary "
                "would dispatch nothing" % (host, " ".join(members))
            )
        linked = sorted(
            posixpath.basename(entry.path)
            for entry in entries
            if entry.kind == "l" and entry.target == path
        )
        for member in kept:
            if member not in linked:
                errors.append(
                    "multicall %s dispatches %s, which no link in this profile reaches"
                    % (host, member)
                )
        for name in linked:
            if name not in kept:
                errors.append(
                    "link %s reaches %s, which does not dispatch it in this profile"
                    % (posixpath.join(posixpath.dirname(path), name), path)
                )
    return errors


def describe(profile, with_names, without_names):
    """Name a profile and the per-build changes applied to it."""
    parts = [profile]
    if with_names:
        parts.append("with " + " ".join(with_names))
    if without_names:
        parts.append("without " + " ".join(without_names))
    return " ".join(parts)


def report(profile, errors):
    """Print a verdict; return True when the composition ships."""
    if not errors:
        return True
    print("mkmanifest: profile %s does not compose:" % profile, file=sys.stderr)
    for error in errors:
        print("  %s" % error, file=sys.stderr)
    return False


def emit(lines, output):
    """Write the composed manifest, replacing any earlier one atomically.

    A file that already holds the same text keeps its modification time, so
    a rule that recomposes on every invocation rebuilds what depends on the
    manifest only when the selection changed it.
    """
    body = "".join(line.text + "\n" for line in lines)
    try:
        with open(output, encoding="utf-8") as fh:
            if fh.read() == body:
                return
    except OSError:
        pass
    temporary = output + ".new"
    with open(temporary, "w", encoding="utf-8") as fh:
        fh.write(body)
    os.replace(temporary, output)






def load(args):
    """Read all inputs and hold their closure sets to each other."""
    lines = []
    closures = []
    for manifest in args.manifest:
        more, opened = read_marked(manifest)
        duplicates = sorted(set(closures).intersection(opened))
        if duplicates:
            raise ManifestError(
                "%s repeats closure %s" % (manifest, ", ".join(duplicates))
            )
        lines.extend(more)
        closures.extend(opened)
    appended = []
    for extra in args.append:
        more, opened = read_marked(extra)
        if opened:
            raise ManifestError(
                "%s: closure markers belong in --manifest inputs" % extra
            )
        appended.extend(more)
    decl = read_profiles(args.profiles)
    decl.closures = closures
    selected_extra = []
    for closure in args.select_closure:
        if closure in selected_extra:
            raise ManifestError("--select-closure names %s twice" % closure)
        selected_extra.append(closure)
    errors = check_declarations(decl, lines, selected_extra)
    if errors:
        raise ManifestError(
            "%s and %s disagree:\n  %s"
            % (", ".join(args.profiles), ", ".join(args.manifest), "\n  ".join(errors))
        )
    return lines, appended, decl, selected_extra


def run_profile(profile, lines, appended, decl, selected_extra, with_names=(), without_names=()):
    """Compose one declared profile and decide it; return (selected, kept, errors)."""
    selected = select(decl, decl.profiles[profile], selected_extra, with_names, without_names)
    kept = compose(lines, selected)
    errors = verify(parse_entries(kept + appended), selected, decl)
    return selected, kept, errors


def summarize(label, kept, appended):
    """Print what the composition installs, for the build log."""
    entries = parse_entries(kept + appended)
    counts = {}
    for entry in entries:
        counts[entry.kind] = counts.get(entry.kind, 0) + 1
    print(
        "mkmanifest: profile %s: %d directories, %d files, %d devices, %d links, %d symlinks"
        % (
            label,
            counts.get("d", 0),
            counts.get("f", 0) + counts.get("p", 0),
            counts.get("b", 0) + counts.get("c", 0),
            counts.get("l", 0),
            counts.get("s", 0),
        )
    )


def format_cost(decl, name):
    figures = decl.costs.get(name)
    if figures is None:
        return "-", "-"
    return str(figures[0]), str(figures[1])


def list_closures(decl, lines, selected):
    """Print every closure, group and profile with dependencies and cost.

    The block and inode columns are the measured figures that `cost` lines
    in the profile file record (the method and the table are in
    sys/arch/rp2040/doc/PROFILES.md); the install column is computed from
    the manifest. A dash marks a closure with no recorded measurement.
    """
    chosen = set(selected or [])
    rows = [("", "closure", "groups", "needs closure", "blocks", "inodes", "installs")]
    for closure in decl.closures:
        entries = closure_entries(lines, closure)
        installs = " ".join(
            entry.path for entry in entries if entry.kind in ("d", "f", "p", "l", "s")
        )
        blocks, inodes = format_cost(decl, closure)
        rows.append((
            "*" if closure in chosen else "",
            closure,
            ",".join(groups_of(decl, closure)) or "-",
            ",".join(decl.needs_closure.get(closure, [])) or "-",
            blocks,
            inodes,
            installs or "-",
        ))
    widths = [max(len(row[col]) for row in rows) for col in range(6)]
    for row in rows:
        print("  ".join(cell.ljust(widths[col]) for col, cell in enumerate(row[:6]))
              + "  " + row[6])
    print()
    print("groups (a group name stands for its members in a profile, --with and --without):")
    for name in decl.group_order:
        blocks, inodes = format_cost(decl, name)
        print("  %-12s %s  [blocks %s, inodes %s]"
              % (name, " ".join(decl.groups[name]), blocks, inodes))
    print()
    print("multicall binaries (the Make variable <HOST>_TOOLS carries the selected members):")
    for host, (path, members) in decl.multicalls.items():
        print("  %-12s %s: %s" % (host, path, " ".join(members)))
    print()
    print("profiles:")
    for name in decl.order:
        print("  %-12s %s" % (name, " ".join(decl.profiles[name]) or "(base only)"))
    print()
    print("* marks a closure the current profile and selection keep. Blocks and inodes are")
    print("measured, not computed: the free counts an image of base plus the closure and the")
    print("closures it needs loses (fsutil --verbose), recorded as cost lines in the profiles")
    print("file; '-' marks a closure with no recorded measurement.")


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




def new_case(verdict, label, names, expected=None, removed=None, with_names=(),
             without_names=(), needs_path=None, members=None):
    return {
        "verdict": verdict,
        "label": label,
        "names": list(names),
        "expected": expected,
        "removed": removed,
        "with": list(with_names),
        "without": list(without_names),
        "needs_path": needs_path,
        "members": members,
    }


def run_case(case, lines, appended, decl, selected_extra):
    """Decide one calibration case; return (selected, reasons it cannot ship)."""
    local = decl
    if case["needs_path"]:
        local = decl.copy()
        for closure, paths in case["needs_path"].items():
            local.needs_path.setdefault(closure, []).extend(paths)
    try:
        selected = select(
            local, case["names"], selected_extra, case["with"], case["without"]
        )
    except ManifestError as error:
        return None, [str(error)]
    kept = compose(lines, selected)
    if case["removed"]:
        kept = drop(kept, case["removed"])
    return selected, verify(parse_entries(kept + appended), selected, local)


def declaration_cases():
    """Declaration text that check_declarations must refuse, with the reason."""
    return [
        ("group declared twice", "group games keen\n", "group games is declared twice"),
        ("group that contains itself",
         "group loop-a loop-b\ngroup loop-b loop-a\n", "contains itself"),
        ("group that shares a closure's name", "group keen fifteen\n",
         "group keen is also a closure"),
        ("group member that names nothing", "group ghosts nosuch\n",
         "nosuch, which is neither a closure nor a group"),
        ("cost of a closure no manifest opens", "cost nosuch 1 1\n",
         "cost names nosuch, which the manifest never opens"),
        ("dependency on a closure no manifest opens", "closure keen needs closure nosuch\n",
         "needs closure nosuch, which the manifest never opens"),
        ("multicall member without a link to its binary",
         "multicall coremark /usr/bin/coremark keen\n",
         "closure keen installs no link /usr/bin/keen to /usr/bin/coremark"),
        ("multicall binary its closure never installs",
         "multicall calc /usr/bin/nothere flood\n",
         "closure calc installs no file /usr/bin/nothere"),
        ("multicall member claimed by two binaries",
         "multicall coremark /usr/bin/coremark keen\n",
         "member keen already belongs to"),
        ("profile line that names nothing", "profile ghostly nosuch\n",
         "profile ghostly names closure nosuch, which the manifest never opens"),
    ]


def selftest(lines, appended, decl, selected_extra):
    """Calibrate the checker: every case below must land on its stated side."""
    full = decl.profiles["full"]
    cases = []
    for name in sorted(decl.profiles):
        cases.append(new_case("accept", "declared profile %s" % name, decl.profiles[name]))
    cases.append(new_case(
        "reject", "compiler without its link library",
        ["coremark", "toolchain", "games"], "needs closure toolchainlib"))
    cases.append(new_case(
        "reject", "link library without its compiler", ["toolchainlib"],
        "closure toolchainlib needs closure cc"))
    cases.append(new_case(
        "reject", "gamebox entry points without gamebox", ["games"],
        "hard link /usr/games/keen has no source", removed="pack /usr/games/gamebox"))
    cases.append(new_case(
        "reject", "puzzlebox entry points without puzzlebox", ["games", "puzzles"],
        "hard link /usr/games/flood has no source", removed="pack /usr/games/puzzlebox"))
    cases.append(new_case(
        "reject", "puzzlebox without the games directory it installs into", ["puzzles"],
        "closure puzzlebox needs closure gamesdir"))
    cases.append(new_case(
        "reject", "compiler without /usr/lib/libc.a", ["toolchain", "toolchainlib"],
        "needs /usr/lib/libc.a", removed="file /usr/lib/libc.a"))
    cases.append(new_case(
        "reject", "calc-bignum without dc", ["calc-bignum"],
        "needs /usr/bin/dc", removed="pack /usr/bin/dc"))
    cases.append(new_case(
        "reject", "bc without its -l library", ["calc-bignum"],
        "needs /usr/share/misc/bc.library", removed="file /usr/share/misc/bc.library"))
    cases.append(new_case(
        "reject", "gamebox without its directory", ["games"],
        "needs directory /usr/games", removed="dir /usr/games"))
    cases.append(new_case(
        "reject", "gamebox entry point whose link the image drops",
        ["games"], "multicall gamebox dispatches keen, which no link in this profile reaches",
        removed="link /usr/games/keen"))
    # Per-build selection: the Make variables IMAGE_WITH and IMAGE_WITHOUT.
    cases.append(new_case(
        "reject", "--with of an unknown name", full,
        "--with names unknown closure or group nosuch", with_names=["nosuch"]))
    cases.append(new_case(
        "reject", "--without of an unknown name", full,
        "--without names unknown closure or group nosuch", without_names=["nosuch"]))
    cases.append(new_case(
        "reject", "a closure both added and removed", full,
        "closure calc is both added (as calc) and removed (as calc)",
        with_names=["calc"], without_names=["calc"]))
    cases.append(new_case(
        "reject", "a group added while one of its members is removed", full,
        "closure keen is both added (as games) and removed (as keen)",
        with_names=["games"], without_names=["keen"]))
    cases.append(new_case(
        "reject", "toolchain removed while toolchainlib stays", full,
        "closure toolchainlib needs closure cc, which this profile does not select",
        without_names=["toolchain"]))
    cases.append(new_case(
        "reject", "gamebox removed while its members stay", full,
        "closure keen needs closure gamebox, which this profile does not select",
        without_names=["gamebox"]))
    cases.append(new_case(
        "reject", "every gamebox member removed while gamebox stays", full,
        "closure gamebox is selected with none of its members",
        without_names=["fifteen", "keen", "bubble", "fourfall", "coil"]))
    cases.append(new_case(
        "reject", "a member removed whose link another selected part needs",
        decl.profiles["picocalc"],
        "closure calc needs /usr/games/keen, which this profile does not install",
        without_names=["keen"], needs_path={"calc": ["/usr/games/keen"]}))
    cases.append(new_case(
        "accept", "toolchain and toolchainlib removed together", full,
        without_names=["toolchain", "toolchainlib"]))
    cases.append(new_case(
        "accept", "one gamebox member removed", full, without_names=["keen"],
        members={"gamebox": ["fifteen", "bubble", "fourfall", "coil"],
                 "puzzlebox": ["flood", "samegame", "unruly", "lightup"]}))
    cases.append(new_case(
        "accept", "puzzles kept and every gamebox member removed", full,
        without_names=["gamebox", "fifteen", "keen", "bubble", "fourfall", "coil"],
        members={"gamebox": [], "puzzlebox": ["flood", "samegame", "unruly", "lightup"]}))
    cases.append(new_case(
        "accept", "a single puzzle on the base system", [],
        with_names=["gamesdir", "puzzlebox", "lightup"],
        members={"gamebox": [], "puzzlebox": ["lightup"]}))
    cases.append(new_case(
        "accept", "dc without bc", full, with_names=["dc"]))
    cases.append(new_case(
        "accept", "assembler and linker without the compiler", [],
        with_names=["as", "ld"]))
    cases.append(new_case(
        "accept", "picocalc with the arbitrary-precision calculator removed",
        decl.profiles["picocalc"], without_names=["calc-bignum"]))
    failures = 0
    for case in cases:
        label = case["label"]
        selected, errors = run_case(case, lines, appended, decl, selected_extra)
        joined = "\n".join(errors)
        if case["verdict"] == "accept":
            if errors:
                failures += 1
                print("selftest: %s must compose, and does not:" % label, file=sys.stderr)
                for error in errors:
                    print("  %s" % error, file=sys.stderr)
                continue
            wrong = [
                host for host, wanted in sorted((case["members"] or {}).items())
                if selected_members(decl, selected, host) != wanted
            ]
            if wrong:
                failures += 1
                print("selftest: %s keeps the wrong members of %s" % (label, " ".join(wrong)),
                      file=sys.stderr)
            else:
                print("selftest: accept %s" % label)
        else:
            expected = case["expected"]
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

    for label, text, expected in declaration_cases():
        mutated = decl.copy()
        reasons = []
        try:
            parse_declarations(mutated, "selftest", text)
            reasons = check_declarations(mutated, lines, selected_extra)
        except ManifestError as error:
            reasons = [str(error)]
        if expected not in "\n".join(reasons):
            failures += 1
            print(
                "selftest: %s must be refused for '%s':\n%s"
                % (label, expected, "\n".join(reasons) or "declaration accepted"),
                file=sys.stderr,
            )
        else:
            print("selftest: reject %s -- %s" % (label, expected))

    selected = select(decl, full, selected_extra)
    mode_cases = (
        (
            "passwd without setuid mode",
            "mode 04755",
            "/usr/bin/passwd mode is 0775, expected packed mode 4755",
        ),
        (
            "su without setuid mode",
            "mode 04751",
            "/usr/bin/su mode is 0775, expected packed mode 4751",
        ),
    )
    for label, removed_mode, expected_mode_error in mode_cases:
        bad_mode_lines = drop(lines, removed_mode)
        bad_mode_entries = parse_entries(compose(bad_mode_lines, selected) + appended)
        mode_errors = verify(bad_mode_entries, selected, decl)
        if expected_mode_error not in "\n".join(mode_errors):
            failures += 1
            print(
                "selftest: reject %s -- expected '%s', got:\n%s"
                % (label, expected_mode_error, "\n".join(mode_errors) or "composition accepted"),
                file=sys.stderr,
            )
        else:
            print("selftest: reject %s -- %s" % (label, expected_mode_error))

    if "pdp11" in selected_extra and "v6disk" in selected_extra:
        legacy_cases = [
            (
                "emulator without its V6 pack",
                ["pdp11"],
                None,
                "needs closure v6disk",
            ),
            (
                "PDP-11 closure without its emulator",
                ["pdp11", "v6disk"],
                "pack /usr/bin/pdp11",
                "needs /usr/bin/pdp11",
            ),
            (
                "V6 closure without its guest pack",
                ["pdp11", "v6disk"],
                "file /usr/v6/root.rk",
                "needs /usr/v6/root.rk",
            ),
        ]
        for label, selected, removed, expected in legacy_cases:
            kept = compose(lines, selected)
            if removed:
                kept = drop(kept, removed)
            errors = verify(parse_entries(kept + appended), selected, decl)
            joined = "\n".join(errors)
            if expected not in joined:
                failures += 1
                print(
                    "selftest: %s must be rejected for '%s':\n%s"
                    % (label, expected, joined or "composition accepted"),
                    file=sys.stderr,
                )
            else:
                print("selftest: reject %s -- %s" % (label, expected))
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument(
        "--manifest",
        action="append",
        required=True,
        help="a closure-marked manifest; repeat for an opt-in closure set",
    )
    parser.add_argument(
        "--profiles",
        action="append",
        required=True,
        help="a profile or closure declaration file; repeat for opt-in declarations",
    )
    parser.add_argument(
        "--select-closure",
        action="append",
        default=[],
        help="select an opt-in closure in addition to the named profile",
    )
    parser.add_argument(
        "--with",
        dest="with_names",
        action="append",
        default=[],
        metavar="NAME",
        help="add a closure or group to the profile for this build; repeatable",
    )
    parser.add_argument(
        "--without",
        dest="without_names",
        action="append",
        default=[],
        metavar="NAME",
        help="remove a closure or group from the profile for this build; repeatable",
    )
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
    parser.add_argument(
        "--list-closures",
        action="store_true",
        help="print each closure with its groups, dependencies, cost and install paths",
    )
    parser.add_argument(
        "--members",
        metavar="HOST",
        help="print the members of multicall closure HOST that the profile keeps",
    )
    args = parser.parse_args()

    try:
        lines, appended, decl, selected_extra = load(args)
    except (OSError, ManifestError) as error:
        print("mkmanifest: %s" % error, file=sys.stderr)
        return 2

    if args.list:
        for name in decl.order:
            print(" ".join([name] + decl.profiles[name]))
        return 0

    try:
        if args.selftest:
            return 1 if selftest(lines, appended, decl, selected_extra) else 0

        if args.check_all:
            failures = 0
            for name in decl.order:
                _, kept, errors = run_profile(name, lines, appended, decl, selected_extra)
                if report(name, errors):
                    summarize(name, kept, appended)
                else:
                    failures += 1
            return 1 if failures else 0

        if args.profile and args.profile not in decl.profiles:
            print(
                "mkmanifest: %s declares no profile %s; it declares %s"
                % (", ".join(args.profiles), args.profile, " ".join(decl.order)),
                file=sys.stderr,
            )
            return 2

        if args.list_closures:
            selected = None
            if args.profile:
                selected = select(decl, decl.profiles[args.profile], selected_extra,
                                  args.with_names, args.without_names)
            list_closures(decl, lines, selected)
            return 0

        if not args.profile:
            print("mkmanifest: name a profile, --check-all, --selftest, --list "
                  "or --list-closures", file=sys.stderr)
            return 2
        label = describe(args.profile, args.with_names, args.without_names)
        selected, kept, errors = run_profile(
            args.profile, lines, appended, decl, selected_extra,
            args.with_names, args.without_names,
        )
        if args.members:
            print(" ".join(selected_members(decl, selected, args.members)))
            return 0
        if not report(label, errors):
            return 1
        summarize(label, kept, appended)
        if args.output:
            emit(kept, args.output)
    except (OSError, ManifestError) as error:
        print("mkmanifest: %s" % error, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
