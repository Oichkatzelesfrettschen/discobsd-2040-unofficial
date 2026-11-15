"""Check exact callback types and ARM behavior in maintained qsort callers."""

import os
import re
import shlex
import subprocess

CASES = [
    (
        "bin/ls/ls.c",
        "fcmp",
        """
    struct afile a = {0}, b = {0};
    a.fname = "a"; b.fname = "b";
    dflg = 1; fflg = tflg = 0; rflg = 1;
    if (fcmp(&a, &b) >= 0) return 1;
    rflg = -1;
    if (fcmp(&a, &b) <= 0) return 2;
    tflg = 1; rflg = 1; a.fmtime = 100; b.fmtime = 200;
    if (fcmp(&a, &b) <= 0) return 3;
    rflg = -1;
    if (fcmp(&a, &b) >= 0) return 4;
    tflg = 0; dflg = 0; a.fmode = b.fmode = ISARG;
    a.ftype = 'd'; b.ftype = 'f';
    if (fcmp(&a, &b) <= 0 || fcmp(&b, &a) >= 0) return 5;
    dflg = 1;
    {
        struct afile rows[2];
        rows[0] = a; rows[1] = b;
        qsort(rows, 2, sizeof(rows[0]), fcmp);
        if (rows[0].fname != b.fname || rows[1].fname != a.fname) return 6;
    }
    return 0;
""",
    ),
    (
        "usr.bin/diff/diffdir.c",
        "entcmp",
        """
    struct dir rows[2] = {{0}, {0}};
    rows[0].d_entry = "b"; rows[1].d_entry = "a";
    if (entcmp(&rows[0], &rows[1]) <= 0 || entcmp(&rows[0], &rows[0]) != 0)
        return 1;
    qsort(rows, 2, sizeof(rows[0]), entcmp);
    return rows[0].d_entry[0] != 'a' || rows[1].d_entry[0] != 'b';
""",
    ),
    (
        "usr.bin/uucp/uuq.c",
        "jcompare",
        """
    struct job a = {0}, b = {0};
    struct job *rows[2];
    a.j_grade = 'B'; b.j_grade = 'A';
    a.j_jobno[0] = '2'; b.j_jobno[0] = '1';
    rows[0] = &a; rows[1] = &b;
    if (jcompare(&rows[0], &rows[1]) <= 0) return 1;
    a.j_grade = 'A';
    if (jcompare(&rows[0], &rows[1]) <= 0 || jcompare(&rows[0], &rows[0]) != 0)
        return 2;
    qsort(rows, 2, sizeof(rows[0]), jcompare);
    return rows[0] != &b || rows[1] != &a;
""",
    ),
    (
        "games/atc/log.c",
        "compar",
        """
    SCORE rows[3];
    memset(rows, 0, sizeof(rows));
    rows[0].planes = 1; rows[0].time = 100;
    rows[1].planes = 2; rows[1].time = 100;
    rows[2].planes = 2; rows[2].time = 200;
    if (compar(&rows[0], &rows[1]) <= 0 ||
        compar(&rows[1], &rows[2]) <= 0 || compar(&rows[0], &rows[0]) != 0)
        return 1;
    qsort(rows, 3, sizeof(rows[0]), compar);
    return rows[0].time != 200 || rows[1].planes != 2 || rows[2].planes != 1;
""",
    ),
    (
        "games/boggle/boggle.c",
        "compare",
        """
    char *rows[] = {"0dog", "0cat", "0bat"};
    if (compare(&rows[0], &rows[1]) <= 0 ||
        compare(&rows[0], &rows[0]) != 0) return 1;
    qsort(rows, 3, sizeof(rows[0]), compare);
    return rows[0][1] != 'b' || rows[1][1] != 'c' || rows[2][1] != 'd';
""",
    ),
    (
        "games/wump/wump.c",
        "icomp",
        """
    int rows[] = {12, 1, 5};
    if (icomp(&rows[0], &rows[1]) <= 0 ||
        icomp(&rows[0], &rows[0]) != 0) return 1;
    qsort(rows, 3, sizeof(rows[0]), icomp);
    return rows[0] != 1 || rows[1] != 5 || rows[2] != 12;
""",
    ),
    (
        "games/robots/score.c",
        "cmp_sc",
        """
    SCORE rows[3];
    memset(rows, 0, sizeof(rows));
    rows[0].s_score = 1; rows[1].s_score = 3; rows[2].s_score = 2;
    if (cmp_sc(&rows[0], &rows[1]) <= 0 ||
        cmp_sc(&rows[0], &rows[0]) != 0) return 1;
    qsort(rows, 3, sizeof(rows[0]), cmp_sc);
    return rows[0].s_score != 3 || rows[1].s_score != 2 || rows[2].s_score != 1;
""",
    ),
    (
        "lib/libtcl/tclcmdil.c",
        "SortCompareProc",
        """
    unsigned char *rows[] = {(unsigned char *)"c", (unsigned char *)"a",
                             (unsigned char *)"b"};
    if (SortCompareProc(&rows[0], &rows[1]) <= 0 ||
        SortCompareProc(&rows[0], &rows[0]) != 0) return 1;
    qsort(rows, 3, sizeof(rows[0]), SortCompareProc);
    return rows[0][0] != 'a' || rows[1][0] != 'b' || rows[2][0] != 'c';
""",
    ),
]


def type_rejection(result, name):
    pattern = r"error: conflicting types for ['‘]" + re.escape(name) + r"['’]"
    return (
        result.returncode == 1
        and not result.stdout
        and "fatal error:" not in result.stderr
        and "internal compiler error:" not in result.stderr
        and re.search(pattern, result.stderr)
    )


