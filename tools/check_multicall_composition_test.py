"""Mutation controls for the DPUCP-2024 multicall composition ledger."""

import contextlib
import copy
import io
import os
import tempfile
from argparse import Namespace
from pathlib import Path
from unittest import mock

import check_multicall_composition as composition


def mutation_is_rejected(root, document):
    try:
        composition.validate_document(root, document)
    except composition.CompositionError:
        return True
    return False


def makefile_contract_failures(root):
    makefile_text = (root / "Makefile").read_text(encoding="utf-8")
    required_composition_order = (
        "CROSS_COMPOSITION_GATES=\tcheck-libc-array-memory-line-cross \\\n"
        "\t\t.WAIT check-timeout-target-link .WAIT \\\n"
        "\t\tcheck-multicall-composition"
    )
    failures = []
    if required_composition_order not in makefile_text:
        failures.append("parallel composition gates")
    image_prerequisite = (
        "check-multicall-composition: check-python .WAIT symlinks "
        ".WAIT ${FSIMG}"
    )
    if image_prerequisite not in makefile_text:
        failures.append("missing root image prerequisite")
    if "${MAKE} -C ${TOPSRC}/bin/sh MACHINE=${MACHINE} clean all" in makefile_text:
        failures.append("parallel shell clean and all")
    box_clean_then_link = (
        '\t\t\trm -f "$$map_root/$$program.map"; \\\n'
        '\t\t\t${MAKE} -C "${TOPSRC}/$$directory" MACHINE=${MACHINE} clean; \\\n'
        '\t\t\t${MAKE} -C "${TOPSRC}/$$directory" MACHINE=${MACHINE} \\\n'
        "\t\t\t    LINK_MAP=\"$$map_root/$$program.map\" all;"
    )
    if box_clean_then_link not in makefile_text:
        failures.append("stale multicall link maps")
    if '--root ${TOPSRC} --list-boxes);' not in makefile_text:
        failures.append("hardcoded multicall relink denominator")
    if 'rm -f "${TOPSRC}/.build-logs/multicall-link-maps/sh.map"' not in makefile_text:
        failures.append("stale shell link map")
    if "\t\t    --root-image ${FSIMG}\n" not in makefile_text:
        failures.append("live root image not passed to the checker")
    if ('test -s "$$map_root/$$program.map"; \\\n'
            '\t\t\ttouch "$$map_root/$$program.map";' not in makefile_text):
        failures.append("missing map publication after a.out conversion")
    return failures


def makefile_contract_calibration(root):
    text = (root / "Makefile").read_text(encoding="utf-8")
    mutations = (
        (".WAIT check-timeout-target-link .WAIT", "check-timeout-target-link",
         "parallel composition gates"),
        (".WAIT ${FSIMG}", "", "missing root image prerequisite"),
        ("${MAKE} -C ${TOPSRC}/bin/sh MACHINE=${MACHINE} clean",
         "${MAKE} -C ${TOPSRC}/bin/sh MACHINE=${MACHINE} clean all",
         "parallel shell clean and all"),
        ('rm -f "$$map_root/$$program.map";', "", "stale multicall link maps"),
        ("--list-boxes);", ");", "hardcoded multicall relink denominator"),
        ('rm -f "${TOPSRC}/.build-logs/multicall-link-maps/sh.map"', "",
         "stale shell link map"),
        ('test -s "$$map_root/$$program.map";', "",
         "missing map publication after a.out conversion"),
        (" \\\n\t\t    --root-image ${FSIMG}", "",
         "live root image not passed to the checker"),
    )
    with tempfile.TemporaryDirectory() as temporary_name:
        fixture = Path(temporary_name)
        for original, replacement, expected in mutations:
            (fixture / "Makefile").write_text(text.replace(original, replacement))
            if expected not in makefile_contract_failures(fixture):
                raise AssertionError(f"Makefile control accepted {expected}")
    print(f"PASS Makefile composition contracts: {len(mutations)} mutations rejected")


