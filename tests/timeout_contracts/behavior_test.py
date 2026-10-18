#!/usr/bin/env python3
"""Exercise the selected DPUCP-2024 timeout utility contract."""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

COMPILE_FLAGS = (
    "-std=c17",
    "-D_POSIX_C_SOURCE=200809L",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wformat=2",
)

MUTATIONS = (
    (
        "timeout-status",
        "#define EXIT_TIMEOUT 124",
        "#define EXIT_TIMEOUT 123",
    ),
    (
        "child-only-default",
        "destination = foreground ? child_pid : -getpid();",
        "(void)foreground;\n\tdestination = child_pid;",
    ),
    (
        "not-found-status",
        "_exit(exec_error == ENOENT ? EXIT_NOT_FOUND : EXIT_CANNOT_INVOKE);",
        "(void)exec_error;\n\t\t_exit(EXIT_CANNOT_INVOKE);",
    ),
    (
        "missing-sigcont",
        "if (send_to_utility(child_pid, foreground, SIGCONT) < 0)",
        "if (((void)child_pid, (void)foreground, 0))",
    ),
    (
        "encoded-signal-status",
        "return mimic_signal(WTERMSIG(status));",
        "return ((void)&mimic_signal, 128 + WTERMSIG(status));",
    ),
    (
        "uncatchable-signal-status",
        "if (signal_number != SIGKILL && signal_number != SIGSTOP) {",
        "if (1) {",
    ),
    (
        "mimic-sigaction-flags",
        "action.sa_handler = SIG_DFL;\n\t\taction.sa_flags = 0;",
        "action.sa_handler = SIG_DFL;\n\t\taction.sa_flags = 1;",
    ),
    (
        "inherited-timeout-disposition",
        "if (sigaction(timeout_signal, &action, NULL) < 0)",
        "if (0)",
    ),
    (
        "inherited-alarm-mask",
        "if (sigdelset(parent_mask, terminating_signals[index]) < 0)",
        "if (terminating_signals[index] != SIGALRM &&\n"
        "\t\t    sigdelset(parent_mask, terminating_signals[index]) < 0)",
    ),
    (
        "supervisor-group-leader",
        "if (!foreground && setpgid(0, 0) < 0)",
        "if (!foreground && setpgid(0, getpgrp()) < 0)",
    ),
    (
        "postfork-sigchld-handler",
        "if (install_handler(SIGCHLD, signal_handler, &inherited_sigchld) < 0)",
        "if (((void)&install_handler, sigaction(SIGCHLD, NULL, "
        "&inherited_sigchld)) < 0)",
    ),
    (
        "self-signal-ignore",
        "if (sigaction(signal_number, &ignored_action, &saved_action) < 0)",
        "if (sigaction(signal_number, NULL, &saved_action) < 0)",
    ),
    (
        "self-signal-drain",
        "sigprocmask(SIG_UNBLOCK, &signal_mask, NULL) < 0 ||",
        "0 ||",
    ),
    (
        "external-signal-deadline",
        "if (escalation_armed) {",
        "if (escalation_armed || first_signal_sent) {",
    ),
    (
        "zero-kill-after",
        "if (!have_kill_after || !kill_after->nonzero)\n\t\treturn 0;",
        "if (!have_kill_after)\n\t\treturn 0;\n"
        "\tif (!kill_after->nonzero)\n"
        "\t\treturn send_to_utility(child_pid, foreground, SIGKILL);",
    ),
)


class ContractFailure(RuntimeError):
    pass


def require(condition: bool, description: str) -> None:
    if not condition:
        raise ContractFailure(description)


def compile_program(
    compiler: str,
    output: Path,
    sources: list[Path],
    extra_flags: tuple[str, ...] = (),
) -> None:
    result = subprocess.run(
        [
            compiler,
            *COMPILE_FLAGS,
            *extra_flags,
            *(str(source) for source in sources),
            "-o",
            str(output),
        ],
        capture_output=True,
        check=False,
    )
    require(
        result.returncode == 0,
        "compile failed:\n" + result.stderr.decode(errors="replace"),
    )


