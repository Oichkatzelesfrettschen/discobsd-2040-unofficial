"""Exercise real compiler, archive and linker capture boundaries."""

import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

RECORDER = Path(__file__).with_name("record_tool.py")


class ToolCaptureTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.records = self.root / "records"
        self.compiler = shutil.which("arm-none-eabi-gcc")
        self.assertIsNotNone(self.compiler, "ARM cross compiler is required")
        (self.root / "used.h").write_text("#define VALUE 37\n")
        (self.root / "unused.h").write_text("#define UNUSED 91\n")
        (self.root / "main.c").write_text(
            '#include "used.h"\nint main(void) { return VALUE; }\n'
        )

    def capture(self, *arguments, input_bytes=None):
        return subprocess.run(
            [sys.executable, str(RECORDER), "--root", str(self.root),
             "--records", str(self.records), "--", *arguments],
            cwd=self.root, input=input_bytes, capture_output=True, check=False,
        )

    def receipts(self):
        return [json.loads(path.read_text()) for path in sorted(self.records.glob("*.json"))]

    def test_compile_tracks_used_header_and_preserves_object(self):
        normal = subprocess.run(
            [self.compiler, "-std=c17", "-Wall", "-Wextra", "-Werror",
             "-c", "main.c", "-o", "normal.o"],
            cwd=self.root, capture_output=True, check=False,
        )
        self.assertEqual(normal.returncode, 0, normal.stderr)
        result = self.capture(self.compiler, "-std=c17", "-Wall", "-Wextra",
                              "-Werror", "-c", "main.c", "-o", "captured.o")
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt, = self.receipts()
        dependencies = {row["path"] for row in receipt["dependencies"]}
        self.assertIn("used.h", dependencies)
        self.assertNotIn("unused.h", dependencies)
        self.assertEqual(receipt["kind"], "compile")
        self.assertEqual(receipt["exit_code"], 0)
        self.assertEqual((self.root / "normal.o").read_bytes(),
                         (self.root / "captured.o").read_bytes())
        output = receipt["outputs"][0]
        self.assertEqual(output["sha256"], hashlib.sha256(
            (self.root / "captured.o").read_bytes()).hexdigest())
        self.assertEqual((self.records / output["retained"]).read_bytes(),
                         (self.root / "captured.o").read_bytes())

    def test_preexisting_dependency_output_remains_available(self):
        result = self.capture(self.compiler, "-c", "main.c", "-o", "main.o",
                              "-MD", "-MP", "-MF", "original.d")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.root / "original.d").is_file())
        receipt, = self.receipts()
        self.assertIn("used.h", {row["path"] for row in receipt["dependencies"]})

    def test_failure_receipt_does_not_claim_output(self):
        (self.root / "bad.c").write_text("#error rejected fixture\n")
        result = self.capture(self.compiler, "-c", "bad.c", "-o", "bad.o")
        self.assertNotEqual(result.returncode, 0)
        receipt, = self.receipts()
        self.assertNotEqual(receipt["exit_code"], 0)
        self.assertEqual(receipt["outputs"], [])

    def test_stdin_source_and_preprocessor_stdout_are_preserved(self):
        source = b'#include "used.h"\nVALUE\n'
        result = self.capture(self.compiler, "-x", "c", "-E", "-P", "-I.", "-",
                              input_bytes=source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), b"37")
        receipt, = self.receipts()
        self.assertEqual(receipt["kind"], "preprocess")
        self.assertEqual(receipt["stdin_sha256"], hashlib.sha256(source).hexdigest())
        self.assertEqual((self.records / receipt["stdin_retained"]).read_bytes(), source)
        self.assertIn("used.h", {row["path"] for row in receipt["dependencies"]})

    def test_link_retains_map_and_selected_archive_member(self):
        archive_tool = shutil.which("arm-none-eabi-ar")
        self.assertIsNotNone(archive_tool, "archive tool is required")
        (self.root / "selected.c").write_text("int selected(void) { return 37; }\n")
        (self.root / "unused.c").write_text("int unused(void) { return 91; }\n")
        (self.root / "main.c").write_text(
            "int selected(void); int main(void) { return selected(); }\n"
        )
        for source in ["main", "selected", "unused"]:
            result = self.capture(self.compiler, "-c", source + ".c", "-o", source + ".o")
            self.assertEqual(result.returncode, 0, result.stderr)
        result = self.capture(archive_tool, "rcs", "libfixture.a", "selected.o", "unused.o")
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.capture(self.compiler, "-nostdlib", "-Wl,-e,main",
                              "main.o", "libfixture.a", "-o", "program")
        self.assertEqual(result.returncode, 0, result.stderr)
        link, = [row for row in self.receipts() if row["kind"] == "link"]
        map_text = (self.records / link["link_map"]["retained"]).read_text()
        self.assertIn("libfixture.a(selected.o)", map_text)
        self.assertNotIn("libfixture.a(unused.o)", map_text)
        archive, = [row for row in self.receipts() if row["kind"] == "archive"]
        self.assertEqual([row["path"] for row in archive["inputs"]],
                         ["selected.o", "unused.o"])

    def test_configuration_and_source_changes_have_distinct_receipts(self):
        for value in [1, 2]:
            result = self.capture(self.compiler, "-DPROFILE=" + str(value),
                                  "-c", "main.c", "-o", "main.o")
            self.assertEqual(result.returncode, 0, result.stderr)
        first, second = self.receipts()
        self.assertNotEqual(first["argv"], second["argv"])
        self.assertNotEqual(first["id"], second["id"])
        self.assertEqual(first["source"]["sha256"], second["source"]["sha256"])
        (self.root / "main.c").write_text("int main(void) { return 0; }\n")
        result = self.capture(self.compiler, "-c", "main.c", "-o", "main.o")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotEqual(self.receipts()[-1]["source"]["sha256"],
                            first["source"]["sha256"])

    def test_in_place_object_transform_retains_both_identities(self):
        object_copy = shutil.which("arm-none-eabi-objcopy")
        self.assertIsNotNone(object_copy, "objcopy is required")
        result = self.capture(self.compiler, "-c", "main.c", "-o", "main.o")
        self.assertEqual(result.returncode, 0, result.stderr)
        before = hashlib.sha256((self.root / "main.o").read_bytes()).hexdigest()
        result = self.capture(object_copy, "--redefine-sym", "main=renamed_main", "main.o")
        self.assertEqual(result.returncode, 0, result.stderr)
        transform, = [row for row in self.receipts() if row["kind"] == "transform"]
        self.assertEqual(transform["inputs"][0]["sha256"], before)
        self.assertNotEqual(transform["outputs"][0]["sha256"], before)

    def test_output_outside_checkout_is_rejected(self):
        with tempfile.TemporaryDirectory() as outside:
            target = Path(outside) / "forbidden.o"
            result = self.capture(self.compiler, "-c", "main.c", "-o", str(target))
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(target.exists())

    def test_assembly_output_operand_is_not_an_input_source(self):
        result = self.capture(self.compiler, "-S", "main.c", "-o", "main.s")
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt, = self.receipts()
        self.assertEqual([source["path"] for source in receipt["sources"]], ["main.c"])
        self.assertEqual(receipt["outputs"][0]["path"], "main.s")

    def test_redirected_preprocessor_output_has_a_producer(self):
        target = self.root / "generated.s"
        with target.open("wb") as output:
            result = subprocess.run(
                [sys.executable, str(RECORDER), "--root", str(self.root),
                 "--records", str(self.records), "--stdout-file", str(target),
                 "--", self.compiler, "-E", "-P", "main.c"],
                cwd=self.root, stdout=output, stderr=subprocess.PIPE, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt, = self.receipts()
        self.assertEqual(receipt["outputs"][0]["sha256"],
                         hashlib.sha256(target.read_bytes()).hexdigest())

    def test_attached_linker_script_is_retained(self):
        (self.root / "layout.ld").write_text(
            "SECTIONS { . = 0x10000; .text : { *(.text*) } }\n")
        self.capture(self.compiler, "-c", "main.c", "-o", "main.o")
        for index, flag in enumerate(["-Tlayout.ld", "-Wl,-T,layout.ld"]):
            result = self.capture(self.compiler, "-nostdlib", "-Wl,-e,main", flag,
                                  "main.o", "-o", "scripted" + str(index))
            self.assertEqual(result.returncode, 0, result.stderr)
        links = [row for row in self.receipts() if row["kind"] == "link"]
        self.assertEqual(len(links), 2)
        for row in links:
            self.assertIn("layout.ld", [record["path"] for record in row["inputs"]])

    def test_response_input_is_rejected_before_execution(self):
        (self.root / "arguments").write_text("-c main.c -o forbidden.o\n")
        result = self.capture(self.compiler, "@arguments")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"response", result.stderr)
        self.assertFalse((self.root / "forbidden.o").exists())

    def test_tool_query_stdout_is_forwarded(self):
        expected = subprocess.run([self.compiler, "-dumpmachine"],
                                  capture_output=True, check=False)
        result = self.capture(self.compiler, "-dumpmachine")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, expected.stdout)


if __name__ == "__main__":
    unittest.main()
