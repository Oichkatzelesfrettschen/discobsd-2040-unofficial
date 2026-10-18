"""Capture and compare tiny-device resources without promoting runtime claims."""

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from record_tool import digest
from shipped_closure import execution_receipt, manifest_artifacts

FLASH_START = 0x10000000
FLASH_END = FLASH_START + 131072
PROCESS_WINDOW = 147456
RAM_REGIONS = {
    "kernel": (0x20024000, 0x2003E800),
    "u0area": (0x2003E800, 0x2003F400),
    "uarea": (0x2003F400, 0x20040000),
    "scratch": (0x20040000, 0x20042000),
    "usb_dpram": (0x50100000, 0x50101000),
}
SECTION_ROW = re.compile(
    r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+"
)


class ResourceError(ValueError):
    """A resource measurement or paired comparison violates its contract."""


def require(condition, message):
    if not condition:
        raise ResourceError(message)


def natural(value, label):
    require(type(value) is int and value >= 0, "invalid nonnegative integer: " + label)
    return value


def sha(value, label):
    require(isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value),
            "invalid artifact digest: " + label)


def kernel_metrics(kernel):
    sha(kernel["sha256"], "kernel")
    require(kernel["sections"], "kernel sections are absent")
    flash_end = FLASH_START
    regions = {name: [] for name in RAM_REGIONS}
    names = set()
    flash_ranges = []
    for section in kernel["sections"]:
        name = section["name"]
        require(name not in names, "duplicate kernel section")
        names.add(name)
        size = natural(section["size"], name)
        vma = natural(section["vma"], name)
        lma = natural(section["lma"], name)
        require(type(section["contents"]) is bool, "invalid section contents flag")
        if not size:
            continue
        if FLASH_START <= vma < FLASH_END:
            require(vma + size <= FLASH_END, "kernel flash section crosses its region")
        else:
            owners = [owner for owner, (start, end) in RAM_REGIONS.items()
                      if start <= vma and vma + size <= end]
            require(len(owners) == 1, "kernel section crosses or leaves an owned RAM region")
            regions[owners[0]].append((vma, vma + size, name))
        if section["contents"]:
            require(FLASH_START <= lma and lma + size <= FLASH_END,
                    "kernel flash load extent exceeds its region")
            flash_end = max(flash_end, lma + size)
            flash_ranges.append((lma, lma + size, name))
    for intervals in [flash_ranges, *regions.values()]:
        ordered = sorted(intervals)
        for previous, following in zip(ordered, ordered[1:]):
            require(previous[1] <= following[0], "kernel section overlap")
    require(".boot2" in names and ".text" in names, "kernel executable sections are absent")
    metrics = {"flash_bytes": flash_end - FLASH_START}
    for name, intervals in regions.items():
        start, _ = RAM_REGIONS[name]
        metrics[name + "_allocated_bytes"] = sum(end - begin for begin, end, _ in intervals)
        metrics[name + "_extent_bytes"] = max((end - start for _, end, _ in intervals), default=0)
    return metrics


def packed_file_blocks(length):
    """Count 1 KiB data blocks plus the 2.11BSD inode's indirect blocks."""
    data_blocks = (length + 1023) // 1024
    remaining = max(0, data_blocks - 4)
    metadata = int(remaining > 0)
    remaining = max(0, remaining - 256)
    if remaining:
        double_data = min(remaining, 256 * 256)
        metadata += 1 + (double_data + 255) // 256
        remaining -= double_data
    if remaining:
        require(remaining <= 256 ** 3, "packed file exceeds inode addressing")
        metadata += 1 + (remaining + 65535) // 65536 + (remaining + 255) // 256
    return data_blocks + metadata


def archive_component(value, label):
    require(isinstance(value, str) and value not in {"", ".", ".."}
            and "/" not in value and "\\" not in value
            and all(ord(character) >= 32 and ord(character) != 127 for character in value),
            "invalid archive component: " + label)
    return value


