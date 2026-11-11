"""Verify bounded, streaming, retained logs for firmware builds and gates."""

import re
import sys
from pathlib import Path

EXPECTED_JOBS = {"rp2040", "stm32", "posix-sh", "rp2040-macos", "flash-id"}
EXPECTED_LABELS = {
    "linux-board-build",
    "linux-build",
    "linux-changed-comments",
    "linux-changed-text",
    "linux-clean-residue",
    "linux-cross",
    "linux-core1-emulation",
    "linux-flash",
    "linux-host",
    "linux-kernel-headroom",
    "linux-lint",
    "linux-qemu",
    "linux-renode",
    "linux-renode-picocalc",
    "linux-renode-union",
    "linux-stm32-build",
    "linux-uf2",
    "macos-board-build",
    "macos-build",
    "macos-cross",
    "macos-flash",
    "macos-host",
    "macos-kernel-headroom",
    "macos-lint",
    "macos-smaller-c",
    "macos-uf2",
    "posix-dcbc-ilp32",
    "posix-ilp32",
    "posix-inventory",
    "sdk-flash-id",
}
EXPECTED_COMMANDS = {
    "linux-board-build": 'bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 check-board-build',
    "linux-build": 'bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 build',
    "linux-clean-residue": "bmake MACHINE=rp2040 check-clean-residue",
    "linux-changed-comments": (
        'bmake MACHINE=rp2040 CHANGED_COMMENT_BASE="$CHANGED_COMMENT_BASE" '
        'CHANGED_COMMENT_HEAD="$CHANGED_COMMENT_HEAD" check-changed-comments'
    ),
    "linux-changed-text": (
        'bmake MACHINE=rp2040 CHANGED_COMMENT_BASE="$CHANGED_COMMENT_BASE" '
        'CHANGED_COMMENT_HEAD="$CHANGED_COMMENT_HEAD" check-changed-text'
    ),
    "linux-cross": 'bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 check-cross',
    "linux-flash": 'bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 flash',
    "linux-host": 'bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 check-host',
    "linux-kernel-headroom": "bmake MACHINE=rp2040 check-kernel-headroom",
    "linux-lint": "bmake MACHINE=rp2040 check-lint",
    "linux-qemu": "bmake MACHINE=rp2040 check-qemu",
    "linux-core1-emulation": "bmake MACHINE=rp2040 check-core1-emulation",
    "linux-renode": "bmake MACHINE=rp2040 check-renode",
    "linux-renode-picocalc": "bmake MACHINE=rp2040 check-renode-picocalc",
    "linux-renode-union": "bmake MACHINE=rp2040 check-renode-union",
    "linux-stm32-build": 'bmake -j"$(sh tools/online-cpus.sh)" MACHINE=stm32 build',
    "linux-uf2": "bmake MACHINE=rp2040 check-uf2-images",
    "macos-board-build": 'bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 check-board-build',
    "macos-build": 'bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 build',
    "macos-cross": 'bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 check-cross',
    "macos-flash": 'bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 flash',
    "macos-host": 'bmake -j"$(sh tools/online-cpus.sh)" MACHINE=rp2040 check-host',
    "macos-kernel-headroom": "bmake MACHINE=rp2040 check-kernel-headroom",
    "macos-lint": "bmake MACHINE=rp2040 check-lint",
    "macos-smaller-c": "bmake -C usr.bin/smlrc test",
    "macos-uf2": "bmake MACHINE=rp2040 check-uf2-images",
    "posix-ilp32": (
        "bmake MACHINE=rp2040 check-ilp32-execution "
        'TEST_EXECUTION_REPORT="$RUNNER_TEMP/ilp32-execution.json"'
    ),
    "posix-dcbc-ilp32": "bmake -C tests/dcbc check-ilp32 check-peak check-mutants REQUIRE_ILP32=1",
    "posix-inventory": "bmake MACHINE=rp2040 check-test-execution",
    "sdk-flash-id": "bmake MACHINE=rp2040 check-flash-id",
}
HOST_EXPECTED_JOBS = {"test", "wheel", "deb", "arch", "windows", "macos"}
HOST_REQUIRED_LOGS = {
    "test": ".build-logs/host-test.log",
    "wheel": ".build-logs/host-wheel.log",
    "deb": ".build-logs/host-deb.log",
    "arch": ".build-logs/host-arch.log",
    "windows": ".build-logs/host-windows.log",
    "macos": ".build-logs/host-macos.log",
}
JOB_PATTERN = re.compile(r"^  ([A-Za-z0-9_-]+):\s*$", re.MULTILINE)
STEP_PATTERN = re.compile(r"^      - (?:name|uses):", re.MULTILINE)
LABEL_PATTERN = re.compile(r"--label\s+([A-Za-z0-9._-]+)")
DIRECT_BMAKE_PATTERN = re.compile(
    r"^(?:env(?:\s+-[^\s]+)*\s+)?"
    r"(?:[A-Za-z_][A-Za-z0-9_]*=[^\s]+\s+)*(?:[^\s]*/)?bmake\b"
)


