"""Check changed C comments for explicit development-history narration."""

import argparse
import bisect
import os
import re
import stat
import subprocess
import sys
import tempfile
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REGULAR_MODES = {"100644", "100755"}
ZERO_MODE = "000000"
ADVISORY_LINE_THRESHOLD = 12
TRIGRAPHS = {
    ord("="): ord("#"),
    ord("("): ord("["),
    ord("/"): ord("\\"),
    ord(")"): ord("]"),
    ord("'"): ord("^"),
    ord("<"): ord("{"),
    ord("!"): ord("|"),
    ord(">"): ord("}"),
    ord("-"): ord("~"),
}
RULES = (
    ("CH001_IN_THIS_PR", re.compile(rb"\bin\s+this\s+(?:pr|pull\s+request)\b", re.I)),
    ("CH002_BEFORE_THIS_PATCH", re.compile(rb"\bbefore\s+this\s+patch\b", re.I)),
    ("CH003_AFTER_THIS_COMMIT", re.compile(rb"\bafter\s+this\s+commit\b", re.I)),
    ("CH004_REVIEWER_REQUEST", re.compile(rb"\bthe\s+reviewer\s+requested\b", re.I)),
)


class InfrastructureError(Exception):
    """Git or a selected source snapshot cannot supply reliable evidence."""


class LexicalError(InfrastructureError):
    """A selected C source cannot be tokenized without recovery."""


@dataclass(frozen=True)
class RawChange:
    old_mode: str
    new_mode: str
    old_object: str
    new_object: str
    status: str
    old_path: bytes
    new_path: bytes


@dataclass(frozen=True)
class TranslatedSource:
    values: bytes
    physical_starts: tuple
    physical_ends: tuple


@dataclass(frozen=True)
class Comment:
    kind: str
    logical_start: int
    logical_end: int
    physical_start: int
    physical_end: int
    start_line: int
    end_line: int
    body: bytes


@dataclass(frozen=True)
class CommentGroup:
    kind: str
    logical_start: int
    logical_end: int
    physical_start: int
    physical_end: int
    start_line: int
    end_line: int
    body: bytes
    members: tuple


@dataclass(frozen=True)
class MatchBlock:
    before_start: int
    after_start: int
    size: int


@dataclass(frozen=True)
class Diagnostic:
    rule_id: str
    path: bytes
    line: int
    detail: str


@dataclass(frozen=True)
class Advisory:
    path: bytes
    start_line: int
    end_line: int
    lines: int


@dataclass
class CheckResult:
    mode: str
    base: str
    after: str
    changed_paths: int = 0
    candidate_files: int = 0
    changed_comments: int = 0
    non_c_exclusions: int = 0
    deleted_exclusions: int = 0
    diagnostics: tuple = ()
    advisories: tuple = ()


def run_git(root, *arguments):
    environment = os.environ.copy()
    environment["GIT_EXTERNAL_DIFF"] = ""
    environment["GIT_DIFF_OPTS"] = ""
    result = subprocess.run(
        ["git", "-C", os.fspath(root), *arguments],
        env=environment,
        capture_output=True,
        check=False,
    )
    if result.returncode:
        command = "git " + " ".join(arguments)
        message = result.stderr.decode("utf-8", "replace").strip()
        raise InfrastructureError(f"{command}: {message or 'command failed'}")
    return result.stdout


def resolve_commit(root, revision):
    output = run_git(root, "rev-parse", "--verify", f"{revision}^{{commit}}")
    object_id = output.decode("ascii").strip()
    if not re.fullmatch(r"[0-9a-f]+", object_id):
        raise InfrastructureError(f"{revision}: Git returned an invalid commit object ID")
    return object_id


def read_blob(root, object_id):
    if not object_id or set(object_id) == {"0"}:
        return b""
    return run_git(root, "cat-file", "blob", object_id)