def write_fork_interposer(path: Path) -> None:
    path.write_text(
        """#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <unistd.h>

#undef fork
extern pid_t fork(void);

pid_t
timeout_test_fork(void)
{
    siginfo_t child_info;
    pid_t child_pid;

    child_pid = fork();
    if (child_pid > 0) {
        while (waitid(P_PID, (id_t)child_pid, &child_info,
            WEXITED | WNOWAIT) < 0 && errno == EINTR)
            ;
    }
    return child_pid;
}
""",
        encoding="utf-8",
    )


def write_sigaction_interposer(path: Path) -> None:
    path.write_text(
        """#define _POSIX_C_SOURCE 200809L
#include <signal.h>

#include <errno.h>

extern int __real_sigaction(int, const struct sigaction *, struct sigaction *);

int
__wrap_sigaction(int signal_number, const struct sigaction *action,
    struct sigaction *inherited_action)
{
    if (action != 0 && action->sa_flags != 0) {
        errno = EINVAL;
        return -1;
    }
    return __real_sigaction(signal_number, action, inherited_action);
}
""",
        encoding="utf-8",
    )


def exercise_inherited_ignored_sigchld(
    timeout_binary: Path, probe_binary: Path
) -> None:
    result = run(
        [
            str(probe_binary),
            "ignore-chld-exec",
            str(timeout_binary),
            "1",
            str(probe_binary),
            "exit",
            "37",
        ]
    )
    require(result.returncode == 37, "inherited ignored SIGCHLD loses child status")


def compile_mutation(
    compiler: str,
    production_source: Path,
    build_directory: Path,
    name: str,
    original: str,
    replacement: str,
    fork_interposer: Path,
    sigaction_interposer: Path,
) -> Path:
    source_text = production_source.read_text(encoding="utf-8")
    require(source_text.count(original) == 1, f"mutation {name!r} has a stale source anchor")
    mutated_source = build_directory / f"timeout-{name}.c"
    mutated_binary = build_directory / f"timeout-{name}"
    mutated_source.write_text(source_text.replace(original, replacement), encoding="utf-8")
    if name == "postfork-sigchld-handler":
        compile_program(
            compiler,
            mutated_binary,
            [mutated_source, fork_interposer],
            ("-Dfork=timeout_test_fork",),
        )
    elif name == "mimic-sigaction-flags":
        compile_program(
            compiler,
            mutated_binary,
            [mutated_source, sigaction_interposer],
            ("-Wl,--wrap=sigaction",),
        )
    else:
        compile_program(compiler, mutated_binary, [mutated_source])
    return mutated_binary


def run(arguments: list[str], timeout_seconds: float = 4.0) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        arguments,
        capture_output=True,
        timeout=timeout_seconds,
        check=False,
    )


def wait_for_bytes(path: Path, expected: bytes, process: subprocess.Popen[bytes]) -> None:
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        if path.exists() and path.read_bytes() == expected:
            return
        if process.poll() is not None:
            break
        time.sleep(0.01)
    raise ContractFailure(f"child readiness marker {expected!r} was not observed")


def wait_for_pid(path: Path, process: subprocess.Popen[bytes]) -> int:
    pid_path = Path(str(path) + ".pid")
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        if pid_path.exists():
            return int(pid_path.read_text(encoding="ascii"))
        if process.poll() is not None:
            break
        time.sleep(0.01)
    raise ContractFailure(f"child process identifier {pid_path} was not observed")


def wait_for_marker_bytes(path: Path, expected: bytes) -> bytes:
    deadline = time.monotonic() + 1.0
    observed = b""
    while time.monotonic() < deadline:
        if path.exists():
            observed = path.read_bytes()
            if sorted(observed) == sorted(expected):
                return observed
        time.sleep(0.01)
    return observed


def kill_process(process_id: int) -> None:
    try:
        os.kill(process_id, signal.SIGKILL)
    except ProcessLookupError:
        pass


def single_term_cont_delivery(delivered: bytes) -> bool:
    return (
        len(delivered) == 3
        and delivered[:1] == b"R"
        and sorted(delivered[1:]) == [ord("C"), ord("T")]
    )


def block_alarm_signal() -> None:
    signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGALRM})


def require_process_completion(
    process: subprocess.Popen[bytes], process_id: int, description: str
) -> int:
    try:
        return process.wait(timeout=1.0)
    except subprocess.TimeoutExpired as error:
        kill_process(process_id)
        process.kill()
        process.wait()
        raise ContractFailure(description) from error


