"""Calibrate shared-libc provider closure with compiler-produced ARM objects."""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import libc_shared_closure as closure

TARGET_COMPILER = os.environ.get("SHARED_CLOSURE_CC", "arm-none-eabi-gcc")
ARCHIVER = os.environ.get("SHARED_CLOSURE_AR", "arm-none-eabi-ar")


def contract(*names, external=()):
    return closure.read_contract({
        "schema": 1,
        "roots": [{"symbol": name, "kind": "function", "reason": "public test API"}
                  for name in names],
        "external_symbols": [{"symbol": name, "owner": "startup",
                              "reason": "startup-owned test state"} for name in external],
    })


class ClosureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="libc-closure-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.work = Path(cls.temporary.name)
        sources = {
            "api": "extern int helper(void); int api(void) { return helper(); }",
            "helper": "extern int state; int helper(void) { return state; }",
            "state": "int state = 7;",
            "dormant": "int dormant(void) { return 42; }",
            "broken": "extern int absent(void); int broken(void) { return absent(); }",
            "weak_api": "extern int optional(void) __attribute__((weak)); "
                        "int weak_api(void) { return optional ? optional() : 0; }",
            "optional": "int optional(void) { return 23; }",
            "alpha": "extern int beta(void); int alpha(void) { return beta(); }",
            "beta": "extern int alpha(void); int beta(void) { return alpha(); }",
            "collision": "int api(void) { return 99; }",
        }
        for name, source in sources.items():
            path = cls.work / (name + ".c")
            path.write_text(source + "\n")
            cls.command([TARGET_COMPILER, "-std=c17", "-pedantic-errors", "-Wall",
                         "-Wextra", "-Werror", "-ffreestanding", "-fno-common", "-Os",
                         "-mcpu=cortex-m0plus", "-mthumb", "-c", path,
                         "-o", cls.work / (name + ".o")])
        cls.archive = cls.work / "lib.a"
        cls.command([ARCHIVER, "rcs", cls.archive,
                     *(cls.work / (name + ".o") for name in sources if name != "collision")])
        cls.graph = closure.read_archive(cls.archive, ARCHIVER)

    @staticmethod
    def command(argv):
        result = subprocess.run([str(value) for value in argv], capture_output=True,
                                text=True, timeout=30, check=False)
        if result.returncode or result.stderr:
            raise RuntimeError(result.stdout + result.stderr)

    def comdat_object(self, name, *, value=7, state="state", local_state=False,
                      visibility=".hidden shared_helper", signature="shared_helper",
                      helper="shared_helper", prefix="", extra=""):
        source = self.work / (name + ".S")
        source.write_text(
            prefix + ".syntax unified\n.thumb\n.text\n.thumb_func\n.global api_" + name +
            "\napi_" + name + ":\nldr r1, 1f\nbx r1\n.align 2\n1: .word " + helper +
            '\n.section .text.shared_helper,"axG",%progbits,' + signature + ',comdat\n'
            ".align 2\n.thumb_func\n.global " + helper + "\n" + visibility +
            "\n.type " + helper + ",%function\n" + helper + ":\nmovs r0,#" + str(value) +
            "\nldr r1,1f\nbx lr\n.align 2\n1: .word " + state +
            "\n.size " + helper + ",.-" + helper + "\n" + extra +
            (".data\n.local state\nstate: .word 7\n" if local_state else ""))
        target = source.with_suffix(".o")
        self.command([TARGET_COMPILER, "-mcpu=cortex-m0plus", "-mthumb", "-c", source,
                      "-o", target])
        return target

    def comdat_archive(self, name, *objects):
        archive = self.work / (name + ".a")
        self.command([ARCHIVER, "rcs", archive, *objects])
        return closure.read_archive(archive, ARCHIVER)

    def test_identical_comdat_groups_coalesce_storage_and_match_gnu_linker(self):
        first = self.comdat_object("comdat_first")
        second = self.comdat_object("comdat_second", prefix=".local extra\n.equ extra,3\n")
        graph = self.comdat_archive("comdat_pair", first, second)
        specification = contract("api_comdat_first", "api_comdat_second", external=("state",))
        result = closure.resolve(graph, specification)
        self.assertEqual(result["members"], [first.name, second.name])
        size = graph["members"][first.name]["input_section_bytes"][".text.shared_helper"]
        self.assertEqual(result["input_section_bytes"][".text.shared_helper"], 2 * size)
        self.assertEqual(result["coalesced_input_section_bytes"][".text.shared_helper"], size)
        combined = self.work / "coalesced.o"
        self.command([TARGET_COMPILER.removesuffix("gcc") + "ld", "-r", first, second,
                      "-o", combined])
        actual = closure.read_member(combined.name, combined.read_bytes())
        self.assertEqual(actual["input_section_bytes"], result["coalesced_input_section_bytes"])
        reverse = self.comdat_archive("comdat_reverse", second, first)
        self.assertEqual(closure.provider(graph, "shared_helper"),
                         closure.provider(reverse, "shared_helper"))
        self.assertEqual(closure.resolve(reverse, specification)["coalesced_input_section_bytes"],
                         result["coalesced_input_section_bytes"])

    def test_comdat_rejects_different_code_targets_visibility_and_local_owners(self):
        first = self.comdat_object("equivalent_base")
        for variant, options in (
                ("code", {"value": 8}), ("target", {"state": "another_state"}),
                ("visibility", {"visibility": ""}), ("local", {"local_state": True})):
            with self.subTest(variant=variant):
                second = self.comdat_object("different_" + variant, **options)
                graph = self.comdat_archive("different_" + variant, first, second)
                with self.assertRaisesRegex(closure.ClosureError, "conflicting providers"):
                    closure.provider(graph, "shared_helper")
        local_first = self.comdat_object("local_first", local_state=True)
        local_second = self.comdat_object("local_second", local_state=True)
        graph = self.comdat_archive("local_pair", local_first, local_second)
        with self.assertRaisesRegex(closure.ClosureError, "conflicting providers"):
            closure.provider(graph, "shared_helper")

    def test_comdat_signature_collision_rejects_disjoint_exports(self):
        first = self.comdat_object("signature_first", signature="group_token",
                                   helper="helper_first", visibility="")
        second = self.comdat_object("signature_second", signature="group_token",
                                    helper="helper_second", visibility="")
        graph = self.comdat_archive("signature_pair", first, second)
        with self.assertRaisesRegex(closure.ClosureError, "conflicting COMDAT groups"):
            closure.resolve(graph, contract("api_signature_first", "api_signature_second",
                                             external=("state",)))

    def test_comdat_nobits_storage_counts_once(self):
        extra = ('.section .bss.shared,"awG",%nobits,shared_data,comdat\n'
                 '.global shared_data\n.type shared_data,%object\n'
                 'shared_data: .space 16\n.size shared_data,16\n')
        first = self.comdat_object("storage_first", extra=extra)
        second = self.comdat_object("storage_second", extra=extra)
        graph = self.comdat_archive("storage_pair", first, second)
        result = closure.resolve(graph, contract("api_storage_first", "api_storage_second",
                                                external=("state",)))
        self.assertEqual(result["input_section_bytes"][".bss.shared"], 32)
        self.assertEqual(result["coalesced_input_section_bytes"][".bss.shared"], 16)

    def test_group_metadata_controls_reject_before_provider_selection(self):
        source = self.comdat_object("metadata_control")
        content = source.read_bytes()
        elf = closure.ELFFile(closure.io.BytesIO(content))
        group = next(section for section in elf.iter_sections()
                     if section["sh_type"] == "SHT_GROUP")
        indexes = closure.struct.unpack("<" + "I" * (group.data_size // 4), group.data())[1:]
        changes = (
            (group["sh_offset"], 0, "unsupported section group flags"),
            (group["sh_offset"] + 4, 0, "invalid or overlapping group ownership"),
            (group["sh_offset"] + 8, indexes[0], "repeated group member"),
            (elf["e_shoff"] + indexes[0] * elf["e_shentsize"] + 8, 6,
             "group member lacks SHF_GROUP"),
        )
        for offset, value, verdict in changes:
            with self.subTest(verdict=verdict):
                mutated = bytearray(content)
                closure.struct.pack_into("<I", mutated, offset, value)
                with self.assertRaisesRegex(closure.ClosureError, verdict):
                    closure.read_member(source.name, mutated)

    def test_relocation_cannot_escape_group_fingerprint(self):
        source = self.comdat_object("escaping_relocation")
        content = source.read_bytes()
        elf = closure.ELFFile(closure.io.BytesIO(content))
        group_index, group = next((index, section) for index, section in
                                  enumerate(elf.iter_sections())
                                  if section["sh_type"] == "SHT_GROUP")
        indexes = closure.struct.unpack("<" + "I" * (group.data_size // 4), group.data())[1:]
        relocation_index = next(index for index in indexes
                                if elf.get_section(index)["sh_type"] == "SHT_REL")
        kept = [index for index in indexes if index != relocation_index]
        mutated = bytearray(content)
        closure.struct.pack_into("<I", mutated,
                                 elf["e_shoff"] + group_index * elf["e_shentsize"] + 20,
                                 4 * (1 + len(kept)))
        for position, index in enumerate(kept, 1):
            closure.struct.pack_into("<I", mutated, group["sh_offset"] + 4 * position, index)
        section = elf.get_section(relocation_index)
        closure.struct.pack_into("<I", mutated,
                                 elf["e_shoff"] + relocation_index * elf["e_shentsize"] + 8,
                                 section["sh_flags"] & ~0x200)
        with self.assertRaisesRegex(closure.ClosureError, "relocation bypasses COMDAT ownership"):
            closure.read_member(source.name, mutated)

    def test_external_reference_to_group_local_rejects(self):
        source = self.comdat_object("incoming_local").with_suffix(".S")
        source.write_text(source.read_text().replace(".word shared_helper", ".word .Linside")
                          .replace("shared_helper:\n", "shared_helper:\n.Linside:\n"))
        target = source.with_suffix(".o")
        self.command([TARGET_COMPILER, "-mcpu=cortex-m0plus", "-mthumb", "-c", source,
                      "-o", target])
        with self.assertRaisesRegex(closure.ClosureError, "external reference.*local symbol"):
            closure.read_member(target.name, target.read_bytes())

    def test_external_section_link_to_group_storage_rejects(self):
        source = self.comdat_object("incoming_section")
        content = source.read_bytes()
        elf = closure.ELFFile(closure.io.BytesIO(content))
        mutated = bytearray(content)
        target_index = elf.get_section_index(".text.shared_helper")
        source_index = elf.get_section_index(".text")
        closure.struct.pack_into("<I", mutated,
                                 elf["e_shoff"] + source_index * elf["e_shentsize"] + 24,
                                 target_index)
        with self.assertRaisesRegex(closure.ClosureError, "external section reference"):
            closure.read_member(source.name, mutated)

    def test_production_shared_syscalls_resolve_one_helper(self):
        directory = Path(__file__).resolve().parents[1] / "lib/libc/arm/sys"
        objects = []
        for name in ("close", "read"):
            source = self.work / ("production_" + name + ".S")
            source.write_text('#include "SYS.h"\nSYS(' + name + ')\n')
            target = source.with_suffix(".o")
            self.command([TARGET_COMPILER, "-mcpu=cortex-m0plus", "-mthumb",
                          "-DDISCOBSD_SHARED_LIBC=1", "-nostdinc", "-I" + str(directory),
                          "-I" + str(directory.parents[3] / "include"),
                          "-c", source, "-o", target])
            objects.append(target)
        graph = self.comdat_archive("production_pair", *objects)
        result = closure.resolve(graph, contract("close", "read", external=("errno",)))
        helper = ".text.__libc_shared_error"
        self.assertEqual(result["input_section_bytes"][helper],
                         2 * result["coalesced_input_section_bytes"][helper])
        self.assertEqual(result["external_imports"], ["errno"])

    def test_group_signature_uses_elf_names_without_api_identifier_restriction(self):
        first = self.comdat_object("named_first", signature="group.signature")
        second = self.comdat_object("named_second", signature="group.signature")
        graph = self.comdat_archive("named_pair", first, second)
        result = closure.resolve(graph, contract("api_named_first", "api_named_second",
                                                external=("state",)))
        self.assertEqual(list(result["comdat_groups"]), ["group.signature"])

    def test_rela_addends_enter_group_identity(self):
        source = self.comdat_object("rela_base")
        content = source.read_bytes()
        elf = closure.ELFFile(closure.io.BytesIO(content))
        index = elf.get_section_index(".rel.text.shared_helper")
        section = elf.get_section(index)
        members = {}
        for addend in (0, 1):
            mutated = bytearray(content)
            mutated.extend(b"\x00" * (-len(mutated) % 4))
            offset = len(mutated)
            for relocation in section.iter_relocations():
                mutated.extend(closure.struct.pack("<IIi", relocation["r_offset"],
                                                   relocation["r_info"], addend))
            header = elf["e_shoff"] + index * elf["e_shentsize"]
            for field, value in ((4, 4), (16, offset), (20, len(mutated) - offset), (36, 12)):
                closure.struct.pack_into("<I", mutated, header + field, value)
            members[f"rela_{addend}.o"] = closure.read_member(f"rela_{addend}.o", mutated)
        graph = {"members": members, "providers": {"shared_helper": list(members)}}
        with self.assertRaisesRegex(closure.ClosureError, "conflicting providers"):
            closure.provider(graph, "shared_helper")

    def test_malformed_relocation_symbol_ownership_rejects(self):
        source = self.comdat_object("invalid_symbol")
        content = source.read_bytes()
        elf = closure.ELFFile(closure.io.BytesIO(content))
        index = elf.get_section_index(".rel.text.shared_helper")
        section = elf.get_section(index)
        mutated = bytearray(content)
        closure.struct.pack_into("<I", mutated,
                                 elf["e_shoff"] + index * elf["e_shentsize"] + 24,
                                 elf.get_section_index(".text"))
        with self.assertRaisesRegex(closure.ClosureError, "invalid relocation symbol table"):
            closure.read_member(source.name, mutated)
        mutated = bytearray(content)
        closure.struct.pack_into("<I", mutated, section["sh_offset"] + 4, 0xffffff02)
        with self.assertRaisesRegex(closure.ClosureError, "invalid relocation symbol index"):
            closure.read_member(source.name, mutated)

    def test_equivalent_weak_comdat_resolution_preserves_graph(self):
        objects = []
        for name in ("weak_group_z", "weak_group_a"):
            source = self.comdat_object(name).with_suffix(".S")
            source.write_text(source.read_text().replace(".global shared_helper",
                                                        ".weak shared_helper"))
            target = source.with_suffix(".o")
            self.command([TARGET_COMPILER, "-mcpu=cortex-m0plus", "-mthumb", "-c", source,
                          "-o", target])
            objects.append(target)
        graph = self.comdat_archive("weak_comdat_pair", *objects)
        before = json.dumps(graph, sort_keys=True)
        self.assertEqual(closure.provider(graph, "shared_helper"), "weak_group_a.o")
        self.assertEqual(json.dumps(graph, sort_keys=True), before)

    def test_transitive_dependencies_and_unused_member(self):
        result = closure.resolve(self.graph, contract("api"))
        self.assertEqual(result["members"], ["api.o", "helper.o", "state.o"])
        self.assertEqual(result["external_imports"], [])

    def test_explicit_api_survives_absent_program_callers(self):
        specification = contract("api", "dormant")
        result = closure.resolve(self.graph, specification)
        self.assertEqual(result["members"], ["api.o", "dormant.o", "helper.o", "state.o"])
        with self.assertRaisesRegex(closure.ClosureError, "required API dormant"):
            closure.verify(self.graph, specification, ["api.o", "helper.o", "state.o"])

    def test_missing_dependency_rejects_selected_members(self):
        with self.assertRaisesRegex(closure.ClosureError, "unresolved strong import state"):
            closure.verify(self.graph, contract("api"), ["api.o", "helper.o"])

    def test_cycle_reaches_a_finite_fixed_point(self):
        result = closure.resolve(self.graph, contract("alpha"))
        self.assertEqual(result["members"], ["alpha.o", "beta.o"])

    def test_optional_weak_import_does_not_force_a_provider(self):
        result = closure.resolve(self.graph, contract("weak_api"))
        self.assertEqual(result["members"], ["weak_api.o"])
        self.assertEqual(result["unresolved_weak"], ["optional"])
        result = closure.resolve(self.graph, contract("weak_api", "optional"))
        self.assertEqual(result["members"], ["optional.o", "weak_api.o"])
        self.assertEqual(result["unresolved_weak"], [])

    def test_strong_import_requires_a_provider_or_named_external_owner(self):
        with self.assertRaisesRegex(closure.ClosureError, "unresolved strong import absent"):
            closure.resolve(self.graph, contract("broken"))
        result = closure.resolve(self.graph, contract("broken", external=("absent",)))
        self.assertEqual(result["external_imports"], ["absent"])

    def test_public_root_cannot_be_satisfied_by_an_external_owner(self):
        with self.assertRaisesRegex(closure.ClosureError, "external.*API|API.*external"):
            contract("absent", external=("absent",))

    def test_kind_confusion_rejects_a_data_object_as_a_function(self):
        with self.assertRaisesRegex(closure.ClosureError, "kind.*state|state.*kind"):
            closure.resolve(self.graph, contract("state"))

    def test_conflicting_providers_reject_instead_of_following_archive_order(self):
        path = self.work / "collision.a"
        self.command([ARCHIVER, "rcs", path,
                      self.work / "api.o", self.work / "collision.o"])
        graph = closure.read_archive(path, ARCHIVER)
        with self.assertRaisesRegex(closure.ClosureError, "conflicting providers.*api"):
            closure.resolve(graph, contract("api"))

    def test_duplicate_member_names_reject_before_symbol_inspection(self):
        path = self.work / "duplicate.a"
        self.command([ARCHIVER, "qc", path,
                      self.work / "api.o", self.work / "api.o"])
        with self.assertRaisesRegex(closure.ClosureError, "duplicate.*member"):
            closure.read_archive(path, ARCHIVER)

    def test_case_aliases_reject_on_every_host_filesystem(self):
        objects = []
        for directory, name in (("upper", "_Exit.o"), ("lower", "_exit.o")):
            parent = self.work / directory
            parent.mkdir()
            path = parent / name
            path.write_bytes((self.work / "api.o").read_bytes())
            objects.append(path)
        archive = self.work / "case-aliases.a"
        self.command([ARCHIVER, "rcs", archive, *objects])
        with self.assertRaisesRegex(closure.ClosureError, "case-insensitive"):
            closure.read_archive(archive, ARCHIVER)

    def test_archive_paths_reject_without_extraction(self):
        payload = (self.work / "api.o").read_bytes()
        header = (f"{'../bad.o/':<16}{0:<12}{0:<6}{0:<6}{'100644':<8}"
                  f"{len(payload):<10}`\n").encode("ascii")
        path = self.work / "traversal.a"
        path.write_bytes(b"!<arch>\n" + header + payload + (b"\n" if len(payload) % 2 else b""))
        with self.assertRaisesRegex(closure.ClosureError, "unsafe archive member"):
            closure.read_archive(path, ARCHIVER)

    def test_object_in_executable_section_remains_an_object(self):
        source = self.work / "text_object.S"
        source.write_text(".text\n.global text_object\n.type text_object,%object\n"
                          ".size text_object,4\ntext_object:\n.word 7\n")
        target = self.work / "text_object.o"
        self.command([TARGET_COMPILER, "-mcpu=cortex-m0plus", "-mthumb",
                      "-c", source, "-o", target])
        member = closure.read_member("text_object.o", target.read_bytes())
        self.assertEqual(member["definitions"]["text_object"]["kind"], "object")

    def test_strong_provider_overrides_optional_weak_definition(self):
        source = self.work / "weak_dormant.c"
        source.write_text("__attribute__((weak)) int dormant(void) { return 0; }\n")
        target = self.work / "weak_dormant.o"
        self.command([TARGET_COMPILER, "-std=c17", "-Wall", "-Wextra", "-Werror",
                      "-mcpu=cortex-m0plus", "-mthumb", "-c", source, "-o", target])
        archive = self.work / "strong-weak.a"
        self.command([ARCHIVER, "rcs", archive, target, self.work / "dormant.o"])
        result = closure.resolve(closure.read_archive(archive, ARCHIVER),
                                 contract("dormant"))
        self.assertEqual(result["members"], ["dormant.o"])

    def test_archive_and_contract_boundaries(self):
        bad = self.work / "bad.a"
        bad.write_bytes(b"not an archive")
        with self.assertRaises(closure.ClosureError):
            closure.read_archive(bad, ARCHIVER)
        specification = {"schema": 1, "roots": [], "external_symbols": []}
        with self.assertRaisesRegex(closure.ClosureError, "root"):
            closure.read_contract(specification)
        specification["roots"] = [{"symbol": "../api", "kind": "function", "reason": "API"}]
        with self.assertRaisesRegex(closure.ClosureError, "symbol"):
            closure.read_contract(specification)
        specification["roots"] = [{"symbol": "api", "kind": "function", "reason": "API"}] * 2
        with self.assertRaisesRegex(closure.ClosureError, "duplicate"):
            closure.read_contract(specification)

    def test_thin_archive_requires_embedded_member_identity(self):
        archive = self.work / "thin.a"
        subprocess.run([ARCHIVER, "rcsT", archive.name, "api.o"], cwd=self.work,
                       capture_output=True, check=True, timeout=30)
        with self.assertRaisesRegex(closure.ClosureError, "regular embedded archive"):
            closure.read_archive(archive, ARCHIVER)

    def test_common_storage_requires_canonical_owner(self):
        source = self.work / "common.c"
        source.write_text("int shared_state;\n")
        target = self.work / "common.o"
        self.command([TARGET_COMPILER, "-std=c17", "-pedantic-errors", "-Wall",
                      "-Wextra", "-Werror", "-mcpu=cortex-m0plus", "-mthumb",
                      "-fcommon", "-c", source, "-o", target])
        with self.assertRaisesRegex(closure.ClosureError, "common symbol lacks sole owner"):
            closure.read_member("common.o", target.read_bytes())

    def test_ambiguous_weak_providers_require_explicit_ownership(self):
        objects = []
        for name in ("weak_first", "weak_second"):
            source = self.work / (name + ".c")
            source.write_text("__attribute__((weak)) int weak_root(void) { return 7; }\n")
            target = source.with_suffix(".o")
            self.command([TARGET_COMPILER, "-std=c17", "-pedantic-errors", "-Wall",
                          "-Wextra", "-Werror", "-mcpu=cortex-m0plus", "-mthumb",
                          "-c", source, "-o", target])
            objects.append(target)
        archive = self.work / "ambiguous.a"
        self.command([ARCHIVER, "rcs", archive, *objects])
        with self.assertRaisesRegex(closure.ClosureError, "conflicting providers.*weak_root"):
            closure.resolve(closure.read_archive(archive, ARCHIVER), contract("weak_root"))

    def test_report_is_deterministic_and_counts_a_member_once(self):
        specification = contract("api", "helper")
        first = closure.resolve(self.graph, specification)
        second = closure.resolve(self.graph, specification)
        self.assertEqual(json.dumps(first, sort_keys=True), json.dumps(second, sort_keys=True))
        self.assertEqual(first["members"], ["api.o", "helper.o", "state.o"])
        self.assertEqual(first["input_section_bytes"][".data"], 4)

    def test_cli_preserves_contract_and_rejects_duplicate_json_fields(self):
        specification = self.work / "contract.json"
        specification.write_text(json.dumps(contract("api")))
        argv = [sys.executable, str(Path(closure.__file__)), str(self.archive),
                "--ar", ARCHIVER, "--contract", str(specification)]
        result = subprocess.run(argv, capture_output=True, text=True, timeout=30, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["members"],
                         ["api.o", "helper.o", "state.o"])
        specification.write_text('{"schema":1,"schema":2,"roots":[],"external_symbols":[]}')
        result = subprocess.run(argv, capture_output=True, text=True, timeout=30, check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("duplicate JSON field", result.stderr)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