def split_blocks(source, pattern):
    matches = list(pattern.finditer(source))
    blocks = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(source)
        blocks.append(source[match.start():end])
    return blocks


def job_blocks(source):
    jobs_marker = re.search(r"^jobs:\s*$", source, re.MULTILINE)
    if jobs_marker is None:
        raise ValueError("firmware workflow has no jobs mapping")
    body = source[jobs_marker.end():]
    blocks = split_blocks(body, JOB_PATTERN)
    return {JOB_PATTERN.match(block).group(1): block for block in blocks}


def step_blocks(job):
    return split_blocks(job, STEP_PATTERN)


def step_name(step):
    match = re.match(r"^      - name:\s*(.+?)\s*$", step)
    return match.group(1) if match is not None else "unnamed step"


def active_step_source(step):
    return "\n".join(
        line for line in step.splitlines() if not line.lstrip().startswith("#")
    )


def has_one_positive_deadline(step, option):
    matches = re.findall(
        rf"--{re.escape(option)}(?:\s+|=)([^\s\\]+)", step
    )
    if len(matches) != 1:
        return False
    return re.fullmatch(
        r"(?:[1-9][0-9]*(?:\.[0-9]+)?|0\.[0-9]*[1-9][0-9]*)",
        matches[0],
    ) is not None


def normalized_command(text):
    return " ".join(text.replace("\\\n", " ").split())