def display_path(path):
    decoded = path.decode("utf-8", "surrogateescape")
    escaped = []
    named_controls = {"\t": r"\t", "\n": r"\n", "\r": r"\r"}
    for character in decoded:
        value = ord(character)
        if character == "\\":
            escaped.append(r"\\")
        elif character in named_controls:
            escaped.append(named_controls[character])
        elif 0xDC80 <= value <= 0xDCFF:
            escaped.append(f"\\x{value - 0xDC00:02x}")
        elif value < 0x20 or 0x7F <= value < 0xA0:
            escaped.append(f"\\x{value:02x}")
        elif not character.isprintable():
            width = 4 if value <= 0xFFFF else 8
            prefix = "u" if width == 4 else "U"
            escaped.append(f"\\{prefix}{value:0{width}x}")
        else:
            escaped.append(character)
    return "".join(escaped)


def is_c_path(path):
    return path.endswith((b".c", b".h"))


def parse_raw_changes(output):
    records = output.split(b"\0")
    changes = []
    position = 0
    while position < len(records):
        header = records[position]
        position += 1
        if not header:
            continue
        if not header.startswith(b":"):
            raise InfrastructureError("Git raw diff contains a malformed record header")
        try:
            metadata = header[1:].decode("ascii").split()
        except UnicodeDecodeError as error:
            raise InfrastructureError("Git raw diff metadata is not ASCII") from error
        if len(metadata) != 5:
            raise InfrastructureError("Git raw diff contains incomplete metadata")
        old_mode, new_mode, old_object, new_object, status = metadata
        if not re.fullmatch(r"[A-Z][0-9]*", status):
            raise InfrastructureError(f"Git raw diff has invalid status {status!r}")
        if position >= len(records) or not records[position]:
            raise InfrastructureError("Git raw diff is missing a pathname")
        first_path = records[position]
        position += 1
        if status[0] in {"R", "C"}:
            if position >= len(records) or not records[position]:
                raise InfrastructureError("Git raw rename or copy is missing its destination")
            old_path = first_path
            new_path = records[position]
            position += 1
        else:
            old_path = first_path
            new_path = first_path
        changes.append(RawChange(old_mode, new_mode, old_object, new_object,
                                 status, old_path, new_path))
    return changes


def raw_changes(root, mode, base, after=None):
    common = ("--raw", "-z", "--no-abbrev", "--find-renames",
              "--no-ext-diff", "--no-textconv")
    if mode == "working":
        output = run_git(root, "diff", *common, base, "--")
    elif mode == "staged":
        output = run_git(root, "diff", "--cached", *common, base, "--")
    elif mode == "revision":
        output = run_git(root, "diff-tree", "-r", "--no-commit-id", *common,
                         base, after, "--")
    else:
        raise InfrastructureError(f"unsupported mode {mode!r}")
    return parse_raw_changes(output)


def index_entries(root):
    entries = {}
    output = run_git(root, "ls-files", "--stage", "-z")
    for record in output.split(b"\0"):
        if not record:
            continue
        try:
            metadata, path = record.split(b"\t", 1)
            mode, object_id, stage = metadata.decode("ascii").split()
        except (ValueError, UnicodeDecodeError) as error:
            raise InfrastructureError("Git index contains a malformed entry") from error
        if stage != "0":
            if is_c_path(path):
                raise InfrastructureError(
                    f"{display_path(path)}: unresolved index stage {stage}"
                )
            continue
        entries[path] = (mode, object_id)
    return entries


def read_working_file(root, path):
    raw_root = os.fsencode(root)
    full_path = os.path.join(raw_root, path)
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(full_path, flags)
    except OSError as error:
        raise InfrastructureError(
            f"{display_path(path)}: cannot open working file: {error}"
        ) from error
    try:
        file_status = os.fstat(descriptor)
        if not stat.S_ISREG(file_status.st_mode):
            raise InfrastructureError(f"{display_path(path)}: working input is not a regular file")
        chunks = []
        while True:
            chunk = os.read(descriptor, 1024 * 1024)
            if not chunk:
                break
            chunks.append(chunk)
        return b"".join(chunks)
    finally:
        os.close(descriptor)


