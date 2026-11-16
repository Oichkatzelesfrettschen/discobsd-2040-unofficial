"""Calibrate configured-source and lexical call-graph capture."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IDENTITY = ROOT / "tools/compiler_matrix_identity.py"
CAPTURE = ROOT / "tools/capture_rp2040_callgraph.py"
GENERATED = ("sys/arch/rp2040/compile/PICO/ioconf.c",
             "sys/arch/rp2040/compile/PICO/swapunix.c",
             "sys/arch/rp2040/compile/PICO/vers.c")


def run(command, directory, expected=0):
    result = subprocess.run(command, cwd=directory, capture_output=True, text=True, check=False)
    if result.returncode != expected:
        raise AssertionError(f"{command!r}: exit {result.returncode}: {result.stderr}")
    return result


def main():
    programs = {name: shutil.which(name) for name in ("bmake", "cflow", "cscope")}
    if not all(programs.values()):
        raise AssertionError(f"graph fixture requires bmake, cflow and cscope: {programs}")
    with tempfile.TemporaryDirectory(prefix="callgraph-fixture-") as temporary:
        root = Path(temporary) / "repo"
        config = root / "sys/arch/rp2040/compile/PICO"
        source = root / "sys/kern/example.c"
        config.mkdir(parents=True)
        source.parent.mkdir(parents=True)
        source.write_text("int leaf(void) { return 7; }\n"
                          "int helper(void) { return leaf(); }\n"
                          "int entry(void) { return helper(); }\n", encoding="utf-8")
        (source.parent / "example.h").write_text("int entry(void);\n", encoding="utf-8")
        (config / "Makefile").write_text(
            "CFILES= ../../../../kern/example.c swapunix.c\n", encoding="utf-8"
        )
        for name in ("ioconf.c", "swapunix.c", "vers.c"):
            (config / name).write_text(f"int {name[:-2]}_value;\n", encoding="utf-8")
        run(["git", "init", "-q"], root)
        run(["git", "config", "user.name", "Fixture"], root)
        run(["git", "config", "user.email", "fixture@example.invalid"], root)
        run(["git", "add", "sys"], root)
        run(["git", "commit", "-qm", "fixture"], root)
        manifest = Path(temporary) / "manifest.json"
        identity = [sys.executable, str(IDENTITY), "--root", str(root),
                    "--manifest", str(manifest)]
        for name in GENERATED:
            identity += ["--generated-input", name]
        run(identity + ["--record-only"], root)
        output = Path(temporary) / "capture"
        command = [sys.executable, str(CAPTURE), "--root", str(root),
                   "--manifest", str(manifest), "--output", str(output),
                   "--bmake", programs["bmake"], "--cflow", programs["cflow"],
                   "--cscope", programs["cscope"]]
        run(command, root)
        record = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
        assert set(record["c_sources"]) == {
            "sys/kern/example.c", *GENERATED
        }
        assert record["cflow_passes"] >= 2
        graph = (output / "direct.dot").read_text(encoding="utf-8")
        assert "entry -> helper" in graph and "helper -> leaf" in graph
        assert (output / "cscope.out").is_file()
        query = run([programs["cscope"], "-d", "-L", "-3", "helper", "-f",
                     str(output / "cscope.out")], root)
        assert "sys/kern/example.c" in query.stdout and "entry" in query.stdout
        oversized_tool = Path(temporary) / "oversized-cflow"
        oversized_tool.write_text(
            "#!/bin/sh\nset -eu\n"
            "if [ \"${1-}\" = --version ]; then printf 'fixture 1\\n'; exit 0; fi\n"
            "head -c 2048 /dev/zero\n", encoding="utf-8"
        )
        oversized_tool.chmod(0o755)
        limited_output = Path(temporary) / "limited"
        limited = command.copy()
        limited[limited.index(str(output))] = str(limited_output)
        limited[limited.index(programs["cflow"])] = str(oversized_tool)
        run(limited + ["--output-limit-bytes", "1024"], root, expected=1)
        assert not (limited_output / "manifest.json").exists()
        assert (limited_output / "direct-pass-000.dot").stat().st_size <= 1024
        (config / "ioconf.c").unlink()
        missing = command.copy()
        missing[missing.index(str(output))] = str(Path(temporary) / "missing")
        run(missing, root, expected=1)
        (config / "ioconf.c").write_text("int ioconf_value;\n", encoding="utf-8")
        source.write_text("int leaf(void) { return 7; }\n"
                          "int helper(void) { return leaf(); }\n"
                          "int entry(void) { return 7; }\n", encoding="utf-8")
        changed = command.copy()
        changed[changed.index(str(output))] = str(Path(temporary) / "changed")
        run(changed, root, expected=1)
        assert not (Path(temporary) / "changed/manifest.json").exists()
        run(["git", "add", "sys/kern/example.c"], root)
        changed_manifest = Path(temporary) / "changed-source.json"
        changed_identity = identity.copy()
        changed_identity[changed_identity.index(str(manifest))] = str(changed_manifest)
        run(changed_identity + ["--record-only"], root)
        changed[changed.index(str(manifest))] = str(changed_manifest)
        run(changed, root)
        changed_graph = (Path(temporary) / "changed/direct.dot").read_text(encoding="utf-8")
        assert "helper -> leaf" in changed_graph
        assert "entry -> helper" not in changed_graph
    print("PASS configured call-graph capture and input identity controls")


if __name__ == "__main__":
    os.environ.setdefault("PYTHON", sys.executable)
    main()