def archive_basename(identity):
    require(isinstance(identity, str), "invalid archive identity")
    if identity.startswith("external:"):
        basename, separator, archive_hash = identity.removeprefix("external:").rpartition("@")
        require(separator, "external archive identity lacks its retained digest")
        sha(archive_hash, "external archive")
        return archive_component(basename, "external archive basename")
    require(identity.startswith("tree:"), "archive identity lacks an ownership prefix")
    relative = identity.removeprefix("tree:")
    path = Path(relative)
    require(relative and not path.is_absolute() and path.as_posix() == relative
            and all(part not in {".", ".."} for part in path.parts),
            "invalid tree archive path")
    for part in path.parts:
        archive_component(part, "tree archive path")
    return path.name


def validate_archive_members(archives, group):
    require(isinstance(archives, dict), "archive selections are absent: " + group)
    for identity, members in archives.items():
        archive_basename(identity)
        require(isinstance(members, dict) and members,
                "archive has an empty or invalid selected-member set: " + group)
        for member, member_hash in members.items():
            archive_component(member, "selected member")
            sha(member_hash, "archive member:" + member)


def verify_artifact_owners(groups, expected):
    require(isinstance(groups, dict) and set(groups) == set(expected),
            "dependency closure artifact owner denominator differs")
    for name, artifact in expected.items():
        require(isinstance(groups[name], dict) and groups[name].get("artifacts") == [artifact],
                "dependency closure artifact owner differs: " + name)


def collect_archive_selections(groups):
    selections = {}
    for name, group in groups.items():
        archives = group.get("archive_members")
        require(isinstance(archives, dict), "closure archive selections are absent: " + name)
        selected = {}
        for label, members in archives.items():
            require(isinstance(label, str), "invalid closure archive identity")
            if label.startswith("external:"):
                archive_hash = group.get("external_inputs", {}).get(label)
                sha(archive_hash, "external archive:" + label)
                identity = "external:" + Path(label).name + "@" + archive_hash
            else:
                identity = "tree:" + label
            require(identity not in selected, "archive identity collision: " + name)
            require(isinstance(members, dict), "invalid closure archive members: " + name)
            selected[identity] = dict(members)
        validate_archive_members(selected, name)
        selections[name] = selected
    return selections


def artifact_group(prefix, name):
    require(isinstance(name, str) and name.startswith("/")
            and Path(name).as_posix() == name and ".." not in Path(name).parts,
            "invalid manifest archive-selection path")
    return prefix + "." + name.lstrip("/").replace("/", ".")


def validate_archive_selections(document):
    expected = ["PICO", "PICO_UART"]
    expected.extend(artifact_group("root", name) for name in document["manifest"]["packs"])
    expected.extend(artifact_group("native", name)
                    for name in document["manifest"]["native_files"])
    selections = document.get("archive_selections")
    require(isinstance(selections, dict) and len(set(expected)) == len(expected)
            and set(selections) == set(expected), "archive selection denominator differs")
    for name, archives in selections.items():
        validate_archive_members(archives, name)
    for name, program in document["programs"].items():
        archives = selections[artifact_group("root", name)]
        projected = sorted({member for archive, members in archives.items()
                            if archive_basename(archive) == "libc.a" for member in members})
        require(program["libc_members"] == projected, "libc archive projection differs: " + name)
    for name, artifact in document["native_artifacts"].items():
        archives = selections[artifact_group("native", name)]
        projected = sorted({member for members in archives.values() for member in members})
        require(artifact["archive_members"] == projected,
                "native archive projection differs: " + name)