def translate_source(contents):
    phase_one = []
    position = 0
    while position < len(contents):
        if (position + 2 < len(contents) and contents[position:position + 2] == b"??"
                and contents[position + 2] in TRIGRAPHS):
            phase_one.append((TRIGRAPHS[contents[position + 2]], position, position + 3))
            position += 3
        else:
            phase_one.append((contents[position], position, position + 1))
            position += 1

    values = bytearray()
    starts = []
    ends = []
    position = 0
    while position < len(phase_one):
        value, start, end = phase_one[position]
        if value == ord("\\") and position + 1 < len(phase_one):
            next_value = phase_one[position + 1][0]
            if next_value == ord("\n"):
                position += 2
                continue
            if (next_value == ord("\r") and position + 2 < len(phase_one)
                    and phase_one[position + 2][0] == ord("\n")):
                position += 3
                continue
        values.append(value)
        starts.append(start)
        ends.append(end)
        position += 1
    return TranslatedSource(bytes(values), tuple(starts), tuple(ends))


def line_starts(contents):
    starts = [0]
    position = 0
    while position < len(contents):
        if contents[position:position + 2] == b"\r\n":
            position += 2
            starts.append(position)
        elif contents[position] in (ord("\r"), ord("\n")):
            position += 1
            starts.append(position)
        else:
            position += 1
    return starts


def physical_line(starts, offset):
    return bisect.bisect_right(starts, offset)


def comment_from_span(kind, start, end, translated, lines):
    physical_start = translated.physical_starts[start]
    physical_end = translated.physical_ends[end - 1]
    body_start = start + 2
    body_end = end - 2 if kind == "block" else end
    return Comment(
        kind,
        start,
        end,
        physical_start,
        physical_end,
        physical_line(lines, physical_start),
        physical_line(lines, max(physical_start, physical_end - 1)),
        translated.values[body_start:body_end],
    )


def lex_comments(contents, path=b"<memory>"):
    translated = translate_source(contents)
    values = translated.values
    lines = line_starts(contents)
    comments = []
    position = 0
    while position < len(values):
        value = values[position]
        if value == ord("/") and position + 1 < len(values):
            following = values[position + 1]
            if following == ord("*"):
                end = values.find(b"*/", position + 2)
                if end < 0:
                    line = physical_line(lines, translated.physical_starts[position])
                    raise LexicalError(
                        f"{display_path(path)}:{line}: unterminated block comment"
                    )
                end += 2
                comments.append(comment_from_span("block", position, end,
                                                  translated, lines))
                position = end
                continue
            if following == ord("/"):
                end = position + 2
                while end < len(values) and values[end] not in (ord("\n"), ord("\r")):
                    end += 1
                comments.append(comment_from_span("line", position, end,
                                                  translated, lines))
                position = end
                continue
        if value in (ord('"'), ord("'")):
            quote = value
            opening = position
            closed = False
            position += 1
            while position < len(values):
                value = values[position]
                if value == ord("\\"):
                    if position + 1 >= len(values):
                        break
                    position += 2
                    continue
                if value == quote:
                    position += 1
                    closed = True
                    break
                if value in (ord("\n"), ord("\r")):
                    line = physical_line(lines, translated.physical_starts[opening])
                    kind = "string" if quote == ord('"') else "character literal"
                    raise LexicalError(
                        f"{display_path(path)}:{line}: unterminated {kind}"
                    )
                position += 1
            if not closed:
                line = physical_line(lines, translated.physical_starts[opening])
                kind = "string" if quote == ord('"') else "character literal"
                raise LexicalError(f"{display_path(path)}:{line}: unterminated {kind}")
            continue
        position += 1
    return comments


def comment_groups(contents, comments):
    translated = translate_source(contents)
    groups = []
    position = 0
    while position < len(comments):
        comment = comments[position]
        if comment.kind == "block":
            groups.append(comment)
            position += 1
            continue
        members = [comment]
        position += 1
        while position < len(comments) and comments[position].kind == "line":
            following = comments[position]
            separator = translated.values[
                members[-1].logical_end:following.logical_start
            ]
            if not re.fullmatch(rb"(?:\r\n|\r|\n)[ \t]*", separator):
                break
            members.append(following)
            position += 1
        groups.append(CommentGroup(
            "line-group",
            members[0].logical_start,
            members[-1].logical_end,
            members[0].physical_start,
            members[-1].physical_end,
            members[0].start_line,
            members[-1].end_line,
            b" ".join(member.body for member in members),
            tuple(members),
        ))
    return groups


def source_line_starts(contents):
    starts = [0]
    starts.extend(index + 1 for index, value in enumerate(contents) if value == ord("\n"))
    return starts