def list_boxes_calibration():
    """Read real bmake variables and list boxes without retained evidence."""
    with tempfile.TemporaryDirectory() as temporary_name:
        fixture = Path(temporary_name)
        overlay = fixture / "share/mk/multicall-bss-overlay.mk"
        overlay.parent.mkdir(parents=True)
        overlay.touch()
        makefile = fixture / "sbin/container/Makefile"
        makefile.parent.mkdir(parents=True)
        makefile.write_text(f'TOPSRC= {fixture}\nPROG= listedbox\nBINDIR= /opt\n'
                            '.include "${TOPSRC}/share/mk/multicall-bss-overlay.mk"\n')
        manifest = fixture / "distrib/rp2040/mi.rp2040"
        manifest.parent.mkdir(parents=True)
        manifest.write_text("pack /opt/listedbox\nlink /opt/applet\ntarget /opt/listedbox\n")
        arguments = Namespace(root=fixture, ledger=None, list_boxes=True)
        output = io.StringIO()
        with mock.patch.object(composition, "parse_arguments", return_value=arguments):
            with contextlib.redirect_stdout(output):
                status = composition.main()
            if status != 0 or output.getvalue() != "sbin/container\n":
                raise AssertionError(f"live directory listing differs: {output.getvalue()!r}")
            manifest.write_text(manifest.read_text().replace("/opt/listedbox", "/opt/wrongbox"))
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                status = composition.main()
            if status != 1 or "installed paths" not in output.getvalue():
                raise AssertionError("live directory listing accepted a mismatched manifest")
    print("PASS live directory listing uses real bmake identities and rejects manifest drift")


@contextlib.contextmanager
def composition_fixture(root, document, drift=False):
    """Exercise document orchestration with deterministic tool outputs."""
    with tempfile.TemporaryDirectory() as temporary_name, contextlib.ExitStack() as stack:
        fixture_root = Path(temporary_name)
        manifest_path = "distrib/rp2040/mi.rp2040"
        retained_manifest = composition.retained_source_bytes(root, document, manifest_path)
        paths = [manifest_path, "bin/sh/Makefile"]
        paths.extend(box["makefile"] for box in document["boxes"])
        for relative_path in paths:
            destination = fixture_root / relative_path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(
                composition.retained_source_bytes(root, document, relative_path)
            )
        evidence_paths = [document["measurement_inventory"]["path"],
                          document["archive_summary"]["inventory_path"]]
        evidence_paths.extend(
            receipt["receipt_path"] for receipt in document["clean_distribution_receipts"].values()
        )
        for relative_path in evidence_paths:
            destination = fixture_root / relative_path
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes((root / relative_path).read_bytes())
        records = composition.parse_size_inventory(root / document["measurement_inventory"]["path"])
        measurements = {
            (row["kind"], row["box"], row["program"]): tuple(
                row[field] for field in ("raw_bytes", "packed_bytes", "raw_blocks", "packed_blocks")
            ) for row in records
        }
        for row in records:
            binary = fixture_root / row["path"]
            binary.parent.mkdir(parents=True, exist_ok=True)
            binary.touch()
            os.utime(binary, ns=(1_000_000_000, 1_000_000_000))
            if row["kind"] == "standalone":
                (binary.parent / "Makefile").touch()
        variables = {
            box["id"]: {
                "PROG": [box["id"]], "TOOLS": list(box["tools"]),
                "BUILTIN_TOOLS": box.get("builtin_tools", []), "ALIASES": box.get("aliases", []),
                "BINDIR": [str(Path(box["installed_path"]).parent)],
                "UNSHIPPED_TOOLS": [],
            } for box in document["boxes"]
        }
        receipt = composition.load_json(
            root / document["clean_distribution_receipts"]["candidate"]["receipt_path"]
        )
        root_measurement = dict(receipt["root_image"])
        changed_sources = []
        if drift:
            changed_sources = ["Makefile", manifest_path, "games/gamebox/Makefile"]
            variables["gamebox"]["TOOLS"].append("fixturetool")
            (fixture_root / "games/fixturetool").mkdir()
            (fixture_root / "games/fixturetool/Makefile").touch()
            manifest = fixture_root / manifest_path
            manifest.write_text(manifest.read_text() +
                                "\nlink /usr/games/fixturetool\n"
                                "target /usr/games/gamebox\n", encoding="utf-8")
            measurements[("standalone", "gamebox", "fixturetool")] = (2000, 1500, 3, 2)
            measurements[("multicall", "gamebox", "gamebox")] = (14000, 12000, 15, 13)
            root_measurement["free_blocks"] -= 1
            root_measurement["data_blocks"] += 1
        map_root = fixture_root / "maps"
        map_root.mkdir()
        inventory = composition.parse_inventory(
            root / document["archive_summary"]["inventory_path"]
        )
        for consumer in [*document["boxes"], *document["standalone_programs"]]:
            members = [member for member, consumers in inventory.items()
                       if consumer["id"] in consumers]
            if drift:
                members.append("libc.a(fixture.o)")
            (map_root / consumer["link_map"]).write_text("\n".join(members), encoding="utf-8")
            os.utime(map_root / consumer["link_map"], ns=(2_000_000_000, 2_000_000_000))
        (fixture_root / "bin/sh/sh").touch()
        os.utime(fixture_root / "bin/sh/sh", ns=(1_000_000_000, 1_000_000_000))
        stack.enter_context(mock.patch.object(
            composition, "validate_retained_sources", return_value=changed_sources
        ))
        stack.enter_context(mock.patch.object(
            composition, "retained_source_bytes", return_value=retained_manifest
        ))
        stack.enter_context(mock.patch.object(
            composition, "read_make_variable",
            side_effect=lambda root, box, variable: variables[box["id"]][variable],
        ))
        summary = document["measurement_inventory"]
        stack.enter_context(mock.patch.object(
            composition, "measurement_environment_matches", return_value=(
                True, summary["arm_cc_version"], summary["arm_ld_version"], summary["bmake_version"]
            ),
        ))
        build = stack.enter_context(mock.patch.object(composition, "build_live_standalones"))
        stack.enter_context(mock.patch.object(
            composition, "measure_hsaout", side_effect=lambda root, tool, rows: [
                measurements[(row["kind"], row["box"], row["program"])] for row in rows
            ],
        ))
        stack.enter_context(mock.patch.object(
            composition, "measure_root_image", return_value=root_measurement
        ))
        yield fixture_root, map_root, measurements, root_measurement, build, variables