def calibrate_type_rejection():
    for status, output, diagnostic, expected in (
        (1, "", "error: conflicting types for 'fcmp'", True),
        (0, "", "error: conflicting types for 'fcmp'", False),
        (1, "", "fcmp/input.c: fatal error: missing header", False),
        (1, "", "error: conflicting types for 'fcmp'\nfatal error: missing header", False),
        (1, "", "error: conflicting types for 'fcmp'\ninternal compiler error: fault", False),
        (1, "", "", False),
        (-11, "", "error: conflicting types for 'fcmp'", False),
        (1, "unexpected output", "error: conflicting types for 'fcmp'", False),
    ):
        result = subprocess.CompletedProcess([], status, output, diagnostic)
        if bool(type_rejection(result, "fcmp")) != expected:
            raise RuntimeError("caller diagnostic classifier calibration differs")


def run(arguments, *, cwd=None, reject=None):
    environment = dict(os.environ, LC_ALL="C")
    environment.pop("MAKEFLAGS", None)
    environment.pop("MFLAGS", None)
    result = subprocess.run(
        arguments,
        cwd=cwd,
        capture_output=True,
        text=True,
        timeout=90,
        check=False,
        env=environment,
    )
    if reject is None:
        if result.returncode or result.stderr:
            raise RuntimeError(
                "caller command fails: " + repr(arguments) + "\n" + result.stdout + result.stderr
            )
    elif not type_rejection(result, reject):
        raise RuntimeError("caller type control misses named rejection: " + result.stderr)
    return result.stdout


def check_callers(root, compiler, work, execute=False):
    calibrate_type_rejection()
    driver = [compiler] if isinstance(compiler, str) else compiler
    flags = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]
    if execute:
        support = work / "caller-support.o"
        run(
            [
                *driver,
                *flags,
                "-std=gnu17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fomit-frame-pointer",
                "-c",
                str(root / "tests/libbn/qemu_support.c"),
                "-o",
                str(support),
            ]
        )
        provider = work / "caller-qsort.o"
        run(
            [
                *driver,
                *flags,
                "-std=c17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fno-builtin",
                "-c",
                str(root / "lib/libc/gen/qsort.c"),
                "-o",
                str(provider),
            ]
        )
    for path, name, body in CASES:
        original = root / path
        case = work / name
        case.mkdir()
        settings = run(
            ["bmake", "MACHINE=rp2040", "-V", "${CC}", "-V", "${CFLAGS}"], cwd=original.parent
        ).splitlines()
        if len(settings) != 2:
            raise RuntimeError("caller build profile is incomplete: " + path)
        command = shlex.split(settings[0]) + shlex.split(settings[1])
        command += ["-ffunction-sections", "-fdata-sections", "-I" + str(original.parent)]
        fixture = case / "fixture.c"
        obj = case / "fixture.o"
        linkage = "static " if name in {"cmp_sc", "SortCompareProc"} else ""
        prefix = (f"{linkage}int {name}(const void *, const void *);\n"
                  "#define main caller_program_main\n")
        suffix = "\n#undef main\nint main(void)\n{\n" + body + "}\n"
        text = original.read_text()
        fixture.write_text(prefix + '#include "' + str(original) + '"\n' + suffix)
        run(command + ["-c", str(fixture), "-o", str(obj)])
        # The incompatible declaration conflicts with the real callback type.
        damaged = case / original.name
        incompatible = (prefix.replace("int " + name, "void " + name) if linkage
                        else prefix.replace("const void *", "void *"))
        fixture.write_text(incompatible +
                           '#include "' + str(original) + '"\n' + suffix)
        run(command + ["-c", str(fixture), "-o", str(case / "bad.o")], reject=name)
        print("qsort caller type and incompatible control: " + path)
        if execute:
            program = case / "caller"
            run(
                [
                    *driver,
                    *flags,
                    "-nostartfiles",
                    "-Wl,-Ttext=0x20000",
                    "-Wl,--gc-sections",
                    str(obj),
                    str(provider),
                    str(support),
                    "-lc",
                    "-lgcc",
                    "-o",
                    str(program),
                ]
            )
            run(["qemu-arm", "-cpu", "cortex-m0", str(program)])
            print("qsort caller ARM behavior: " + path)
            if name == "fcmp":
                if text.count("signed char rflg = 1;") != 1:
                    raise RuntimeError("direction mutation anchor differs")
                damaged.write_text(text.replace("signed char rflg = 1;", "char rflg = 1;"))
                fixture.write_text(prefix + '#include "' + str(damaged) + '"\n' + suffix)
                run(command + ["-c", str(fixture), "-o", str(case / "unsigned.o")])
                run(
                    [
                        *driver,
                        *flags,
                        "-nostartfiles",
                        "-Wl,-Ttext=0x20000",
                        "-Wl,--gc-sections",
                        str(case / "unsigned.o"),
                        str(provider),
                        str(support),
                        "-lc",
                        "-lgcc",
                        "-o",
                        str(case / "unsigned"),
                    ]
                )
                result = subprocess.run(
                    ["qemu-arm", "-cpu", "cortex-m0", str(case / "unsigned")],
                    capture_output=True,
                    text=True,
                    timeout=90,
                )
                if result.returncode != 2 or result.stdout or result.stderr:
                    raise RuntimeError("unsigned direction control misses reverse rejection")
                print("qsort caller unsigned-direction control: reverse rejects")