def source_line_offset(contents, starts, line):
    if line < 1:
        raise InfrastructureError(f"Git word diff returned invalid source line {line}")
    if line <= len(starts):
        return starts[line - 1]
    if line == len(starts) + 1:
        return len(contents)
    raise InfrastructureError(f"Git word diff returned out-of-range source line {line}")


def hunk_start_offset(contents, starts, line, count):
    return source_line_offset(contents, starts, line if count else line + 1)


def hunk_end_offset(contents, starts, line, count):
    if not count:
        return hunk_start_offset(contents, starts, line, count)
    return source_line_offset(contents, starts, line + count)


def append_match(blocks, before_start, after_start, size):
    if not size:
        return
    if (blocks and blocks[-1].before_start + blocks[-1].size == before_start
            and blocks[-1].after_start + blocks[-1].size == after_start):
        previous = blocks[-1]
        blocks[-1] = MatchBlock(previous.before_start, previous.after_start,
                                previous.size + size)
    else:
        blocks.append(MatchBlock(before_start, after_start, size))


def newline_at(contents, position):
    if contents[position:position + 2] == b"\r\n":
        return b"\r\n"
    if contents[position:position + 1] == b"\n":
        return b"\n"
    return b""


def consume_payload(contents, position, end, payload, label):
    segments = []
    payload_position = 0
    while payload_position < len(payload):
        if position >= end:
            raise InfrastructureError(
                f"Git word diff {label} payload exceeds its hunk"
            )
        if contents[position] == payload[payload_position]:
            if (segments
                    and segments[-1][0] + segments[-1][2] == payload_position
                    and segments[-1][1] + segments[-1][2] == position):
                segments[-1][2] += 1
            else:
                segments.append([payload_position, position, 1])
            position += 1
            payload_position += 1
            continue
        newline = newline_at(contents, position)
        if not newline:
            raise InfrastructureError(
                f"Git word diff {label} payload does not match source"
            )
        position += len(newline)
    return position, tuple(tuple(segment) for segment in segments)


def consume_omitted_newlines(contents, position, end, label):
    while position < end:
        newline = newline_at(contents, position)
        if not newline or position + len(newline) > end:
            raise InfrastructureError(
                f"Git word diff omitted non-newline {label} bytes"
            )
        position += len(newline)
    return position


def append_context_matches(blocks, old_segments, new_segments):
    old_index = 0
    new_index = 0
    while old_index < len(old_segments) and new_index < len(new_segments):
        old_payload, old_source, old_size = old_segments[old_index]
        new_payload, new_source, new_size = new_segments[new_index]
        overlap_start = max(old_payload, new_payload)
        overlap_end = min(old_payload + old_size, new_payload + new_size)
        if overlap_start < overlap_end:
            append_match(
                blocks,
                old_source + overlap_start - old_payload,
                new_source + overlap_start - new_payload,
                overlap_end - overlap_start,
            )
        if old_payload + old_size <= new_payload + new_size:
            old_index += 1
        if new_payload + new_size <= old_payload + old_size:
            new_index += 1


def append_equivalent_region(blocks, before, after, old_start, old_end,
                             new_start, new_end):
    old_position = old_start
    new_position = new_start
    while old_position < old_end and new_position < new_end:
        if before[old_position] == after[new_position]:
            append_match(blocks, old_position, new_position, 1)
            old_position += 1
            new_position += 1
        elif (before[old_position:old_position + 2] == b"\r\n"
              and after[new_position:new_position + 1] == b"\n"):
            old_position += 1
        elif (before[old_position:old_position + 1] == b"\n"
              and after[new_position:new_position + 2] == b"\r\n"):
            new_position += 1
        else:
            raise InfrastructureError("Git word diff omitted unequal inter-hunk bytes")
    if old_position != old_end or new_position != new_end:
        raise InfrastructureError("Git word diff omitted unequal inter-hunk bytes")