def validate_workflow(source, *, expected_jobs=EXPECTED_JOBS,
                      expected_labels=EXPECTED_LABELS, expected_commands=None):
    if expected_commands is None:
        expected_commands = (
            EXPECTED_COMMANDS if expected_labels == EXPECTED_LABELS else {}
        )
    jobs = job_blocks(source)
    missing_jobs = sorted(expected_jobs - jobs.keys())
    if missing_jobs:
        raise ValueError(f"firmware workflow lacks jobs: {', '.join(missing_jobs)}")

    observed_labels = set()
    runner_jobs = set()
    for job_name, job in jobs.items():
        if job_name in expected_jobs and re.search(
                r"^    timeout-minutes:\s*[1-9][0-9]*\s*$", job, re.MULTILINE
        ) is None:
            raise ValueError(f"job {job_name} lacks a positive timeout-minutes")

        for step in step_blocks(job):
            name = step_name(step)
            active_step = active_step_source(step)
            continued_command = False
            for line in active_step.splitlines():
                command = line.strip()
                if command.startswith("run: "):
                    command = command.removeprefix("run: ")
                if (not continued_command and DIRECT_BMAKE_PATTERN.match(command)
                        and re.match(r"^bmake\s+-V\b", command) is None):
                    raise ValueError(
                        f"job {job_name} step {name} has a direct bmake invocation"
                    )
                continued_command = command.endswith("\\")

            if "tools/run_logged_gate.py" not in active_step:
                continue
            runner_jobs.add(job_name)
            if '"$PYTHON" tools/run_logged_gate.py' not in active_step:
                raise ValueError(f"job {job_name} step {name} does not use $PYTHON")
            label_match = LABEL_PATTERN.search(active_step)
            if label_match is None:
                raise ValueError(f"job {job_name} step {name} lacks a runner label")
            label = label_match.group(1)
            if label in observed_labels:
                raise ValueError(f"runner label {label} appears more than once")
            observed_labels.add(label)
            if label in expected_commands:
                normalized = normalized_command(active_step)
                command_match = re.search(r"(?:^|\s)--\s+(.+)$", normalized)
                observed_command = (
                    command_match.group(1) if command_match is not None else ""
                )
                if observed_command != expected_commands[label]:
                    raise ValueError(
                        f"runner {label} command differs: {observed_command or '-'}"
                    )
            if re.search(
                    r"^        timeout-minutes:\s*[1-9][0-9]*\s*$",
                    step,
                    re.MULTILINE,
            ) is None:
                raise ValueError(f"runner {label} lacks a positive step timeout-minutes")
            if not has_one_positive_deadline(active_step, "idle-timeout-seconds"):
                raise ValueError(f"runner {label} lacks a positive idle deadline")
            if not has_one_positive_deadline(active_step, "timeout-seconds"):
                raise ValueError(f"runner {label} lacks a positive hard deadline")
            if f"--log .build-logs/{label}.log" not in active_step:
                raise ValueError(f"runner {label} lacks its distinct log path")
            if f"--status .build-logs/{label}.status.json" not in active_step:
                raise ValueError(f"runner {label} lacks its distinct status path")

    missing_labels = sorted(expected_labels - observed_labels)
    extra_labels = sorted(observed_labels - expected_labels)
    if missing_labels or extra_labels:
        raise ValueError(
            "runner labels differ: "
            f"missing={','.join(missing_labels) or '-'} "
            f"extra={','.join(extra_labels) or '-'}"
        )

    for job_name in runner_jobs:
        uploads = [
            step for step in step_blocks(jobs[job_name])
            if "actions/upload-artifact@" in step and ".build-logs/" in step
        ]
        if len(uploads) != 1:
            raise ValueError(f"job {job_name} needs one build-log artifact upload")
        if re.search(r"^        if:\s*always\(\)\s*$", uploads[0], re.MULTILINE) is None:
            raise ValueError(f"job {job_name} build logs are not retained with always()")
        if re.search(
                r"^          include-hidden-files:\s*true\s*$",
                uploads[0],
                re.MULTILINE,
        ) is None:
            raise ValueError(f"job {job_name} hidden build logs are not included")


