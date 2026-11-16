"""Exercise the compiler matrix input identity boundary using real Git trees."""

import fcntl
import json
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import check_kernel_headroom as headroom
import compiler_matrix_identity as identity

ROOT = Path(__file__).resolve().parents[2]
CAPTURE = ROOT / "tools/compiler_matrix_identity.py"
MATRIX = ROOT / "tools/capture_compiler_matrix.py"


def run(arguments, cwd, success=True, environment=None):
    result = subprocess.run(
        arguments, cwd=cwd, text=True, capture_output=True, check=False, env=environment,
    )
    if (result.returncode == 0) != success:
        raise AssertionError(
            f"{arguments!r} returned {result.returncode}: {result.stderr}"
        )
    return result


def main():
    with tempfile.TemporaryDirectory(prefix="compiler-matrix-'contract-") as temp:
        root = Path(temp) / "repo"
        root.mkdir()
        run(["git", "init", "-q"], root)
        run(["git", "config", "user.name", "Fixture"], root)
        run(["git", "config", "user.email", "fixture@example.invalid"], root)
        tracked = root / "source.c"
        tracked.write_text("int source;\n", encoding="utf-8")
        linker = root / "sys/arch/rp2040/conf/RP2040.ld"
        linker.parent.mkdir(parents=True)
        linker.write_text("MEMORY {\n FLASH : ORIGIN = 0x10000100, "
                          "LENGTH = 128K - 256\n}\n", encoding="utf-8")
        run(["git", "add", "source.c", str(linker)], root)
        run(["git", "commit", "-qm", "fixture"], root)
        good_elf = Path(temp) / "good.elf"
        low_elf = Path(temp) / "low.elf"
        origin, length = 0x10000100, 128 * 1024 - 256
        good_elf.write_bytes(headroom.synthetic_elf([(headroom.PT_LOAD, origin, length - 1024)]))
        low_elf.write_bytes(headroom.synthetic_elf([(headroom.PT_LOAD, origin, length - 1023)]))
        good_elf_shell = shlex.quote(str(good_elf))
        low_elf_shell = shlex.quote(str(low_elf))
        generated = root / "generated.c"
        generated.write_text("int generated;\n", encoding="utf-8")
        host_tool = root / "tool"
        host_tool.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        host_tool.chmod(0o755)
        build_dir = root / "sys/arch/rp2040/compile/PICO"
        build_dir.mkdir(parents=True)
        (build_dir / "ioconf.c").write_text("int config;\n", encoding="utf-8")
        tool_dir = root / "tools/bin/rp2040"
        tool_dir.mkdir(parents=True)
        for name in ("config", "boot2sum"):
            executable = tool_dir / name
            executable.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            executable.chmod(0o755)
        manifest = Path(temp) / "manifest.json"
        command = [sys.executable, str(CAPTURE), "--root", str(root),
                   "--manifest", str(manifest),
                   "--generated-input", "generated.c", "--host-tool", "tool",
                   "--generated-input", "sys/arch/rp2040/compile/PICO/ioconf.c",
                   "--host-tool", "tools/bin/rp2040/config",
                   "--host-tool", "tools/bin/rp2040/boot2sum"]
        run(command + ["--record-only"], root)
        record = json.loads(manifest.read_text(encoding="utf-8"))
        assert record["schema"] == 1
        run(command + ["--verify-only"], root)

        tracked.write_text("int changed;\n", encoding="utf-8")
        run(command + ["--verify-only"], root, success=False)
        dirty_manifest = Path(temp) / "dirty.json"
        dirty_command = command.copy()
        dirty_command[dirty_command.index(str(manifest))] = str(dirty_manifest)
        run(dirty_command + ["--record-only"], root, success=False)
        tracked.write_text("int source;\n", encoding="utf-8")
        generated.write_text("int changed;\n", encoding="utf-8")
        run(command + ["--verify-only"], root, success=False)
        generated.write_text("int generated;\n", encoding="utf-8")
        host_tool.chmod(0o644)
        run(command + ["--verify-only"], root, success=False)
        host_tool.chmod(0o755)
        host_tool.unlink()
        run(command + ["--verify-only"], root, success=False)
        host_tool.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        host_tool.chmod(0o755)
        tracked.write_text("int staged;\n", encoding="utf-8")
        run(["git", "add", "source.c"], root)
        run(command + ["--verify-only"], root, success=False)

        tracked.write_text("int source;\n", encoding="utf-8")
        run(["git", "add", "source.c"], root)
        bmake = root / "bmake"
        bmake.write_text(
            "#!/bin/sh\n"
            "[ \"${LC_ALL-}\" = C ] || exit 8\n"
            "[ \"${COMPILER_PATH+x}\" != x ] || exit 9\n"
            "case ${SOURCE_DATE_EPOCH-} in ''|*[!0-9]*) exit 10;; esac\n"
            "case \" $* \" in *' MAKE_VERSION '*) printf 'fixture make\\n'; exit 0;; esac\n"
            "case \" $* \" in *' clean '*) "
            "rm -f sys/arch/rp2040/compile/PICO/unix "
            "sys/arch/rp2040/compile/PICO/vers.c; exit 0;; esac\n"
            "case \" $* \" in *' COPTS=-O3 '*) "
            "printf '%s\\n' 'arm-none-eabi-ld: region FLASH overflowed by 1 byte'; "
            "exit 1;; esac\n"
            "printf '%s\\n' \"$SOURCE_DATE_EPOCH\" > sys/arch/rp2040/compile/PICO/vers.c\n"
            f"case \" $* \" in *' COPTS=-O2 '*) cp {low_elf_shell} "
            "sys/arch/rp2040/compile/PICO/unix;; "
            f"*) cp {good_elf_shell} sys/arch/rp2040/compile/PICO/unix;; esac\n",
            encoding="utf-8",
        )
        bmake.chmod(0o755)
        for name in ("gcc16-gcc", "gcc16-ld", "gcc16-objcopy", "gcc16-size", "gcc16-nm",
                     "gcc16-objdump", "gcc16-cc1", "gcc16-as", "arm13-gcc",
                     "arm13-ld", "arm13-objcopy", "arm13-size", "arm13-objdump", "arm13-nm",
                     "arm13-cc1", "arm13-as", "gcc16-collect2", "arm13-collect2",
                     "clang", "clang-ld"):
            compiler = root / name
            compiler.write_text(
                "#!/bin/sh\n"
                f"case \" $* \" in *' -print-libgcc-file-name '*) "
                f"printf '%s\\n' {shlex.quote(str(root / 'libgcc.a'))}; exit 0;; esac\n"
                "case \" $* \" in *' -print-prog-name=cc1 '*) "
                "printf '%s\\n' \"${0%-gcc}-cc1\"; exit 0;; esac\n"
                "case \" $* \" in *' -print-prog-name=as '*) "
                "printf '%s\\n' \"${0%-gcc}-as\"; exit 0;; esac\n"
                "case \" $* \" in *' -print-prog-name=ld '*) "
                "printf '%s\\n' \"${0%-gcc}-ld\"; exit 0;; esac\n"
                "case \" $* \" in *' -print-prog-name=collect2 '*) "
                "printf '%s\\n' \"${0%-gcc}-collect2\"; exit 0;; esac\n"
                "printf 'fixture 1\\n'\n", encoding="utf-8",
            )
            compiler.chmod(0o755)
        for name in ("gcc16-nm", "arm13-nm"):
            (root / name).write_text(
                "#!/bin/sh\n"
                "case \" $* \" in *' --version '*) printf 'fixture 1\\n';; "
                "*fixture.o*) printf 'source T 0 4\\n';; esac\n", encoding="utf-8"
            )
        runtime = root / "libgcc.a"
        runtime.write_bytes(b"fixture runtime")
        output = Path(temp) / "matrix"
        matrix_command = [sys.executable, str(MATRIX), "--root", str(root),
                          "--manifest", str(manifest), "--output", str(output),
                          "--generated-input", "generated.c", "--host-tool", "tool",
                          "--generated-input", "sys/arch/rp2040/compile/PICO/ioconf.c",
                          "--host-tool", "tools/bin/rp2040/config",
                          "--host-tool", "tools/bin/rp2040/boot2sum",
                          "--gcc16-prefix", str(root / "gcc16"),
                          "--arm13-prefix", str(root / "arm13"),
                          "--clang", str(root / "clang"),
                          "--clang-libgcc", str(runtime), "--bmake", str(bmake)]
        shadow_header = build_dir / "stdint.h"
        shadow_header.write_text("#error unexpected include\n", encoding="utf-8")
        shadow_output = Path(temp) / "shadow-matrix"
        shadow_command = matrix_command.copy()
        shadow_command[shadow_command.index(str(output))] = str(shadow_output)
        run(shadow_command, root, success=False)
        assert not (shadow_output / "tool-inputs.json").exists(), (
            "an untracked header entered an include-search directory"
        )
        shadow_header.unlink()
        hostile_environment = os.environ.copy()
        hostile_environment["LC_ALL"] = "fixture-invalid-locale"
        hostile_environment["COMPILER_PATH"] = "/untrusted/fixture"
        hostile_environment.pop("SOURCE_DATE_EPOCH", None)
        run(matrix_command, root, success=False, environment=hostile_environment)
        rows = json.loads((output / "results.json").read_text(encoding="utf-8"))
        tool_inputs = json.loads((output / "tool-inputs.json").read_text(encoding="utf-8"))
        assert (output / "source-manifest.json").read_bytes() == manifest.read_bytes()
        assert tool_inputs["gcc16_gcc"]["version"].startswith("fixture 1")
        assert tool_inputs["gcc16_libgcc"]["sha256"]
        assert tool_inputs["gcc16_cc1"]["sha256"]
        assert tool_inputs["gcc16_as"]["sha256"]
        assert tool_inputs["arm13_size"]["sha256"]
        assert tool_inputs["gcc16_collect2"]["sha256"]
        assert tool_inputs["clang_ld"]["sha256"]
        environment_record = json.loads(
            (output / "build-environment.json").read_text(encoding="utf-8")
        )
        assert environment_record["LC_ALL"] == "C"
        expected_epoch = run(["git", "show", "-s", "--format=%ct", "HEAD"], root).stdout.strip()
        assert environment_record["SOURCE_DATE_EPOCH"] == expected_epoch
        assert len(rows) == 24
        for row in rows:
            row_dir = output / f"{row['compiler']}-{row['level']}"
            identity.verify_artifacts(row_dir, row["artifacts_sha256"])
            assert {"clean.log", "build.log"}.issubset(row["artifacts_sha256"])
        control_dir = output / "gcc16-O1"
        control_hashes = rows[2]["artifacts_sha256"]
        assert rows[2]["compiler"] == "gcc16" and rows[2]["level"] == "O1"
        control_file = control_dir / "unix"
        control_bytes = control_file.read_bytes()
        control_file.write_bytes(control_bytes + b"altered")
        try:
            identity.verify_artifacts(control_dir, control_hashes)
        except ValueError:
            pass
        else:
            raise AssertionError("changed matrix artifact was accepted")
        control_file.write_bytes(control_bytes)
        extra = control_dir / "unexpected"
        extra.write_bytes(b"unexpected")
        try:
            identity.verify_artifacts(control_dir, control_hashes)
        except ValueError:
            pass
        else:
            raise AssertionError("unlisted matrix artifact was accepted")
        extra.unlink()
        control_file.unlink()
        try:
            identity.verify_artifacts(control_dir, control_hashes)
        except ValueError:
            pass
        else:
            raise AssertionError("missing matrix artifact was accepted")
        control_file.write_bytes(control_bytes)
        link = control_dir / "untrusted-link"
        link.symlink_to(control_file)
        try:
            identity.verify_artifacts(control_dir, control_hashes)
        except ValueError:
            pass
        else:
            raise AssertionError("matrix artifact symlink was accepted")
        link.unlink()
        identity.verify_artifacts(control_dir, control_hashes)
        assert sum(row["build_exit"] != 0 for row in rows) == 3
        assert sum(row["warnings"] for row in rows) == 0
        assert sum(row["build_errors"] for row in rows) == 3
        assert all(row["headroom_bytes"] == 1023 and not row["headroom_pass"]
                   for row in rows if row["level"] == "O2")
        assert all(row["headroom_bytes"] == 1024 and row["headroom_pass"]
                   for row in rows if row["level"] not in ("O2", "O3"))
        assert all(row["headroom_pass"] is None for row in rows if row["level"] == "O3")
        for level, success in (("O1", True), ("O2", False)):
            margin_output = Path(temp) / f"margin-{level}"
            margin_command = matrix_command.copy()
            margin_command[margin_command.index(str(output))] = str(margin_output)
            run(margin_command + ["--compiler", "gcc16", "--level", level],
                root, success=success)
        original_bmake = bmake.read_text(encoding="utf-8")
        malformed_elf = Path(temp) / "malformed.elf"
        malformed_elf.write_bytes(b"not an ELF")
        bmake.write_text(original_bmake.replace(good_elf_shell, shlex.quote(str(malformed_elf))),
                         encoding="utf-8")
        malformed_output = Path(temp) / "malformed-matrix"
        malformed_command = matrix_command.copy()
        malformed_command[malformed_command.index(str(output))] = str(malformed_output)
        run(malformed_command + ["--compiler", "gcc16", "--level", "O1"],
            root, success=False)
        malformed_row = json.loads((malformed_output / "results.json").read_text(
            encoding="utf-8"))[0]
        assert malformed_row["headroom_pass"] is False and malformed_row["headroom_error"]
        bmake.write_text(original_bmake, encoding="utf-8")
        stack_bmake = (
            "#!/bin/sh\n"
            "case \" $* \" in *' MAKE_VERSION '*) printf 'fixture make\\n'; exit 0;; esac\n"
            "case \" $* \" in *' clean '*) "
            "rm -rf sys/arch/rp2040/compile/PICO/.deps; "
            "rm -f sys/arch/rp2040/compile/PICO/*.o "
            "sys/arch/rp2040/compile/PICO/*.su "
            "sys/arch/rp2040/compile/PICO/unix "
            "sys/arch/rp2040/compile/PICO/vers.c; exit 0;; esac\n"
            "case \" $* \" in *' DEBUG=-g -gdwarf-4 -fstack-usage '*) :;; "
            "*) exit 11;; esac\n"
            "mkdir -p sys/arch/rp2040/compile/PICO/.deps\n"
            f"printf '%s\\n' {shlex.quote('fixture.o: ' + os.path.relpath(tracked, build_dir))} > "
            "sys/arch/rp2040/compile/PICO/.deps/fixture.dep\n"
            "printf object > sys/arch/rp2040/compile/PICO/fixture.o\n"
            "printf 'source.c:1:1:source\\t8\\tstatic\\n' > "
            "sys/arch/rp2040/compile/PICO/fixture.su\n"
            "printf 'int vers;\\n' > sys/arch/rp2040/compile/PICO/vers.c\n"
            "printf object > sys/arch/rp2040/compile/PICO/vers.o\n"
            ": > sys/arch/rp2040/compile/PICO/vers.su\n"
            f"cp {good_elf_shell} sys/arch/rp2040/compile/PICO/unix\n"
        )
        bmake.write_text(stack_bmake, encoding="utf-8")
        stack_output = Path(temp) / "stack-matrix"
        stack_command = matrix_command.copy()
        stack_command[stack_command.index(str(output))] = str(stack_output)
        stack_command.extend(["--compiler", "gcc16", "--level", "O1", "--stack-usage"])
        run(stack_command, root)
        stack_row = json.loads((stack_output / "results.json").read_text(encoding="utf-8"))[0]
        assert set(stack_row["stack_usage_sha256"]) == {"fixture.su", "vers.su"}
        assert (stack_output / "gcc16-O1/fixture.su").is_file()
        bmake.write_text(stack_bmake.replace(
            ": > sys/arch/rp2040/compile/PICO/vers.su\n", ""), encoding="utf-8")
        data_output = Path(temp) / "data-only-stack-matrix"
        data_command = stack_command.copy()
        data_command[data_command.index(str(stack_output))] = str(data_output)
        run(data_command, root)
        data_row = json.loads((data_output / "results.json").read_text(encoding="utf-8"))[0]
        assert data_row["stack_usage_no_code_objects"] == ["vers.o"]
        bmake.write_text(stack_bmake.replace(
            "sys/arch/rp2040/compile/PICO/fixture.su\n", "/dev/null\n"),
            encoding="utf-8")
        missing_output = Path(temp) / "missing-stack-matrix"
        missing_command = stack_command.copy()
        missing_command[missing_command.index(str(stack_output))] = str(missing_output)
        run(missing_command, root, success=False)
        assert not (missing_output / "results.json").exists()
        (build_dir / "stale.su").write_text("stale\n", encoding="utf-8")
        bmake.write_text(stack_bmake.replace(
            "sys/arch/rp2040/compile/PICO/*.su ", ""
        ), encoding="utf-8")
        stale_stack_output = Path(temp) / "stale-stack-matrix"
        stale_stack_command = stack_command.copy()
        stale_stack_command[stale_stack_command.index(str(stack_output))] = str(stale_stack_output)
        run(stale_stack_command, root, success=False)
        assert not (stale_stack_output / "results.json").exists()
        (build_dir / "stale.su").unlink()
        bmake.write_text(stack_bmake, encoding="utf-8")
        run([str(bmake), "clean"], root)
        bmake.write_text(original_bmake, encoding="utf-8")
        version_hashes = {row["version_sha256"] for row in rows if row["version_sha256"]}
        assert len(version_hashes) == 1
        for compiler in ("gcc16", "arm13", "clang"):
            assert not (output / f"{compiler}-O3/unix").exists(), (
                "a failed row retained another row's executable"
            )
        assert {row["level"] for row in rows} == {
            "O0", "O", "O1", "O2", "O3", "Og", "Os", "Oz"
        }
        invalid_epoch_output = Path(temp) / "invalid-epoch-matrix"
        invalid_epoch_command = matrix_command.copy()
        invalid_epoch_command[invalid_epoch_command.index(str(output))] = str(invalid_epoch_output)
        invalid_environment = os.environ.copy()
        invalid_environment["SOURCE_DATE_EPOCH"] = "invalid"
        invalid_result = run(invalid_epoch_command, root, success=False,
                             environment=invalid_environment)
        assert "SOURCE_DATE_EPOCH" in invalid_result.stderr
        bmake.write_text(
            "#!/bin/sh\n"
            "case \" $* \" in *' MAKE_VERSION '*) printf 'fixture make\\n'; exit 0;; esac\n"
            "case \" $* \" in *' clean '*) "
            "rm -f sys/arch/rp2040/compile/PICO/unix "
            "sys/arch/rp2040/compile/PICO/vers.c; exit 0;; esac\n"
            "case \" $* \" in *' COPTS=-Os '*) version=changed;; *) version=baseline;; esac\n"
            "printf '%s\\n' \"$version\" > sys/arch/rp2040/compile/PICO/vers.c\n"
            f"cp {good_elf_shell} sys/arch/rp2040/compile/PICO/unix\n",
            encoding="utf-8",
        )
        version_output = Path(temp) / "version-drift-matrix"
        version_command = matrix_command.copy()
        version_command[version_command.index(str(output))] = str(version_output)
        version_result = run(version_command + ["--compiler", "gcc16", "--level", "O1",
                                                "--level", "Os"], root, success=False)
        assert "different version payload" in version_result.stdout + version_result.stderr
        bmake.write_text(
            "#!/bin/sh\n"
            "case \" $* \" in *' MAKE_VERSION '*) printf 'fixture make\\n'; exit 0;; esac\n"
            "case \" $* \" in *' clean '*) "
            "rm -f sys/arch/rp2040/compile/PICO/unix "
            "sys/arch/rp2040/compile/PICO/vers.c; exit 0;; esac\n"
            "printf 'arm-none-eabi-ld: warning: fixture\\n'\n"
            f"cp {good_elf_shell} sys/arch/rp2040/compile/PICO/unix\n",
            encoding="utf-8",
        )
        warning_output = Path(temp) / "warning-matrix"
        warning_command = matrix_command.copy()
        warning_command[warning_command.index(str(output))] = str(warning_output)
        run(warning_command, root, success=False)
        warning_rows = json.loads(
            (warning_output / "results.json").read_text(encoding="utf-8")
        )
        assert len(warning_rows) == 24 and all(row["build_exit"] == 0 for row in warning_rows)
        assert all(row["warnings"] == 1 for row in warning_rows)
        assert all(row["errors"] == 0 for row in warning_rows)
        bmake.write_text(
            "#!/bin/sh\n"
            "case \" $* \" in *' MAKE_VERSION '*) printf 'fixture make\\n'; exit 0;; esac\n"
            "case \" $* \" in *' clean '*) printf 'warning: clean fixture\\n'; "
            "rm -f sys/arch/rp2040/compile/PICO/unix "
            "sys/arch/rp2040/compile/PICO/vers.c; exit 0;; esac\n"
            f"cp {good_elf_shell} sys/arch/rp2040/compile/PICO/unix\n",
            encoding="utf-8",
        )
        clean_warning_output = Path(temp) / "clean-warning-matrix"
        clean_warning_command = matrix_command.copy()
        clean_warning_command[clean_warning_command.index(str(output))] = str(
            clean_warning_output
        )
        run(clean_warning_command, root, success=False)
        clean_warning_rows = json.loads(
            (clean_warning_output / "results.json").read_text(encoding="utf-8")
        )
        assert len(clean_warning_rows) == 24
        assert all(row["warnings"] == 1 for row in clean_warning_rows)
        partial_output = Path(temp) / "partial-matrix"
        partial_command = matrix_command.copy()
        partial_command[partial_command.index(str(output))] = str(partial_output)
        run(partial_command + ["--compiler", "gcc16", "--level", "Os"],
            root, success=False)
        partial_rows = json.loads((partial_output / "results.json").read_text(encoding="utf-8"))
        partial_coverage = json.loads(
            (partial_output / "coverage.json").read_text(encoding="utf-8")
        )
        assert len(partial_rows) == 1 and not partial_coverage["complete_matrix"]
        unexpected = root / "sys/undeclared_input"
        unexpected.write_text("unexpected\n", encoding="utf-8")
        bmake.write_text(
            "#!/bin/sh\n"
            "case \" $* \" in *' MAKE_VERSION '*) printf 'fixture make\\n'; exit 0;; esac\n"
            "case \" $* \" in *' clean '*) "
            "rm -f sys/arch/rp2040/compile/PICO/unix "
            "sys/arch/rp2040/compile/PICO/vers.c; exit 0;; esac\n"
            "mkdir -p sys/arch/rp2040/compile/PICO/.deps\n"
            f"printf '%s\\n' {shlex.quote('fixture.o: ' + str(unexpected))} > "
            "sys/arch/rp2040/compile/PICO/.deps/fixture.dep\n"
            f"cp {good_elf_shell} sys/arch/rp2040/compile/PICO/unix\n",
            encoding="utf-8",
        )
        dependency_output = Path(temp) / "dependency-matrix"
        dependency_command = matrix_command.copy()
        dependency_command[dependency_command.index(str(output))] = str(dependency_output)
        run(dependency_command, root, success=False)
        assert not (dependency_output / "results.json").exists(), (
            "an undeclared dependency reached a successful matrix row"
        )
        unexpected.unlink()
        bmake.write_text("#!/bin/sh\nprintf 'fixture make\\n'\n", encoding="utf-8")
        stale_output = Path(temp) / "stale-matrix"
        stale_command = matrix_command.copy()
        stale_command[stale_command.index(str(output))] = str(stale_output)
        run(stale_command, root, success=False)
        assert not (stale_output / "results.json").exists(), (
            "the matrix continued after clean left an executable behind"
        )
        lock_output = Path(temp) / "locked-matrix"
        lock_command = matrix_command.copy()
        lock_command[lock_command.index(str(output))] = str(lock_output)
        with (root / ".build-logs/rp2040-products.lock").open("a+") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            run(lock_command, root, success=False)
        assert not (lock_output / "results.json").exists(), (
            "the matrix ran while another process owned the product lock"
        )
        forged_output = Path(temp) / "forged-matrix"
        forged_output.mkdir()
        forged_command = matrix_command.copy()
        forged_command[forged_command.index(str(output))] = str(forged_output)
        environment = os.environ.copy()
        environment["_DISCOBSD_EXECUTION_OWNERSHIP_INVOCATION"] = "forged"
        run(forged_command + ["--owned"], root, success=False, environment=environment)
        assert not (forged_output / "tool-inputs.json").exists(), (
            "an unowned invocation entered matrix execution"
        )
    print("PASS compiler-matrix input identity controls")


if __name__ == "__main__":
    main()