def word_diff_output(before, after):
    if before == after:
        return b""
    with tempfile.TemporaryDirectory(prefix="discobsd-comment-diff-") as temporary:
        directory = Path(temporary)
        before_path = directory / "before.c"
        after_path = directory / "after.c"
        before_path.write_bytes(before)
        after_path.write_bytes(after)
        environment = os.environ.copy()
        environment["GIT_EXTERNAL_DIFF"] = ""
        environment["GIT_DIFF_OPTS"] = ""
        environment["LC_ALL"] = "C"
        result = subprocess.run(
            [
                "git", "-c", "core.autocrlf=false", "-c", "core.eol=lf",
                "diff", "--no-index", "--text", "--no-color",
                "--no-ext-diff", "--no-textconv", "--no-renames", "--unified=0",
                "--word-diff=porcelain", "--word-diff-regex=.",
                os.fspath(before_path), os.fspath(after_path),
            ],
            env=environment,
            capture_output=True,
            check=False,
        )
    if result.returncode != 1:
        message = result.stderr.decode("utf-8", "replace").strip()
        raise InfrastructureError(
            f"Git word diff exited {result.returncode}: {message or 'unexpected status'}"
        )
    return result.stdout


def normalized_matching_blocks(before, after):
    if before == after:
        return (MatchBlock(0, 0, len(before)),) if before else ()
    output = word_diff_output(before, after)
    lines = output.split(b"\n")
    if lines and not lines[-1]:
        lines.pop()
    hunk_pattern = re.compile(
        rb"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@"
    )
    before_starts = source_line_starts(before)
    after_starts = source_line_starts(after)
    blocks = []
    before_previous = 0
    after_previous = 0
    position = 0
    saw_hunk = False
    while position < len(lines):
        match = hunk_pattern.match(lines[position])
        if not match:
            position += 1
            continue
        saw_hunk = True
        old_line = int(match.group(1))
        old_count = int(match.group(2) or b"1")
        new_line = int(match.group(3))
        new_count = int(match.group(4) or b"1")
        old_start = hunk_start_offset(before, before_starts, old_line, old_count)
        new_start = hunk_start_offset(after, after_starts, new_line, new_count)
        append_equivalent_region(
            blocks, before, after,
            before_previous, old_start, after_previous, new_start,
        )
        old_position = old_start
        new_position = new_start
        old_end = hunk_end_offset(before, before_starts, old_line, old_count)
        new_end = hunk_end_offset(after, after_starts, new_line, new_count)
        position += 1
        while position < len(lines) and not hunk_pattern.match(lines[position]):
            line = lines[position]
            if line == b"~":
                old_newline = (
                    newline_at(before, old_position)
                    if old_position < old_end else b""
                )
                new_newline = (
                    newline_at(after, new_position)
                    if new_position < new_end else b""
                )
                if old_newline and old_newline == new_newline:
                    append_match(blocks, old_position, new_position, len(old_newline))
                old_position += len(old_newline)
                new_position += len(new_newline)
            elif line[:1] in {b" ", b"-", b"+"}:
                marker = line[:1]
                payload = line[1:]
                old_segments = ()
                new_segments = ()
                if marker in {b" ", b"-"}:
                    old_position, old_segments = consume_payload(
                        before, old_position, old_end, payload, "before"
                    )
                if marker in {b" ", b"+"}:
                    new_position, new_segments = consume_payload(
                        after, new_position, new_end, payload, "after"
                    )
                if marker == b" ":
                    append_context_matches(blocks, old_segments, new_segments)
            else:
                raise InfrastructureError("Git word diff returned an unknown hunk record")
            position += 1
        old_position = consume_omitted_newlines(
            before, old_position, old_end, "before"
        )
        new_position = consume_omitted_newlines(
            after, new_position, new_end, "after"
        )
        if old_position != old_end or new_position != new_end:
            raise InfrastructureError("Git word diff hunk extent does not match source")
        before_previous = old_end
        after_previous = new_end
    if not saw_hunk:
        raise InfrastructureError("Git word diff reported differences without a hunk")
    append_equivalent_region(
        blocks, before, after,
        before_previous, len(before), after_previous, len(after),
    )
    return tuple(blocks)


def normalize_crlf(contents):
    values = bytearray()
    starts = []
    ends = []
    position = 0
    while position < len(contents):
        if contents[position:position + 2] == b"\r\n":
            values.append(ord("\n"))
            starts.append(position)
            ends.append(position + 2)
            position += 2
        else:
            values.append(contents[position])
            starts.append(position)
            ends.append(position + 1)
            position += 1
    return TranslatedSource(bytes(values), tuple(starts), tuple(ends))