def validate_makefile(source):
    host_gates = re.search(
        r"^HOST_GATES=(.*?)(?=^[A-Z][A-Z0-9_]*=|\Z)",
        source,
        re.MULTILINE | re.DOTALL,
    )
    if host_gates is None or "check-run-logging" not in host_gates.group(1).split():
        raise ValueError("check-run-logging is absent from HOST_GATES")
    if "check-compiler-matrix-harness" not in host_gates.group(1).split():
        raise ValueError("check-compiler-matrix-harness is absent from HOST_GATES")
    matrix_target = re.search(
        r"^check-compiler-matrix-harness:\s*check-python\s*$\n((?:\t.*\n?)+)",
        source,
        re.MULTILINE,
    )
    if (matrix_target is None or
            normalized_command(matrix_target.group(1)) !=
            "${PYTHON} tests/compiler_matrix/check.py"):
        raise ValueError("check-compiler-matrix-harness recipe differs")
    if "check-callgraph-capture" not in host_gates.group(1).split():
        raise ValueError("check-callgraph-capture is absent from HOST_GATES")
    graph_target = re.search(
        r"^check-callgraph-capture:\s*check-python\s*$\n((?:\t.*\n?)+)",
        source,
        re.MULTILINE,
    )
    if (graph_target is None or
            normalized_command(graph_target.group(1)) != "${PYTHON} tests/callgraph/check.py"):
        raise ValueError("check-callgraph-capture recipe differs")
    if "check-rp2040-storage-delta" not in host_gates.group(1).split():
        raise ValueError("check-rp2040-storage-delta is absent from HOST_GATES")
    storage_target = re.search(
        r"^check-rp2040-storage-delta:\s*check-python\s*$\n((?:\t.*\n?)+)",
        source,
        re.MULTILINE,
    )
    if (storage_target is None or normalized_command(storage_target.group(1)) !=
            "${PYTHON} tests/rp2040/storage_delta_test.py"):
        raise ValueError("check-rp2040-storage-delta recipe differs")
    cross = re.search(r"^check-cross:((?:[^\n]*\\\n)*[^\n]*)", source, re.MULTILINE)
    if cross is None or "check-cross-kernel" not in cross.group(1).replace("\\\n", " ").split():
        raise ValueError("check-rp2040-execsw is unreachable from check-cross")
    if "check-cross-contracts" not in cross.group(1).replace("\\\n", " ").split():
        raise ValueError("check-compiler-object-attribution is unreachable from check-cross")
    contract_gates = re.search(r"^CROSS_CONTRACT_GATES=((?:[^\n]*\\\n)*[^\n]*)",
                               source, re.MULTILINE)
    if (contract_gates is None or "check-compiler-object-attribution" not in
            contract_gates.group(1).replace("\\\n", " ").split() or
            re.search(r"^check-cross-contracts:\s*\$\{CROSS_CONTRACT_GATES\}\s*$",
                      source, re.MULTILINE) is None):
        raise ValueError("check-compiler-object-attribution is absent from cross contracts")
    object_target = re.search(
        r"^check-compiler-object-attribution:\s*check-python\s*$\n((?:\t.*\n?)+)",
        source, re.MULTILINE,
    )
    object_command = (
        "${PYTHON} tests/compiler_matrix/object_attribution.py "
        "--gcc ${ARM_GCC_PREFIX:U${GCCPREFIX:U${BOARDLIBC_GCCPREFIX}}:Q}-gcc "
        "--objdump ${ARM_GCC_PREFIX:U${GCCPREFIX:U${BOARDLIBC_GCCPREFIX}}:Q}-objdump"
    )
    if object_target is None or normalized_command(object_target.group(1)) != object_command:
        raise ValueError("check-compiler-object-attribution recipe differs")
    cross_kernel = re.search(r"^check-cross-kernel:((?:[^\n]*\\\n)*[^\n]*)",
                             source, re.MULTILINE)
    if (cross_kernel is None or
            "check-rp2040-execsw" not in cross_kernel.group(1).replace("\\\n", " ").split()):
        raise ValueError("check-rp2040-execsw is absent from check-cross-kernel")
    execsw_recipe = (
        "check-rp2040-execsw: check-python\n"
        "\t${PYTHON} tests/rp2040_execsw/check.py --kernel "
        "sys/arch/rp2040/compile/PICO/unix.elf\n"
        ".for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}\n"
        "\t${PYTHON} tools/check_rp2040_execsw.py "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf\n"
        ".endfor"
    )
    normalized_source = re.sub(r"^\t+", "\t", source, flags=re.MULTILINE)
    if re.search(r"^" + re.escape(execsw_recipe) + r"$", normalized_source, re.MULTILINE) is None:
        raise ValueError("check-rp2040-execsw recipe differs")
    if "check-rp2040-vectors" not in cross_kernel.group(1).replace("\\\n", " ").split():
        raise ValueError("check-rp2040-vectors is absent from check-cross-kernel")
    vectors_recipe = (
        "check-rp2040-vectors: check-python\n"
        "\t${PYTHON} tests/rp2040_vectors/check.py --kernel "
        "sys/arch/rp2040/compile/PICO/unix.elf\n"
        ".for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}\n"
        "\t${PYTHON} tools/check_rp2040_vectors.py "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf\n"
        ".endfor"
    )
    if re.search(r"^" + re.escape(vectors_recipe) + r"$", normalized_source, re.MULTILINE) is None:
        raise ValueError("check-rp2040-vectors recipe differs")
    if "check-rp2040-syscalls" not in cross_kernel.group(1).replace("\\\n", " ").split():
        raise ValueError("check-rp2040-syscalls is absent from check-cross-kernel")
    syscalls_recipe = (
        "check-rp2040-syscalls: check-python\n"
        "\t${PYTHON} tests/rp2040_syscalls/check.py --kernel "
        "sys/arch/rp2040/compile/PICO/unix.elf\n"
        ".for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}\n"
        "\t${PYTHON} tools/check_rp2040_syscalls.py --master sys/kern/syscalls.master "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf\n"
        ".endfor"
    )
    if re.search(r"^" + re.escape(syscalls_recipe) + r"$", normalized_source, re.MULTILINE) is None:
        raise ValueError("check-rp2040-syscalls recipe differs")
    if "check-rp2040-devsw" not in cross_kernel.group(1).replace("\\\n", " ").split():
        raise ValueError("check-rp2040-devsw is absent from check-cross-kernel")
    devsw_recipe = (
        "check-rp2040-devsw: check-python\n"
        ".for KERNEL_CONFIGURATION in PICO PICOCALC_PSRAM\n"
        "\t${PYTHON} tests/rp2040_devsw/check.py --kernel "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf --object "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/conf.o\n"
        ".endfor\n"
        ".for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}\n"
        "\t${PYTHON} tools/check_rp2040_devsw.py --object "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/conf.o "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf\n"
        ".endfor"
    )
    if re.search(r"^" + re.escape(devsw_recipe) + r"$", normalized_source, re.MULTILINE) is None:
        raise ValueError("check-rp2040-devsw recipe differs")
    if "check-rp2040-fileops" not in cross_kernel.group(1).replace("\\\n", " ").split():
        raise ValueError("check-rp2040-fileops is absent from check-cross-kernel")
    fileops_recipe = (
        "check-rp2040-fileops: check-python\n"
        "\t${PYTHON} tests/rp2040_fileops/check.py --kernel "
        "sys/arch/rp2040/compile/PICO/unix.elf\n"
        ".for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}\n"
        "\t${PYTHON} tools/check_rp2040_fileops.py "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf\n"
        ".endfor"
    )
    if re.search(r"^" + re.escape(fileops_recipe) + r"$", normalized_source, re.MULTILINE) is None:
        raise ValueError("check-rp2040-fileops recipe differs")
    if "check-rp2040-ioconf" not in cross_kernel.group(1).replace("\\\n", " ").split():
        raise ValueError("check-rp2040-ioconf is absent from check-cross-kernel")
    ioconf_recipe = (
        "check-rp2040-ioconf: check-python\n"
        ".for KERNEL_CONFIGURATION in PICO PICOCALC_PSRAM\n"
        "\t${PYTHON} tests/rp2040_ioconf/check.py --kernel "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf --object "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/ioconf.o\n"
        ".endfor\n"
        ".for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}\n"
        "\t${PYTHON} tools/check_rp2040_ioconf.py --object "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/ioconf.o "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf\n"
        ".endfor"
    )
    if re.search(r"^" + re.escape(ioconf_recipe) + r"$", normalized_source, re.MULTILINE) is None:
        raise ValueError("check-rp2040-ioconf recipe differs")
    if "check-rp2040-tty-roots" not in cross_kernel.group(1).replace("\\\n", " ").split():
        raise ValueError("check-rp2040-tty-roots is absent from check-cross-kernel")
    tty_recipe = (
        "check-rp2040-tty-roots: check-python\n"
        "\t${PYTHON} tests/rp2040_tty_roots/check.py --build-dir "
        "sys/arch/rp2040/compile/PICOCALC_PSRAM\n"
        ".for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}\n"
        "\t${PYTHON} tools/check_rp2040_tty_roots.py --build-dir "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}\n"
        ".endfor"
    )
    if re.search(r"^" + re.escape(tty_recipe) + r"$", normalized_source, re.MULTILINE) is None:
        raise ValueError("check-rp2040-tty-roots recipe differs")
    if "check-rp2040-callout-roots" not in cross_kernel.group(1).replace("\\\n", " ").split():
        raise ValueError("check-rp2040-callout-roots is absent from check-cross-kernel")
    callout_recipe = (
        "check-rp2040-callout-roots: check-python\n"
        "\t${PYTHON} tests/rp2040_callout_roots/check.py --build-dir "
        "sys/arch/rp2040/compile/PICOCALC_PSRAM\n"
        ".for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}\n"
        "\t${PYTHON} tools/check_rp2040_callout_roots.py --build-dir "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}\n"
        ".endfor"
    )
    if re.search(r"^" + re.escape(callout_recipe) + r"$", normalized_source, re.MULTILINE) is None:
        raise ValueError("check-rp2040-callout-roots recipe differs")
    if "check-rp2040-nlist" not in cross_kernel.group(1).replace("\\\n", " ").split():
        raise ValueError("check-rp2040-nlist is absent from check-cross-kernel")
    nlist_recipe = (
        "check-rp2040-nlist: check-python\n"
        "\t${PYTHON} tests/rp2040_nlist/check.py --object "
        "sys/arch/rp2040/compile/PICO/sysctl.o --kernel sys/arch/rp2040/compile/PICO/unix.elf\n"
        ".for KERNEL_CONFIGURATION in ${MACHINE_KERNEL_CONFIGS}\n"
        "\t${PYTHON} tools/check_rp2040_nlist.py --object "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/sysctl.o "
        "sys/arch/rp2040/compile/${KERNEL_CONFIGURATION:Q}/unix.elf\n"
        ".endfor"
    )
    if re.search(r"^" + re.escape(nlist_recipe) + r"$", normalized_source, re.MULTILINE) is None:
        raise ValueError("check-rp2040-nlist recipe differs")
    qemu_target = re.search(r"^check-qemu:[^\n]*\n((?:\t.*\n?)+)", source, re.MULTILINE)
    if (qemu_target is None or "${MAKE} check-exec-dispatch-qemu" not in
            [line.strip() for line in qemu_target.group(1).splitlines()]):
        raise ValueError("check-exec-dispatch-qemu is absent from check-qemu")
    dispatch_target = re.search(
        r"^check-exec-dispatch-qemu:\s*check-python\s*$\n((?:\t.*\n?)+)",
        source, re.MULTILINE,
    )
    dispatch_command = (
        "${PYTHON} tests/kernel/exec_dispatch_qemu.py "
        "--gcc ${ARM_GCC_PREFIX:U${GCCPREFIX:U${BOARDLIBC_GCCPREFIX}}:Q}-gcc "
        "--clang ${CLANG:Uclang:Q}"
    )
    if dispatch_target is None or normalized_command(dispatch_target.group(1)) != dispatch_command:
        raise ValueError("check-exec-dispatch-qemu recipe differs")
    target = re.search(
        r"^check-run-logging:\s*check-python\s*$\n((?:\t.*\n?)+)",
        source,
        re.MULTILINE,
    )
    if target is None:
        raise ValueError("check-run-logging lacks its check-python target")
    recipes = target.group(1)
    for script in (
        "run_logged_gate_test.py",
        "run_logged_build_check_test.py",
        "build_observability_test.py",
        "storage_counter_driver_test.py",
    ):
        command = f"${{PYTHON}} tools/{script}"
        if command not in recipes:
            raise ValueError(f"check-run-logging does not execute {command}")

    local_targets = {
        "logged-build": ("check-python", "rp2040-build-local", "build"),
        "logged-distribution": (
            "check-python", "rp2040-distribution-local", "distribution"
        ),
        "logged-flash": ("check-python", "rp2040-flash-local", "flash"),
        "logged-kernel": ("check-python", "rp2040-kernel-local", "kernel"),
        "logged-ilp32": (
            "check-python", "rp2040-ilp32-local", "check-ilp32-execution"
        ),
    }
    for target, (dependencies, label, command_target) in local_targets.items():
        target_match = re.search(
            rf"^{re.escape(target)}:\s*{re.escape(dependencies)}\s*$\n"
            r"((?:\t.*\n?)+)",
            source,
            re.MULTILINE,
        )
        if target_match is None:
            raise ValueError(f"{target} lacks dependencies {dependencies}")
        recipe = normalized_command(target_match.group(1))
        required = (
            f"${{PYTHON}} tools/run_logged_gate.py --label {label} "
            "--evidence-root .build-logs "
            "--execution-lock .build-logs/rp2040-products.lock "
            "--idle-timeout-seconds 600 --timeout-seconds 3600 "
            f"-- ${{MAKE}} MACHINE=rp2040 {command_target}"
        )
        if recipe != required:
            raise ValueError(f"{target} recipe differs")

    check_match = re.search(
        r"^logged-check:\s*check-python\s*$\n((?:\t.*\n?)+)",
        source,
        re.MULTILINE,
    )
    if check_match is None:
        raise ValueError("logged-check lacks dependency check-python")
    check_recipe = normalized_command(check_match.group(1))
    required_check = (
        "${PYTHON} tools/run_logged_build_check.py --label rp2040-check-local "
        "--evidence-root .build-logs "
        "--execution-lock .build-logs/rp2040-products.lock "
        "--idle-timeout-seconds 600 --timeout-seconds 5400 "
        "-- ${MAKE} MACHINE=rp2040"
    )
    if check_recipe != required_check:
        raise ValueError("logged-check recipe differs")