def validate(document):
    require(type(document["schema_version"]) is int and document["schema_version"] in {1, 2},
            "unsupported resource schema")
    if document["schema_version"] == 2:
        sha(document["collector_sha256"], "collector")
        sha(document["execution"]["closure_sha256"], "closure")
    require(re.fullmatch(r"[0-9a-f]{40}", document["source"]["commit"]),
            "source commit identity is absent")
    for name in ["cc", "ld", "bmake"]:
        require(document["toolchain"][name], "toolchain identity is absent: " + name)
    for name in ["hsaout", "fsutil", "objdump"]:
        sha(document["measurement_tools"][name], "measurement tool:" + name)
    execution = document["execution"]
    require(execution["state"] == "completed" and execution["exit_code"] == 0,
            "distribution execution failed or remains incomplete")
    for name in ["receipt_sha256", "log_sha256"]:
        sha(execution[name], name)
    manifest = document["manifest"]
    sha(manifest["sha256"], "manifest")
    packs = manifest["packs"]
    require(packs and len(packs) == len(set(packs)), "invalid manifest executable denominator")
    require(set(packs) == set(document["programs"]), "manifest executable measurements differ")
    for target in manifest["links"].values():
        require(target.startswith("/"), "invalid manifest hard-link destination")
    require(set(document["kernels"]) == {"PICO", "PICO_UART"}, "kernel configurations differ")
    metrics = {"kernel:" + configuration + ":" + name: value
               for configuration, kernel in document["kernels"].items()
               for name, value in kernel_metrics(kernel).items()}
    for name, program in document["programs"].items():
        sha(program["sha256"], name)
        for field in ["text", "data", "bss", "raw_bytes", "packed_bytes", "packed_blocks"]:
            natural(program[field], name + ":" + field)
        loaded = program["text"] + program["data"] + program["bss"]
        require(0 < loaded < PROCESS_WINDOW, "program exhausts the base process window: " + name)
        require(program["raw_bytes"] >= 32 + program["text"] + program["data"],
                "a.out file is shorter than its declared segments")
        require(program["packed_blocks"] == packed_file_blocks(program["packed_bytes"]),
                "packed block arithmetic differs")
        members = program["libc_members"]
        require(isinstance(members, list) and members == sorted(set(members)),
                "archive member accounting differs")
        for field in ["raw_bytes", "packed_bytes", "packed_blocks"]:
            metrics["program:" + name + ":" + field] = program[field]
        metrics["program:" + name + ":loaded_bytes"] = loaded
    native_files = manifest["native_files"]
    require(native_files == sorted(set(native_files)), "native manifest denominator differs")
    require(set(native_files) == set(document["native_artifacts"]),
            "native artifact measurements differ from manifest")
    for name, artifact in document["native_artifacts"].items():
        sha(artifact["sha256"], name)
        natural(artifact["bytes"], name)
        members = artifact["archive_members"]
        require(isinstance(members, list) and members == sorted(set(members)),
                "native archive membership differs")
        metrics["native:" + name + ":bytes"] = artifact["bytes"]
    root = document["root"]
    sha(root["sha256"], "root image")
    for name in ["partition_blocks", "inode_list_blocks", "data_blocks", "free_blocks",
                 "free_inodes"]:
        natural(root[name], "root:" + name)
    require(root["partition_blocks"] == 988 and root["inode_list_blocks"] == 9,
            "root partition geometry differs")
    require(root["data_blocks"] + root["free_blocks"] + root["inode_list_blocks"] == 988,
            "root block arithmetic differs")
    require(root["free_blocks"] > 0 and root["free_inodes"] > 0,
            "root image exhausts free capacity")
    metrics["root:data_blocks"] = root["data_blocks"]
    metrics["root:used_inodes"] = -root["free_inodes"]
    for name in ["stack", "heap", "storage_traffic"]:
        require(document["runtime"][name] == {"status": "unmeasured"},
                "runtime measurement requires a separate calibrated receipt: " + name)
    if document["schema_version"] == 2:
        validate_archive_selections(document)
    return metrics


def compare(base, candidate):
    baseline = validate(base)
    measured = validate(candidate)
    require(base["schema_version"] == candidate["schema_version"] == 2,
            "complete archive comparison requires schema 2 receipts")
    require(base["collector_sha256"] == candidate["collector_sha256"],
            "paired collector identities differ")
    require(base["toolchain"] == candidate["toolchain"], "paired toolchain identities differ")
    require(base["measurement_tools"] == candidate["measurement_tools"],
            "paired measurement tool identities differ")
    require(set(baseline) == set(measured), "paired resource denominators differ")
    deltas = {name: measured[name] - baseline[name] for name in sorted(baseline)}
    changes = {}
    for name, program in candidate["programs"].items():
        old = set(base["programs"][name]["libc_members"])
        new = set(program["libc_members"])
        changes[name] = {"added": sorted(new - old), "removed": sorted(old - new)}
    for name, artifact in candidate["native_artifacts"].items():
        old = set(base["native_artifacts"][name]["archive_members"])
        new = set(artifact["archive_members"])
        changes["native:" + name] = {"added": sorted(new - old), "removed": sorted(old - new)}
    selections = {}
    for name, archives in candidate["archive_selections"].items():
        previous = base["archive_selections"][name]
        selections[name] = {}
        for archive in sorted(set(previous) | set(archives)):
            old = previous.get(archive, {})
            new = archives.get(archive, {})
            selections[name][archive] = {
                "added": sorted(set(new) - set(old)),
                "removed": sorted(set(old) - set(new)),
                "changed": sorted(member for member in set(old) & set(new)
                                  if old[member] != new[member])}
    return {"schema_version": 2, "deltas": deltas,
            "positive_deltas": [name for name, delta in deltas.items() if delta > 0],
            "archive_changes": changes, "archive_selection_changes": selections,
            "runtime_acceptance": "open"}