def matching_blocks(before, after):
    if before == after:
        return (MatchBlock(0, 0, len(before)),) if before else ()
    before_view = normalize_crlf(before)
    after_view = normalize_crlf(after)
    normalized = normalized_matching_blocks(before_view.values, after_view.values)
    blocks = []
    for block in normalized:
        for offset in range(block.size):
            old_index = block.before_start + offset
            new_index = block.after_start + offset
            old_start = before_view.physical_starts[old_index]
            old_end = before_view.physical_ends[old_index]
            new_start = after_view.physical_starts[new_index]
            new_end = after_view.physical_ends[new_index]
            old_bytes = before[old_start:old_end]
            new_bytes = after[new_start:new_end]
            if old_bytes == new_bytes:
                append_match(blocks, old_start, new_start, len(old_bytes))
    return tuple(blocks)


def changed_after_comments(before_contents, after_contents, before_comments, after_comments):
    before_comments = comment_groups(before_contents, before_comments)
    after_comments = comment_groups(after_contents, after_comments)
    before_by_span = {
        (comment.physical_start, comment.physical_end, comment.kind): comment
        for comment in before_comments
    }
    blocks = matching_blocks(before_contents, after_contents)
    starts = [block.after_start for block in blocks]
    changed = []
    for comment in after_comments:
        block_position = bisect.bisect_right(starts, comment.physical_start) - 1
        unchanged = False
        if block_position >= 0:
            block = blocks[block_position]
            if (block.after_start <= comment.physical_start
                    and comment.physical_end <= block.after_start + block.size):
                old_start = (
                    block.before_start + comment.physical_start - block.after_start
                )
                old_end = old_start + comment.physical_end - comment.physical_start
                unchanged = (old_start, old_end, comment.kind) in before_by_span
        if not unchanged:
            changed.append(comment)
    before_counts = Counter(
        (comment.kind, before_contents[comment.physical_start:comment.physical_end])
        for comment in before_comments
    )
    after_counts = Counter(
        (comment.kind, after_contents[comment.physical_start:comment.physical_end])
        for comment in after_comments
    )
    ambiguous_additions = {
        identity for identity, count in after_counts.items()
        if count > before_counts[identity]
    }
    selected = set(changed)
    for comment in after_comments:
        identity = (
            comment.kind,
            after_contents[comment.physical_start:comment.physical_end],
        )
        if identity in ambiguous_additions and comment not in selected:
            changed.append(comment)
            selected.add(comment)
    return changed


def normalize_comment_body(body):
    undecorated = re.sub(rb"(?m)^[ \t]*\*(?:[ \t]+|$)", b" ", body)
    return re.sub(rb"\s+", b" ", undecorated).strip()


def inspect_comment(path, comment, advisory_threshold):
    normalized = normalize_comment_body(comment.body)
    diagnostics = []
    for rule_id, pattern in RULES:
        match = pattern.search(normalized)
        if match:
            phrase = match.group().decode("ascii", "replace")
            diagnostics.append(Diagnostic(
                rule_id, path, comment.start_line,
                f'explicit development narration "{phrase}"',
            ))
    lines = comment.end_line - comment.start_line + 1
    advisory = None
    if advisory_threshold and lines > advisory_threshold:
        advisory = Advisory(path, comment.start_line, comment.end_line, lines)
    return diagnostics, advisory


def selected_contents(root, mode, change, index):
    if change.old_mode not in {ZERO_MODE, *REGULAR_MODES}:
        raise InfrastructureError(
            f"{display_path(change.old_path)}: unsupported selected mode {change.old_mode}"
        )
    before = (
        read_blob(root, change.old_object)
        if change.old_mode != ZERO_MODE and is_c_path(change.old_path)
        else b""
    )
    if change.new_mode == ZERO_MODE:
        return before, None
    if change.new_mode not in REGULAR_MODES:
        raise InfrastructureError(
            f"{display_path(change.new_path)}: unsupported selected mode {change.new_mode}"
        )
    if mode == "working":
        after = read_working_file(root, change.new_path)
    elif mode == "staged":
        entry = index.get(change.new_path)
        if entry is None:
            raise InfrastructureError(
                f"{display_path(change.new_path)}: missing stage-0 index entry"
            )
        index_mode, object_id = entry
        if index_mode != change.new_mode or object_id != change.new_object:
            raise InfrastructureError(
                f"{display_path(change.new_path)}: index changed during capture"
            )
        after = read_blob(root, object_id)
    else:
        after = read_blob(root, change.new_object)
    return before, after