def validate_host_workflow(source):
    jobs = job_blocks(source)
    missing_jobs = sorted(HOST_EXPECTED_JOBS - jobs.keys())
    if missing_jobs:
        raise ValueError(
            f"host workflow lacks jobs: {', '.join(missing_jobs)}"
        )
    for job_name in HOST_EXPECTED_JOBS:
        job = jobs[job_name]
        if re.search(
                r"^    timeout-minutes:\s*[1-9][0-9]*\s*$", job, re.MULTILINE
        ) is None:
            raise ValueError(f"host workflow job {job_name} lacks a job deadline")
        for step in step_blocks(job):
            if re.search(r"^        run:", step, re.MULTILINE) is None:
                continue
            if re.search(
                    r"^        timeout-minutes:\s*[1-9][0-9]*\s*$",
                    step,
                    re.MULTILINE,
            ) is None:
                raise ValueError(
                    f"host workflow job {job_name} step {step_name(step)} "
                    "lacks a step deadline"
                )
            if re.search(r"(?:^|\s)\|\s*tee\b", step) is not None and re.search(
                    r"^        shell:\s*bash\s*$", step, re.MULTILINE
            ) is None:
                raise ValueError(
                    f"host workflow job {job_name} step {step_name(step)} "
                    "has a tee pipeline without an explicit bash pipefail shell"
                )
            if "Tee-Object" in step and (
                    re.search(r"^        shell:\s*pwsh\s*$", step, re.MULTILINE)
                    is None or
                    "if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }" not in step
            ):
                raise ValueError(
                    f"host workflow job {job_name} step {step_name(step)} "
                    "uses Tee-Object without preserving LASTEXITCODE"
                )
        if HOST_REQUIRED_LOGS[job_name] not in job:
            raise ValueError(
                f"host workflow job {job_name} lacks retained decision log "
                f"{HOST_REQUIRED_LOGS[job_name]}"
            )
        uploads = [
            step for step in step_blocks(job)
            if "actions/upload-artifact@" in step and ".build-logs/" in step
        ]
        if len(uploads) != 1:
            raise ValueError(
                f"host workflow job {job_name} needs one build-log artifact upload"
            )
        if re.search(
                r"^        if:\s*always\(\)\s*$", uploads[0], re.MULTILINE
        ) is None:
            raise ValueError(
                f"host workflow job {job_name} logs are not retained with always()"
            )
        if re.search(
                r"^          include-hidden-files:\s*true\s*$",
                uploads[0],
                re.MULTILINE,
        ) is None:
            raise ValueError(
                f"host workflow job {job_name} hidden logs are not included"
            )


def check(root):
    workflow = root / ".github/workflows/firmware.yml"
    host_workflow = root / ".github/workflows/host.yml"
    makefile = root / "Makefile"
    validate_workflow(workflow.read_text(encoding="utf-8"))
    validate_host_workflow(host_workflow.read_text(encoding="utf-8"))
    validate_makefile(makefile.read_text(encoding="utf-8"))
    print(
        "build observability: firmware jobs have bounded streaming gates; "
        "host jobs have bounded steps and retained decision logs"
    )


def main():
    try:
        check(Path(__file__).resolve().parent.parent)
    except (OSError, ValueError) as error:
        print(f"build observability: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