def output_of(command, cwd):
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True,
                          check=True).stdout


def section_inventory(path, root):
    lines = output_of(["arm-none-eabi-objdump", "-h", str(path)], root).splitlines()
    sections = []
    for index, line in enumerate(lines):
        match = SECTION_ROW.match(line)
        if match and index + 1 < len(lines) and "ALLOC" in lines[index + 1]:
            name, size, vma, lma = match.groups()
            sections.append({"name": name, "size": int(size, 16), "vma": int(vma, 16),
                             "lma": int(lma, 16), "contents": "CONTENTS" in lines[index + 1]})
    require(sections, "kernel section inventory is empty")
    return {"sha256": digest(path), "sections": sections}


def bound_image(path, provenance):
    observed = digest(path)
    require(observed == provenance["image_sha256"],
            "root image differs from dependency closure provenance")
    return observed


def capture(root, closure_path, receipt_path):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from analysis.aout_reachability import DEFAULT_TEXT_BASE, parse_aout
    from check_multicall_composition import measure_hsaout, measure_root_image, parse_manifest

    root = root.resolve()
    bound_execution = execution_receipt(root, receipt_path, ["distribution"])
    receipt = json.loads(receipt_path.read_text())
    require(receipt["state"] == "completed" and receipt["exit_code"] == 0,
            "distribution execution failed or remains incomplete")
    require(receipt["terminal_queue_dropped_bytes"] == 0, "distribution log lost output bytes")
    status_path = (root / receipt["status_path"]).resolve()
    status_path.relative_to(root)
    log_path = (root / receipt["log_path"]).resolve()
    log_path.relative_to(root)
    require(json.loads(status_path.read_text()) == receipt,
            "immutable distribution receipt differs")
    closure = json.loads(closure_path.read_text())
    groups = closure["configurations"]
    provenance = closure["provenance"]
    require(provenance["distribution"] == bound_execution,
            "distribution receipt differs from closure provenance")
    manifest_path = root / "distrib/rp2040/_manifest"
    require(digest(manifest_path) == provenance["manifest_sha256"],
            "manifest differs from closure provenance")
    expected_paths = manifest_artifacts(root, manifest_path)
    expected_paths.update({configuration: f"sys/arch/rp2040/compile/{configuration}/unix"
                           for configuration in ("PICO", "PICO_UART")})
    verify_artifact_owners(groups, {name: {"path": relative, "sha256": digest(root / relative)}
                                   for name, relative in expected_paths.items()})
    packs, links = parse_manifest(manifest_path)
    destination = root / "distrib/obj/destdir.rp2040"
    programs = {}
    size_records = [{"path": (destination / name.lstrip("/")).relative_to(root).as_posix()}
                    for name in packs]
    measurements = measure_hsaout(root, root / "tools/bin/rp2040/hsaout", size_records)
    for name, entry, measurement in zip(packs, size_records, measurements):
        path = root / entry["path"]
        parsed = parse_aout(path, DEFAULT_TEXT_BASE)
        program_hash = digest(path)
        owner = groups[artifact_group("root", name)]
        require(owner["artifacts"] == [{"path": entry["path"], "sha256": program_hash}],
                "program dependency closure artifact owner differs: " + name)
        libc_members = sorted({member for archive, members in owner["archive_members"].items()
                               if Path(archive).name == "libc.a" for member in members})
        raw, packed, _, blocks = measurement
        programs[name] = {"sha256": program_hash, "text": parsed["text_size"],
                          "data": parsed["data_size"], "bss": parsed["bss_size"],
                          "raw_bytes": raw, "packed_bytes": packed, "packed_blocks": blocks,
                          "libc_members": libc_members}
    kernels = {}
    for configuration in ["PICO", "PICO_UART"]:
        relative = f"sys/arch/rp2040/compile/{configuration}/unix"
        path = root / relative
        kernel = section_inventory(path, root)
        require(groups[configuration]["artifacts"] == [
                    {"path": relative, "sha256": kernel["sha256"]}],
                "kernel dependency closure artifact owner differs: " + configuration)
        kernels[configuration] = kernel
    native = {}
    for name, relative in manifest_artifacts(root, manifest_path).items():
        if not name.startswith("native."):
            continue
        path = root / relative
        artifact_hash = digest(path)
        owner = groups[name]
        require(owner["artifacts"] == [{"path": relative, "sha256": artifact_hash}],
                "native dependency closure artifact owner differs: " + name)
        members = sorted({member for archive in owner["archive_members"].values()
                          for member in archive})
        native["/" + path.relative_to(destination).as_posix()] = {
            "sha256": artifact_hash, "bytes": path.stat().st_size, "archive_members": members}
    image_path = root / "distrib/rp2040/sdcard.img"
    root_measurements = measure_root_image(root / "tools/bin/rp2040/fsutil", image_path)
    root_measurements["sha256"] = bound_image(image_path, provenance)
    document = {
        "schema_version": 2,
        "archive_selections": collect_archive_selections(groups),
        "source": {"commit": provenance["source_commit"],
                   "tracked_files_sha256": provenance["tracked_files_sha256"]},
        "toolchain": {
            "cc": output_of(["arm-none-eabi-gcc", "--version"], root).splitlines()[0],
            "ld": output_of(["arm-none-eabi-ld", "--version"], root).splitlines()[0],
            "bmake": output_of(["bmake", "-V", "MAKE_VERSION"], root).strip()},
        "measurement_tools": {
            "hsaout": digest(root / "tools/bin/rp2040/hsaout"),
            "fsutil": digest(root / "tools/bin/rp2040/fsutil"),
            "objdump": digest(Path(shutil.which("arm-none-eabi-objdump")))},
        "collector_sha256": digest(Path(__file__)),
        "manifest": {"sha256": digest(manifest_path), "packs": list(packs), "links": links,
                     "native_files": sorted(native)},
        "native_artifacts": native,
        "execution": {"state": receipt["state"], "exit_code": receipt["exit_code"],
                      "receipt_sha256": digest(status_path), "log_sha256": digest(log_path),
                      "closure_sha256": digest(closure_path)},
        "kernels": kernels, "programs": programs, "root": root_measurements,
        "runtime": {name: {"status": "unmeasured"}
                    for name in ["stack", "heap", "storage_traffic"]}}
    validate(document)
    return document


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="operation", required=True)
    for operation in ["validate", "compare"]:
        child = subparsers.add_parser(operation)
        child.add_argument("--candidate", type=Path, required=True)
        if operation == "compare":
            child.add_argument("--base", type=Path, required=True)
            child.add_argument("--output", type=Path, required=True)
    child = subparsers.add_parser("capture")
    child.add_argument("--root", type=Path, default=Path.cwd())
    child.add_argument("--closure", type=Path, required=True)
    child.add_argument("--distribution-receipt", type=Path, required=True)
    child.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.operation == "capture":
            result = capture(args.root, args.closure, args.distribution_receipt)
            args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
        elif args.operation == "validate":
            candidate = json.loads(args.candidate.read_text())
            validate(candidate)
            if candidate["schema_version"] == 1:
                print("PASS historical schema-1 static measurements; complete archive "
                      "comparison requires schema 2; runtime remains unmeasured")
                return 0
        else:
            result = compare(json.loads(args.base.read_text()),
                             json.loads(args.candidate.read_text()))
            args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
            require(not any(change["added"]
                            for group in result["archive_selection_changes"].values()
                            for change in group.values()),
                    "added archive dependencies require measured review")
            require(not result["positive_deltas"],
                    "positive constrained-surface deltas require reviewed exceptions: " +
                    ", ".join(result["positive_deltas"]))
        print("PASS static resource accounting; runtime remains unmeasured")
        return 0
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"FAIL shipped resources: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
