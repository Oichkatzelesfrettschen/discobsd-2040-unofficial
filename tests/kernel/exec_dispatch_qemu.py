"""Execute production exec dispatch and its table with recording stubs."""

import argparse
import hashlib
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DISPATCH = ROOT / "sys/kern/kern_exec.c"
TABLE = ROOT / "sys/kern/exec_conf.c"
FIXTURE = ROOT / "tests/kernel/exec_dispatch_control.c"
LEVELS = ("O0", "O", "O1", "O2", "O3", "Og", "Os", "Oz")
START = """\
.syntax unified
.thumb
.text
.globl _start
.thumb_func
_start:
    bl main
    movs r7, #1
    svc #0
"""
FLAGS = ("-std=gnu17", "-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wundef",
         "-Wformat=2", "-ffreestanding", "-fno-builtin", "-fno-strict-aliasing",
         "-fshort-enums", "-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft")


def replace_once(text, original, replacement, label):
    if text.count(original) != 1:
        raise ValueError(f"{label}: expected one mutation boundary")
    return text.replace(original, replacement, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", action="append", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--qemu", default="qemu-arm")
    args = parser.parse_args()
    dispatch_source = DISPATCH.read_text(encoding="utf-8")
    table_source = TABLE.read_text(encoding="utf-8")
    signature = "int exec_check(struct exec_params *epp)\n"
    table_signature = "const struct execsw execsw[] = {"
    if dispatch_source.count(signature) != 1 or table_source.count(table_signature) != 1:
        raise ValueError("production dispatcher or table is not unique")
    start = dispatch_source.index(signature)
    end = dispatch_source.index("\nvoid\nexecv(void)", start)
    dispatch = dispatch_source[start:end]
    start = table_source.index(table_signature)
    end = table_source.index("int exec_maxhdrsz;", start) + len("int exec_maxhdrsz;")
    table = table_source[start:end]
    fixture = FIXTURE.read_text(encoding="utf-8")
    for marker in ("/* PRODUCTION_EXEC_TABLE */", "/* PRODUCTION_EXEC_CHECK */"):
        if fixture.count(marker) != 1:
            raise ValueError(f"fixture marker is not unique: {marker}")
    denied = "    if (access (epp->ip, IEXEC))\n        return ENOEXEC;"
    read_failure = "    if (error)\n        return error;"
    continuation = "i < nexecs && error == ENOEXEC"
    header_length = "epp->hdr_len = sizeof(epp->hdr) - r;"
    packed_slot = "{ exec_hsaout_check,"
    raw_slot = "{ exec_aout_check,"
    variants = []
    for packed in (False, True):
        define = ("-DEXEC_HSAOUT",) if packed else ()
        variants.extend((
            ("control", define, dispatch, table, 0),
            ("execute-denial", define, replace_once(dispatch, denied,
             denied.replace("return ENOEXEC", "return EACCES"), "execute denial"), table, 11),
            ("read-failure", define, replace_once(dispatch, read_failure,
             read_failure.replace("if (error)", "if (0 && error)"), "read failure"), table, 15),
            ("error-continuation", define, replace_once(dispatch, continuation,
             "i < nexecs && error != 0", "error continuation"), table, 21),
            ("header-residual", define, replace_once(dispatch, header_length,
             "epp->hdr_len = sizeof(epp->hdr);", "header residual"), table,
             16 if packed else 17),
        ))
    variants.append(("wrong-table-target", ("-DEXEC_HSAOUT",), dispatch,
                     replace_once(table, packed_slot, raw_slot, "packed handler"), 30))
    variants.append(("null-table-slot", ("-DEXEC_HSAOUT", "-DNULL_TEST"), dispatch,
                     replace_once(table, packed_slot, "{ NULL,", "null handler"), 0))
    dispatch_hash = hashlib.sha256(dispatch_source.encode()).hexdigest()
    table_hash = hashlib.sha256(table_source.encode()).hexdigest()
    print(f"exec-dispatch: kern_exec.c sha256={dispatch_hash} exec_conf.c sha256={table_hash}",
          flush=True)
    compilers = [(gcc,) for gcc in args.gcc] + [(args.clang, "--target=arm-none-eabi")]
    with tempfile.TemporaryDirectory(prefix="discobsd-exec-dispatch-") as temporary:
        directory = Path(temporary)
        (directory / "machine").symlink_to(
            ROOT / "sys/arch/rp2040/include", target_is_directory=True
        )
        startup = directory / "start.S"
        startup.write_text(START, encoding="utf-8")
        start_obj = directory / "start.o"
        subprocess.run([args.gcc[0], "-mcpu=cortex-m0plus", "-mthumb", "-c", str(startup),
                        "-o", str(start_obj)], check=True)
        for compiler in compilers:
            for level in LEVELS:
                for name, defines, body, selected_table, expected in variants:
                    source = fixture.replace("/* PRODUCTION_EXEC_TABLE */", selected_table)
                    source = source.replace("/* PRODUCTION_EXEC_CHECK */", body)
                    fixture_path = directory / "fixture.c"
                    fixture_path.write_text(source, encoding="utf-8")
                    obj, binary = directory / "fixture.o", directory / "fixture"
                    subprocess.run([*compiler, *FLAGS, f"-{level}", *defines,
                                    "-I", str(directory), "-I", str(ROOT / "sys"),
                                    "-c", str(fixture_path),
                                    "-o", str(obj)], check=True)
                    subprocess.run([args.gcc[0], "-mcpu=cortex-m0plus", "-mthumb", "-nostdlib",
                                    "-static", "-Wl,--build-id=none", "-Wl,-z,noexecstack",
                                    "-Wl,--fatal-warnings", "-Wl,-Ttext=0x20000",
                                    str(start_obj), str(obj), "-o", str(binary)], check=True)
                    result = subprocess.run([args.qemu, "-cpu", "cortex-m0", str(binary)],
                                            check=False, timeout=30)
                    if result.returncode != expected:
                        raise ValueError(f"{compiler} -{level} {name}: exit {result.returncode}, "
                                         f"expected {expected}")
            print(f"PASS exec-dispatch: {compiler} controls and mutants", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