def require_error(expected, operation):
    try:
        operation()
    except composition.CompositionError as error:
        if expected not in str(error):
            raise AssertionError(f"expected {expected!r}, got {str(error)!r}") from error
    else:
        raise AssertionError(f"accepted mutation requiring {expected!r}")


def live_mode_calibration(root, document):
    case_names = []
    with composition_fixture(root, document) as (fixture, maps, measurements, _, _, _):
        composition.validate_document(fixture, document, maps)
        identity = ("multicall", "box", "box")
        raw, packed, raw_blocks, packed_blocks = measurements[identity]
        measurements[identity] = (raw + 1, packed, raw_blocks, packed_blocks)
        require_error("box live hsaout measurement: expected", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("matching_sources_and_toolchain_reject_byte_drift")

    with composition_fixture(root, document) as (fixture, maps, _, _, _, _):
        board_image = fixture / "distrib/rp2040/sdcard-FIXTURE.img"
        board_image.write_bytes(b"")
        composition.validate_document(fixture, document, maps, board_image)
        measured_path = composition.measure_root_image.call_args.args[1]
        if measured_path != board_image:
            raise AssertionError(
                f"live root measurement read {measured_path}, not the board image"
            )
        composition.validate_document(fixture, document, maps)
        default_path = composition.measure_root_image.call_args.args[1]
        if default_path != fixture / "distrib/rp2040/sdcard.img":
            raise AssertionError("default live root measurement lost the receipt path")
        require_error("live root image", lambda: composition.validate_document(
            fixture, document, maps, fixture / "distrib/rp2040/absent.img"))
        case_names.append("live_mode_measures_the_named_board_image")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, build, _):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            composition.validate_document(fixture, document, maps)
        expected_line = ("live mode; changed retained sources: Makefile, "
                         "distrib/rp2040/mi.rp2040, games/gamebox/Makefile")
        if expected_line not in output.getvalue():
            raise AssertionError("source-drift live-mode diagnostic lacks changed paths")
        if "live libc member delta gamebox" not in output.getvalue():
            raise AssertionError("source-drift live mode omits archive deltas")
        if ("live denominator: 7 boxes, 81 shipped names, 286 free blocks; "
                "retained ledger: 7 boxes, 80 shipped names, 287 free blocks"
                not in output.getvalue()):
            raise AssertionError("live-mode pass line omits live and retained denominators")
        built_records = build.call_args.args[1]
        if not any(row["path"] == "games/fixturetool/fixturetool" for row in built_records):
            raise AssertionError("live measurement omits added standalone tool")
        case_names.append("drifted_sources_accept_live_composition")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, _):
        manifest = fixture / "distrib/rp2040/mi.rp2040"
        manifest.write_text(manifest.read_text() +
                            "\nlink /usr/games/unknown\ntarget /usr/games/gamebox\n",
                            encoding="utf-8")
        require_error("gamebox ships names outside dispatch: unknown", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_unknown_manifest_dispatch")

    with composition_fixture(root, document, drift=True) as (fixture, maps, measurements, _, _, _):
        identity = ("multicall", "gamebox", "gamebox")
        standalone_sum = sum(values[3] for key, values in measurements.items()
                             if key[:2] == ("standalone", "gamebox"))
        measurements[identity] = (*measurements[identity][:3], standalone_sum)
        require_error(f"gamebox live composition saves no packed blocks: "
                      f"{standalone_sum} against {standalone_sum}", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_equal_packed_block_cost")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, _):
        makefile = fixture / "games/gamebox/Makefile"
        makefile.write_text(makefile.read_text().replace("${LINK_MAP_FLAG}", ""), encoding="utf-8")
        require_error("gamebox link does not consume LINK_MAP_FLAG", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_missing_link_map_flag")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, live_root, _, _):
        live_root["data_blocks"] += live_root["free_blocks"]
        live_root["free_blocks"] = 0
        require_error("candidate live root has no free capacity", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_zero_free_root_blocks")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, _):
        manifest = fixture / "distrib/rp2040/mi.rp2040"
        manifest.write_text(manifest.read_text() +
                            "\npack /usr/games/unlistedbox\n"
                            "link /usr/games/unlistedtool\ntarget /usr/games/unlistedbox\n")
        require_error("/usr/games/unlistedbox", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_unlisted_packed_box_with_link")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, variables):
        makefile = fixture / "sbin/fixturebox/Makefile"
        makefile.parent.mkdir()
        makefile.write_text("include ${TOPSRC}/share/mk/multicall-bss-overlay.mk\n")
        variables["fixturebox"] = {
            "PROG": ["fixturebox"], "BINDIR": ["/sbin"], "TOOLS": [],
            "BUILTIN_TOOLS": [], "ALIASES": [], "UNSHIPPED_TOOLS": [],
        }
        require_error("fixturebox", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_box_without_manifest_entry")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, _):
        manifest = fixture / "distrib/rp2040/mi.rp2040"
        manifest.write_text(manifest.read_text().replace(
            "link /usr/games/bubble\ntarget /usr/games/gamebox\n", ""))
        require_error("gamebox undeclared unshipped dispatch names", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_dropped_link_kept_in_tools")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, variables):
        variables["gamebox"]["PROG"] = ["different_gamebox"]
        # The variable reader keys by the Makefile directory, independently of PROG.
        variables["different_gamebox"] = variables["gamebox"]
        require_error("installed paths", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_prog_installed_basename_mismatch")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, _):
        makefile = fixture / "games/gamebox/Makefile"
        makefile.write_text(makefile.read_text().replace("${LINK_MAP_FLAG}", "") +
                            "\n# ${LINK_MAP_FLAG}\n")
        os.utime(maps / "gamebox.map", ns=(0, 0))
        require_error("gamebox link does not consume LINK_MAP_FLAG", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_comment_only_flag_with_stale_map")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, _):
        os.utime(maps / "gamebox.map", ns=(0, 0))
        require_error("gamebox stale link map", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_stale_map_with_valid_recipe")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, variables):
        variables["gamebox"]["BUILTIN_TOOLS"] = ["cat"]
        variables["gamebox"]["UNSHIPPED_TOOLS"] = ["cat"]
        require_error("overlapping dispatch names", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_overlapping_dispatch_names")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, _):
        (fixture / "usr.sbin/date2").mkdir(parents=True)
        require_error("date standalone directory usr.sbin/date2 lacks a Makefile", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_rejects_program2_decoy_without_makefile")

    with composition_fixture(root, document, drift=True) as (fixture, maps, measurements,
                                                           _, _, variables):
        makefile = fixture / "sbin/fixturebox/Makefile"
        makefile.parent.mkdir()
        makefile.write_text("include ${TOPSRC}/share/mk/multicall-bss-overlay.mk\n"
                            "all:\n\t${CC} ${LINK_MAP_FLAG} -o fixturebox\n")
        variables["fixturebox"] = {
            "PROG": ["fixturebox"], "BINDIR": ["/sbin"], "TOOLS": ["fixtureapplet"],
            "BUILTIN_TOOLS": [], "ALIASES": [], "UNSHIPPED_TOOLS": [],
        }
        manifest = fixture / "distrib/rp2040/mi.rp2040"
        manifest.write_text(manifest.read_text() +
                            "\npack /sbin/fixturebox\nlink /sbin/fixtureapplet\n"
                            "target /sbin/fixturebox\n")
        standalone = fixture / "sbin/fixtureapplet"
        standalone.mkdir()
        (standalone / "Makefile").touch()
        (makefile.parent / "fixturebox").touch()
        os.utime(makefile.parent / "fixturebox", ns=(1_000_000_000, 1_000_000_000))
        measurements[("multicall", "fixturebox", "fixturebox")] = (1000, 900, 2, 1)
        measurements[("standalone", "fixturebox", "fixtureapplet")] = (2000, 1900, 3, 2)
        map_path = maps / "fixturebox.map"
        map_path.write_text("libc.a(fixturebox.o)\n")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            composition.validate_document(fixture, document, maps)
        if "live denominator: 8 boxes, 82 shipped names, 286 free blocks" not in output.getvalue():
            raise AssertionError("live denominator omits the discovered box")
        if "live libc member delta fixturebox" not in output.getvalue():
            raise AssertionError("archive consumers omit the discovered box")
        map_path.write_text("")
        require_error("fixturebox.map names no libc archive members", lambda:
                      composition.validate_document(fixture, document, maps))
        case_names.append("live_mode_measures_added_box_and_rejects_its_empty_map")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, variables):
        variables["gamebox"]["BINDIR"] = ["/opt/games"]
        require_error("installed paths", lambda:
                      composition.validate_document(fixture, document, maps))
        manifest = fixture / "distrib/rp2040/mi.rp2040"
        manifest.write_text(manifest.read_text().replace(
            "/usr/games/gamebox", "/opt/games/gamebox"))
        with contextlib.redirect_stdout(io.StringIO()):
            composition.validate_document(fixture, document, maps)
        case_names.append("live_mode_binds_installed_path_to_bindir")

    with composition_fixture(root, document, drift=True) as (fixture, maps, _, _, _, variables):
        manifest = fixture / "distrib/rp2040/mi.rp2040"
        manifest.write_text(manifest.read_text().replace(
            "link /usr/games/bubble\ntarget /usr/games/gamebox\n", ""))
        variables["gamebox"]["UNSHIPPED_TOOLS"] = ["bubble"]
        with contextlib.redirect_stdout(io.StringIO()):
            composition.validate_document(fixture, document, maps)
        case_names.append("live_mode_accepts_explicit_unshipped_declaration")
    for name in case_names:
        print(f"PASS {name}")


def main():
    root = Path(__file__).resolve().parents[1]
    document = composition.load_json(
        root / "docs/research/dpucp-2024-multicall-composition.json"
    )
    composition.validate_document(root, document)

    size_records = composition.parse_size_inventory(
        root / document["measurement_inventory"]["path"]
    )
    live_measurements = [
        (
            record["raw_bytes"],
            record["packed_bytes"],
            record["raw_blocks"],
            record["packed_blocks"],
        )
        for record in size_records
    ]
    live_measurements[0] = (34016, 28170, 35, 29)
    composition.validate_live_measurements(
        size_records, document["boxes"], live_measurements, exact=False
    )
    try:
        composition.validate_live_measurements(
            size_records, document["boxes"], live_measurements, exact=True
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("exact measurement accepted cross-toolchain byte drift")
    no_savings = list(live_measurements)
    first_box_standalone = sum(
        measurement[3]
        for record, measurement in zip(size_records, live_measurements)
        if record["kind"] == "standalone"
        and record["box"] == document["boxes"][0]["id"]
    )
    no_savings[0] = (*no_savings[0][:3], first_box_standalone)
    try:
        composition.validate_live_measurements(
            size_records, document["boxes"], no_savings, exact=False
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("cross-toolchain measurement accepted no block savings")

    candidate_receipt = composition.load_json(
        root
        / document["clean_distribution_receipts"]["candidate"]["receipt_path"]
    )
    retained_root = candidate_receipt["root_image"]
    live_root = {
        field: retained_root[field]
        for field in (
            "partition_blocks",
            "inode_list_blocks",
            "free_blocks",
            "free_inodes",
            "data_blocks",
        )
    }
    live_root["data_blocks"] += 1
    live_root["free_blocks"] -= 1
    composition.validate_live_root_measurement(
        document, retained_root, live_root, exact=False
    )
    try:
        composition.validate_live_root_measurement(
            document, retained_root, live_root, exact=True
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("exact root measurement accepted cross-toolchain drift")
    invalid_root = dict(live_root)
    invalid_root["partition_blocks"] += 1
    try:
        composition.validate_live_root_measurement(
            document, retained_root, invalid_root, exact=False
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("cross-toolchain root accepted partition drift")

    with tempfile.TemporaryDirectory() as temporary_name:
        map_path = Path(temporary_name) / "fixture.map"
        map_path.write_text(
            "Archive member included to satisfy reference by file (symbol)\n"
            "/build/libc.a(puts.o) caller.o (puts)\n"
            "/build/libc.a(write.o) /build/libc.a(puts.o) (write)\n"
            "                0x00000000 puts\n",
            encoding="utf-8",
        )
        members = composition.parse_libc_members(map_path)
        if members != ("libc.a(puts.o)", "libc.a(write.o)"):
            raise AssertionError(f"link-map member parse drift: {members}")

    retained_members = ("libc.a(memcpy.o)", "libc.a(write.o)")
    linux_members = (
        "libc.a(memcpy.o)",
        "libc.a(memmove.o)",
        "libc.a(write.o)",
    )
    added_members, removed_members = composition.validate_live_archive_members(
        retained_members, linux_members, exact=False
    )
    if added_members != ("libc.a(memmove.o)",) or removed_members:
        raise AssertionError(
            "cross-toolchain archive delta accounting differs: "
            f"added={added_members}, removed={removed_members}"
        )
    try:
        composition.validate_live_archive_members(
            retained_members, linux_members, exact=True
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("exact archive replay accepted a linker-member delta")
    try:
        composition.validate_live_archive_members(
            retained_members, (), exact=False
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("cross-toolchain archive accounting accepted no members")

    version_summary = {
        "arm_cc_version": "recorded compiler",
        "arm_ld_version": "recorded linker",
        "bmake_version": "recorded bmake",
    }
    with mock.patch.object(
        composition,
        "command_version",
        side_effect=("recorded compiler", "different linker", "recorded bmake"),
    ):
        exact, observed_cc, observed_ld, observed_bmake = (
            composition.measurement_environment_matches(version_summary)
        )
    if exact or (
        observed_cc,
        observed_ld,
        observed_bmake,
    ) != ("recorded compiler", "different linker", "recorded bmake"):
        raise AssertionError("ARM linker drift retained exact measurement mode")

    cases = []

    changed = copy.deepcopy(document)
    changed["claims"]["posix_conformance"] = True
    cases.append(("POSIX conformance claim", changed))

    changed = copy.deepcopy(document)
    changed["denominator"]["multicall_executables"] += 1
    cases.append(("multicall denominator drift", changed))

    changed = copy.deepcopy(document)
    changed["denominator"]["manifest_hard_links"] -= 1
    cases.append(("manifest hard-link drift", changed))

    changed = copy.deepcopy(document)
    changed["boxes"].append(copy.deepcopy(changed["boxes"][0]))
    cases.append(("duplicate box", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["shipped_names"].pop()
    cases.append(("missing shipped applet", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["libc_member_count"] += 1
    cases.append(("archive-member total drift", changed))

    changed = copy.deepcopy(document)
    changed["archive_summary"]["consumer_edges"] += 1
    cases.append(("archive consumer-edge drift", changed))

    changed = copy.deepcopy(document)
    changed["root"]["candidate"]["free_blocks"] += 1
    cases.append(("packed-root arithmetic drift", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["raw_bytes"] = -1
    cases.append(("negative raw byte measurement", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["packed_bytes"] = 999999999
    cases.append(("forged packed byte measurement", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["raw_blocks"] = -10
    cases.append(("negative raw block measurement", changed))

    changed = copy.deepcopy(document)
    changed["root"]["candidate"]["links"] -= 1
    changed["root"]["delta"]["links"] = 0
    cases.append(("candidate manifest-link drift", changed))

    changed = copy.deepcopy(document)
    changed["root"]["candidate"]["data_blocks"] += 1
    changed["root"]["candidate"]["free_blocks"] -= 1
    changed["root"]["delta"]["data_blocks"] += 1
    changed["root"]["delta"]["free_blocks"] -= 1
    cases.append(("positive candidate root growth", changed))

    changed = copy.deepcopy(document)
    changed["clean_distribution_receipts"]["candidate"]["exit_code"] = 1
    cases.append(("failed clean-distribution receipt", changed))

    changed = copy.deepcopy(document)
    changed["clean_distribution_receipts"]["candidate"]["receipt_sha256"] = (
        "0" * 64
    )
    cases.append(("placeholder clean-distribution digest", changed))

    changed = copy.deepcopy(document)
    changed["clean_distribution_receipts"]["candidate"]["receipt_path"] = (
        "docs/research/missing-multicall-receipt.json"
    )
    cases.append(("missing clean-distribution receipt", changed))

    changed = copy.deepcopy(document)
    changed["clean_distribution_receipts"]["candidate"]["receipt_sha256"] = (
        "f" * 64
    )
    cases.append(("clean-distribution digest mismatch", changed))

    changed = copy.deepcopy(document)
    first_source = next(iter(changed["source_sha256"]))
    changed["source_sha256"][first_source] = "0" * 64
    cases.append(("source identity drift", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["aliases"].append("duplicate:cat")
    cases.append(("Makefile member drift", changed))

    failures = makefile_contract_failures(root)
    failures.extend(
        name for name, changed in cases
        if not mutation_is_rejected(root, changed)
    )
    for receipt_name in ("base", "candidate"):
        if "receipt_path" not in document["clean_distribution_receipts"][receipt_name]:
            failures.append(f"missing retained {receipt_name} receipt")
    if "measurement_inventory" not in document:
        failures.append("missing retained size inventory")
    if failures:
        raise AssertionError("unrejected controls: " + ", ".join(failures))

    makefile_contract_calibration(root)
    list_boxes_calibration()
    live_mode_calibration(root, document)
    print(
        "PASS DPUCP-2024 multicall composition calibration: "
        f"{len(cases)} mutations rejected"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
