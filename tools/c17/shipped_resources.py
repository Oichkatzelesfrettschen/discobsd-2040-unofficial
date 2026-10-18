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


def validate(document):
    require(document["schema_version"] == 1, "unsupported resource schema")
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
    return metrics


def compare(base, candidate):
    baseline = validate(base)
    measured = validate(candidate)
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
    return {"schema_version": 1, "deltas": deltas,
            "positive_deltas": [name for name, delta in deltas.items() if delta > 0],
            "archive_changes": changes, "runtime_acceptance": "open"}


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
        owners = [group for group in groups.values()
                  if any(artifact["path"] == entry["path"] and
                         artifact["sha256"] == program_hash for artifact in group["artifacts"])]
        require(len(owners) == 1, "program lacks one current dependency-closure owner: " + name)
        libc_members = sorted({member for archive, members in owners[0]["archive_members"].items()
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
        require(any(artifact["path"] == relative and artifact["sha256"] == kernel["sha256"]
                    for group in groups.values() for artifact in group["artifacts"]),
                "kernel lacks current dependency-closure evidence: " + configuration)
        kernels[configuration] = kernel
    native = {}
    for name, relative in manifest_artifacts(root, manifest_path).items():
        if not name.startswith("native."):
            continue
        path = root / relative
        artifact_hash = digest(path)
        owners = [group for group in groups.values()
                  if any(artifact["path"] == relative and artifact["sha256"] == artifact_hash
                         for artifact in group["artifacts"])]
        require(len(owners) == 1, "native artifact lacks current dependency closure")
        members = sorted({member for archive in owners[0]["archive_members"].values()
                          for member in archive})
        native["/" + path.relative_to(destination).as_posix()] = {
            "sha256": artifact_hash, "bytes": path.stat().st_size, "archive_members": members}
    image_path = root / "distrib/rp2040/sdcard.img"
    root_measurements = measure_root_image(root / "tools/bin/rp2040/fsutil", image_path)
    root_measurements["sha256"] = bound_image(image_path, provenance)
    document = {
        "schema_version": 1,
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
            validate(json.loads(args.candidate.read_text()))
        else:
            result = compare(json.loads(args.base.read_text()),
                             json.loads(args.candidate.read_text()))
            args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
            require(not any(change["added"] for change in result["archive_changes"].values()),
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