def exercise_external_signal_deadline(
    timeout_binary: Path, probe_binary: Path, marker: Path
) -> None:
    inherited_usr1 = signal.getsignal(signal.SIGUSR1)
    signal.signal(signal.SIGUSR1, signal.SIG_IGN)
    try:
        process = subprocess.Popen(
            [
                str(timeout_binary),
                "0.10",
                str(probe_binary),
                "pause-pid",
                str(marker),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    finally:
        signal.signal(signal.SIGUSR1, inherited_usr1)
    child_pid = wait_for_pid(marker, process)
    os.kill(process.pid, signal.SIGUSR1)
    status = require_process_completion(
        process,
        child_pid,
        "forwarded external signal disables the timeout deadline",
    )
    require(status == 124, "deadline after external signal does not return 124")


def exercise_zero_kill_after(
    timeout_binary: Path, probe_binary: Path, marker: Path
) -> None:
    result = run(
        [
            str(timeout_binary),
            "-p",
            "-k",
            "0",
            "0.05",
            str(probe_binary),
            "count-term",
            str(marker),
        ],
        timeout_seconds=1.0,
    )
    require(result.returncode == 0, "-k 0 does not disable SIGKILL escalation")
    delivered = marker.read_bytes()
    require(
        single_term_cont_delivery(delivered),
        "-k 0 changes timeout signal delivery: " f"{delivered!r}",
    )


def exercise_mutation(
    name: str, timeout_binary: Path, probe_binary: Path, work_directory: Path
) -> None:
    work_directory.mkdir(exist_ok=True)
    if name == "timeout-status":
        result = run([str(timeout_binary), "0.05", str(probe_binary), "pause"])
        require(result.returncode == 124, "timeout status mutation escaped detection")
        return
    if name == "child-only-default":
        marker = work_directory / "tree"
        result = subprocess.run(
            [str(timeout_binary), "0.20", str(probe_binary), "tree", str(marker)],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=4.0,
            check=False,
        )
        descendant_pid = int(Path(str(marker) + ".pid").read_text(encoding="ascii"))
        kill_process(descendant_pid)
        require(result.returncode == 124, "child-only mutation changed timeout status")
        require(
            sorted(marker.read_bytes()) == [ord("C"), ord("P")],
            "child-only default mutation escaped detection",
        )
        return
    if name == "not-found-status":
        result = run([str(timeout_binary), "1", "missing-dpucp-timeout-command"])
        require(result.returncode == 127, "not-found status mutation escaped detection")
        return
    if name == "missing-sigcont":
        marker = work_directory / "stopped"
        process = subprocess.Popen(
            [str(timeout_binary), "0.05", str(probe_binary), "stop-pid", str(marker)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        child_pid = wait_for_pid(marker, process)
        status = require_process_completion(
            process, child_pid, "missing SIGCONT mutation escaped detection"
        )
        require(status == 124, "stopped-child mutation changed timeout status")
        return
    if name == "encoded-signal-status":
        result = run([str(timeout_binary), "1", str(probe_binary), "signal", "15"])
        require(result.returncode == -signal.SIGTERM, "signal encoding mutation escaped detection")
        return
    if name == "uncatchable-signal-status":
        result = run([str(timeout_binary), "1", str(probe_binary), "signal", "9"])
        require(result.returncode == -signal.SIGKILL, "SIGKILL status mutation escaped detection")
        return
    if name == "mimic-sigaction-flags":
        result = run([str(timeout_binary), "1", str(probe_binary), "signal", "15"])
        require(
            result.returncode == -signal.SIGTERM,
            "mimic sigaction-flags mutation escaped detection",
        )
        return
    if name == "inherited-timeout-disposition":
        marker = work_directory / "ignored"
        inherited_term = signal.getsignal(signal.SIGTERM)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        try:
            process = subprocess.Popen(
                [str(timeout_binary), "0.05", str(probe_binary), "pause-pid", str(marker)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        finally:
            signal.signal(signal.SIGTERM, inherited_term)
        child_pid = wait_for_pid(marker, process)
        status = require_process_completion(
            process, child_pid, "inherited timeout disposition mutation escaped detection"
        )
        require(status == 124, "timeout disposition mutation changed timeout status")
        return
    if name == "inherited-alarm-mask":
        marker = work_directory / "blocked-alarm-mask"
        process = subprocess.Popen(
            [str(timeout_binary), "0.05", str(probe_binary), "pause-pid", str(marker)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            preexec_fn=block_alarm_signal,
        )
        child_pid = wait_for_pid(marker, process)
        status = require_process_completion(
            process, child_pid, "inherited alarm-mask mutation escaped detection"
        )
        require(status == 124, "alarm-mask mutation changed timeout status")
        return
    if name == "supervisor-group-leader":
        marker = work_directory / "group-identifiers"
        process = subprocess.Popen(
            [str(timeout_binary), "5", str(probe_binary), "group-ids", str(marker)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        deadline = time.monotonic() + 2.0
        identifiers: list[int] = []
        while time.monotonic() < deadline:
            if marker.exists():
                fields = marker.read_text(encoding="ascii").split()
                if len(fields) == 3:
                    identifiers = [int(value) for value in fields]
                    break
            if process.poll() is not None:
                break
            time.sleep(0.01)
        require(len(identifiers) == 3, "group-leader mutation omits the identity marker")
        child_pid, supervisor_pid, process_group = identifiers
        kill_process(child_pid)
        kill_process(supervisor_pid)
        process.wait(timeout=1.0)
        require(child_pid != process_group, "mutated child unexpectedly leads the group")
        require(
            supervisor_pid == process_group,
            "supervisor group-leader mutation escaped detection",
        )
        return
    if name == "postfork-sigchld-handler":
        exercise_inherited_ignored_sigchld(timeout_binary, probe_binary)
        return
    if name in {"self-signal-ignore", "self-signal-drain"}:
        marker = work_directory / "term-count"
        result = run(
            [
                str(timeout_binary),
                "0.20",
                str(probe_binary),
                "count-term",
                str(marker),
            ],
            timeout_seconds=1.0,
        )
        require(result.returncode == 124, f"{name} mutation changed timeout status")
        delivered = marker.read_bytes()
        require(
            single_term_cont_delivery(delivered),
            f"{name} mutation escaped the exact-delivery count: {delivered!r}",
        )
        return
    if name == "external-signal-deadline":
        exercise_external_signal_deadline(
            timeout_binary, probe_binary, work_directory / "external-deadline"
        )
        return
    if name == "zero-kill-after":
        exercise_zero_kill_after(
            timeout_binary, probe_binary, work_directory / "zero-kill-after"
        )
        return
    raise ContractFailure(f"unknown mutation {name!r}")


def calibrate_mutations(
    compiler: str,
    production_source: Path,
    probe_binary: Path,
    build_directory: Path,
    fork_interposer: Path,
    sigaction_interposer: Path,
) -> None:
    for name, original, replacement in MUTATIONS:
        mutated_binary = compile_mutation(
            compiler,
            production_source,
            build_directory,
            name,
            original,
            replacement,
            fork_interposer,
            sigaction_interposer,
        )
        try:
            exercise_mutation(
                name, mutated_binary, probe_binary, build_directory / f"mutation-{name}"
            )
        except ContractFailure:
            continue
        raise ContractFailure(f"mutation {name!r} passed its deciding behavior")
    print(f"timeout contract calibration: {len(MUTATIONS)} mutations rejected")


def exercise(timeout_binary: Path, probe_binary: Path, work_directory: Path) -> None:
    work_directory.mkdir(exist_ok=True)
    result = run([str(timeout_binary)])
    require(result.returncode == 125, "missing operands do not return 125")
    require(result.stderr, "missing operands omit the diagnostic")

    result = run([str(timeout_binary), "bad", str(probe_binary), "exit", "0"])
    require(result.returncode == 125, "invalid duration does not return 125")

    result = run([str(timeout_binary), "-s", "15", "1", str(probe_binary), "exit", "0"])
    require(
        result.returncode == 125,
        "numeric signal extension is admitted into the selected profile",
    )

    result = run([str(timeout_binary), "0", str(probe_binary), "exit", "37"])
    require(result.returncode == 37, "zero duration does not preserve child exit status")

    result = run(
        [
            str(probe_binary),
            "ignore-chld-exec",
            str(timeout_binary),
            "1",
            str(probe_binary),
            "exit",
            "37",
        ]
    )
    require(result.returncode == 37, "inherited ignored SIGCHLD loses child status")

    result = run([str(timeout_binary), "1", "missing-dpucp-timeout-command"])
    require(result.returncode == 127, "missing utility does not return 127")

    blocked = work_directory / "blocked-command"
    blocked.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
    blocked.chmod(0o600)
    result = run([str(timeout_binary), "1", str(blocked)])
    require(result.returncode == 126, "non-invokable utility does not return 126")

    result = run([str(timeout_binary), "0.05", str(probe_binary), "pause"])
    require(result.returncode == 124, "expired default timeout does not return 124")

    for duration in ("0.000000001", "0.001m", "0.00001h", "0.000001d"):
        result = run([str(timeout_binary), duration, str(probe_binary), "pause"])
        require(result.returncode == 124, f"duration suffix {duration!r} is not enforced")

    result = run([str(timeout_binary), "-p", "0.05", str(probe_binary), "pause"])
    require(result.returncode == -signal.SIGTERM, "-p does not mimic SIGTERM wait status")

    result = run([str(timeout_binary), "1", str(probe_binary), "signal", "15"])
    require(result.returncode == -signal.SIGTERM, "child signal status is encoded as an exit code")

    result = run([str(timeout_binary), "1", str(probe_binary), "signal", "9"])
    require(result.returncode == -signal.SIGKILL, "child SIGKILL status is not propagated")

    result = run(
        [
            str(timeout_binary),
            "-p",
            "-s",
            "iNt",
            "0.05s",
            str(probe_binary),
            "pause",
        ]
    )
    require(
        result.returncode == -signal.SIGINT,
        "case-independent symbolic signal does not select SIGINT",
    )

    result = run(
        [str(timeout_binary), "-k", "0.05", "0.05", str(probe_binary), "ignore-term"]
    )
    require(
        result.returncode in (124, -signal.SIGKILL),
        "SIGKILL escalation returns neither Issue 8 permitted outcome",
    )

    result = run([str(timeout_binary), "0.05", str(probe_binary), "stop"])
    require(result.returncode == 124, "stopped child was not continued after timeout signal")

    count_marker = work_directory / "term-count"
    result = run(
        [
            str(timeout_binary),
            "0.20",
            str(probe_binary),
            "count-term",
            str(count_marker),
        ]
    )
    require(result.returncode == 124, "counted timeout signal does not return 124")
    counted_delivery = count_marker.read_bytes()
    require(
        single_term_cont_delivery(counted_delivery),
        "timeout signals were not delivered exactly once after readiness: "
        f"{counted_delivery!r}",
    )

    group_marker = work_directory / "group-identifiers"
    result = run(
        [str(timeout_binary), "0.20", str(probe_binary), "group-ids", str(group_marker)]
    )
    require(result.returncode == 124, "process-group identity probe does not time out")
    child_pid, supervisor_pid, process_group = (
        int(value) for value in group_marker.read_text(encoding="ascii").split()
    )
    require(child_pid != process_group, "child unexpectedly leads the timeout group")
    require(
        supervisor_pid == process_group,
        "timeout supervisor does not lead the signaled process group",
    )

    tree_marker = work_directory / "tree-default"
    result = run([str(timeout_binary), "0.20", str(probe_binary), "tree", str(tree_marker)])
    descendant_pid = int(Path(str(tree_marker) + ".pid").read_text(encoding="ascii"))
    delivered_markers = wait_for_marker_bytes(tree_marker, b"CP")
    kill_process(descendant_pid)
    require(result.returncode == 124, "process-group timeout does not return 124")
    require(
        sorted(delivered_markers) == [ord("C"), ord("P")],
        "default timeout does not signal the child process group",
    )

    foreground_marker = work_directory / "tree-foreground"
    result = subprocess.run(
        [str(timeout_binary), "-f", "0.20", str(probe_binary), "tree", str(foreground_marker)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        timeout=4.0,
        check=False,
    )
    require(result.returncode == 124, "-f timeout does not return 124")
    require(foreground_marker.read_bytes() == b"P", "-f signals a descendant")
    orphan_pid = int(Path(str(foreground_marker) + ".pid").read_text(encoding="ascii"))
    try:
        os.kill(orphan_pid, signal.SIGKILL)
    except ProcessLookupError:
        pass

    inherited_action = signal.getsignal(signal.SIGUSR1)
    signal.signal(signal.SIGUSR1, signal.SIG_IGN)
    try:
        result = run([str(timeout_binary), "1", str(probe_binary), "raise-usr1"])
    finally:
        signal.signal(signal.SIGUSR1, inherited_action)
    require(result.returncode == 33, "child does not inherit the original ignored disposition")

    inherited_term = signal.getsignal(signal.SIGTERM)
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    try:
        result = run([str(timeout_binary), "0.05", str(probe_binary), "pause"])
    finally:
        signal.signal(signal.SIGTERM, inherited_term)
    require(result.returncode == 124, "selected timeout signal is not reset in the child")

    blocked_mask_marker = work_directory / "blocked-alarm-mask"
    process = subprocess.Popen(
        [
            str(timeout_binary),
            "0.05",
            str(probe_binary),
            "pause-pid",
            str(blocked_mask_marker),
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        preexec_fn=block_alarm_signal,
    )
    blocked_mask_child = wait_for_pid(blocked_mask_marker, process)
    status = require_process_completion(
        process,
        blocked_mask_child,
        "inherited blocked SIGALRM prevents timeout delivery",
    )
    require(status == 124, "blocked-mask timeout status differs")

    exercise_external_signal_deadline(
        timeout_binary,
        probe_binary,
        work_directory / "external-signal-deadline",
    )
    exercise_zero_kill_after(
        timeout_binary,
        probe_binary,
        work_directory / "zero-kill-after",
    )

    forwarding_marker = work_directory / "forwarded-signal"
    process = subprocess.Popen(
        [str(timeout_binary), "5", str(probe_binary), "record-int", str(forwarding_marker)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        wait_for_bytes(forwarding_marker, b"R", process)
        os.kill(process.pid, signal.SIGINT)
        stdout, stderr = process.communicate(timeout=3.0)
    except BaseException:
        process.kill()
        process.wait()
        raise
    require(process.returncode == 42, "forwarded SIGINT child status differs")
    require(forwarding_marker.read_bytes() == b"RI", "SIGINT was not forwarded")
    require(stdout == b"", "timeout writes standard output")
    require(stderr == b"", "successful signal forwarding writes a diagnostic")


def main() -> int:
    require(len(sys.argv) == 3, "usage: behavior_test.py ROOT HOST_CC")
    source_root = Path(sys.argv[1]).resolve()
    compiler = sys.argv[2]
    production_source = source_root / "usr.bin" / "timeout" / "timeout.c"
    test_root = source_root / "tests" / "timeout_contracts"

    with tempfile.TemporaryDirectory(prefix="timeout-contract-") as temporary:
        build_directory = Path(temporary)
        bad_binary = build_directory / "known-bad"
        probe_binary = build_directory / "probe"
        compile_program(compiler, bad_binary, [test_root / "known_bad.c"])
        compile_program(compiler, probe_binary, [test_root / "probe.c"])
        try:
            exercise(bad_binary, probe_binary, build_directory / "known-bad-work")
        except (ContractFailure, subprocess.TimeoutExpired, FileNotFoundError):
            print("timeout contract calibration: known-bad executable rejected")
        else:
            raise ContractFailure("known-bad executable passed the timeout contract")

        require(production_source.is_file(), "timeout production source is absent")
        timeout_binary = build_directory / "timeout"
        compile_program(compiler, timeout_binary, [production_source])
        exercise(timeout_binary, probe_binary, build_directory / "production-work")
        fork_interposer = build_directory / "fork-interposer.c"
        write_fork_interposer(fork_interposer)
        sigaction_interposer = build_directory / "sigaction-interposer.c"
        write_sigaction_interposer(sigaction_interposer)
        sigchld_binary = build_directory / "timeout-sigchld-order"
        compile_program(
            compiler,
            sigchld_binary,
            [production_source, fork_interposer],
            ("-Dfork=timeout_test_fork",),
        )
        exercise_inherited_ignored_sigchld(sigchld_binary, probe_binary)
        calibrate_mutations(
            compiler,
            production_source,
            probe_binary,
            build_directory,
            fork_interposer,
            sigaction_interposer,
        )

    print("PASS timeout contract")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ContractFailure as error:
        print(f"timeout contract: {error}", file=sys.stderr)
        raise SystemExit(1) from error