def check(root, mode, base_revision="HEAD", after_revision=None,
          advisory_threshold=ADVISORY_LINE_THRESHOLD):
    root = Path(root).resolve()
    base = resolve_commit(root, base_revision)
    if mode == "revision":
        if not after_revision:
            raise InfrastructureError("revision mode requires an after revision")
        after_identity = resolve_commit(root, after_revision)
    elif after_revision:
        raise InfrastructureError(f"{mode} mode does not accept an after revision")
    else:
        after_identity = "working-capture" if mode == "working" else "index"
    changes = raw_changes(root, mode, base,
                          after_identity if mode == "revision" else None)
    index = index_entries(root) if mode == "staged" else {}
    result = CheckResult(mode, base, after_identity, changed_paths=len(changes))
    diagnostics = []
    advisories = []
    for change in changes:
        if not is_c_path(change.new_path) and not is_c_path(change.old_path):
            result.non_c_exclusions += 1
            continue
        if change.new_mode == ZERO_MODE or not is_c_path(change.new_path):
            result.deleted_exclusions += 1
            continue
        before_contents, after_contents = selected_contents(root, mode, change, index)
        result.candidate_files += 1
        before_comments = lex_comments(before_contents, change.old_path)
        after_comments = lex_comments(after_contents, change.new_path)
        selected = changed_after_comments(before_contents, after_contents,
                                          before_comments, after_comments)
        result.changed_comments += len(selected)
        for comment in selected:
            comment_diagnostics, advisory = inspect_comment(
                change.new_path, comment, advisory_threshold
            )
            diagnostics.extend(comment_diagnostics)
            if advisory:
                advisories.append(advisory)
    result.diagnostics = tuple(diagnostics)
    result.advisories = tuple(advisories)
    return result


def format_summary(result, verdict):
    return (
        f"{verdict} changed-comments: mode={result.mode} base={result.base} "
        f"after={result.after} changed_paths={result.changed_paths} "
        f"candidate_files={result.candidate_files} "
        f"changed_comments={result.changed_comments} "
        f"violations={len(result.diagnostics)} advisories={len(result.advisories)} "
        f"exclusions=non-c:{result.non_c_exclusions},deleted-or-left-scope:"
        f"{result.deleted_exclusions}"
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--working", action="store_true")
    modes.add_argument("--staged", action="store_true")
    modes.add_argument("--revision", metavar="COMMIT")
    parser.add_argument("--base")
    parser.add_argument("--advisory-lines", type=int, default=ADVISORY_LINE_THRESHOLD)
    arguments = parser.parse_args(argv)
    if arguments.advisory_lines < 0:
        parser.error("--advisory-lines must be zero or greater")
    mode = "working" if arguments.working else "staged" if arguments.staged else "revision"
    if mode == "revision" and not arguments.base:
        parser.error("--revision requires an explicit --base")
    base = arguments.base or "HEAD"
    try:
        result = check(
            arguments.root,
            mode,
            base,
            arguments.revision,
            arguments.advisory_lines,
        )
    except (InfrastructureError, OSError) as error:
        print(f"ERROR changed-comments: {error}", file=sys.stderr)
        return 2
    for advisory in result.advisories:
        print(
            f"ADVISORY CHL001 {display_path(advisory.path)}:"
            f"{advisory.start_line}-{advisory.end_line}: changed comment spans "
            f"{advisory.lines} physical lines "
            f"(threshold {arguments.advisory_lines})"
        )
    for diagnostic in result.diagnostics:
        print(
            f"FAIL {diagnostic.rule_id} {display_path(diagnostic.path)}:"
            f"{diagnostic.line}: {diagnostic.detail}",
            file=sys.stderr,
        )
    if result.diagnostics:
        print(format_summary(result, "FAIL"), file=sys.stderr)
        return 1
    print(format_summary(result, "PASS"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
