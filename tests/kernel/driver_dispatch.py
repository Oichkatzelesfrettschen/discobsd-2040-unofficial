"""Exercise production controller and device dispatch with recorded probes."""

import argparse
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ("sys/arch/rp2040/rp2040/machdep.c", "sys/arch/stm32/stm32/machdep.c")
LEVELS = ("O0", "O", "O1", "O2", "O3", "Og", "Os", "Oz")
FLAGS = ("-std=gnu17", "-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wundef",
         "-Wformat=2")

FIXTURE = """\
#include <sys/config.h>
void cpuidentify(void) { }
void watchdog_init(void) { }
extern struct conf_ctlr conf_ctlr_init[];
extern struct conf_device conf_device_init[];
static int events[8];
static int count;
static int wrong_pointer;
static int controller_probe(struct conf_ctlr *controller)
{
    if (controller->ctlr_unit < 1 || controller->ctlr_unit > 2 ||
        controller != &conf_ctlr_init[controller->ctlr_unit - 1])
        wrong_pointer = 1;
    events[count++] = controller->ctlr_unit;
    return controller->ctlr_unit == 1;
}
static int device_probe(struct conf_device *device)
{
    if (device->dev_unit < 10 || device->dev_unit > 14 ||
        device != &conf_device_init[device->dev_unit - 10])
        wrong_pointer = 1;
    events[count++] = 100 + device->dev_unit;
    return device->dev_unit != 14;
}
struct controller_driver controller_driver = { "controller", controller_probe };
struct driver device_driver = { "device", device_probe };
struct conf_ctlr conf_ctlr_init[] = {
    { .ctlr_driver = &controller_driver, .ctlr_unit = 1 },
    { .ctlr_driver = &controller_driver, .ctlr_unit = 2 },
    { 0 }
};
struct conf_device conf_device_init[] = {
    { .dev_driver = &device_driver, .dev_unit = 10 },
    { .dev_driver = &device_driver, .dev_cdriver = &controller_driver,
      .dev_unit = 11, .dev_ctlr = 1 },
    { .dev_driver = &device_driver, .dev_cdriver = &controller_driver,
      .dev_unit = 12, .dev_ctlr = 2 },
    { .dev_driver = &device_driver, .dev_cdriver = &controller_driver,
      .dev_unit = 13, .dev_ctlr = 9 },
    { .dev_driver = &device_driver, .dev_cdriver = &controller_driver,
      .dev_unit = 14, .dev_ctlr = 1 },
    { 0 }
};
"""
ASSERTIONS = """\
int main(void)
{
    config();
    if (wrong_pointer || count != 5 || events[0] != 1 || events[1] != 2 || events[2] != 110 ||
        events[3] != 111 || events[4] != 114)
        return 1;
    if (conf_ctlr_init[0].ctlr_alive != 1 || conf_ctlr_init[1].ctlr_alive != 0)
        return 2;
    if (conf_device_init[0].dev_alive != 1 || conf_device_init[1].dev_alive != 1 ||
        conf_device_init[2].dev_alive != 0 || conf_device_init[3].dev_alive != 0 ||
        conf_device_init[4].dev_alive != 0)
        return 3;
    return 0;
}
"""


def source_body(path):
    text = path.read_text(encoding="utf-8")
    start = text.index("\nstatic int\nis_controller_alive(")
    end = text.index("\n/*\n * Sit and wait", start)
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", action="append", dest="compilers")
    args = parser.parse_args()
    compilers = args.compilers or ["cc", "clang"]
    failures = []
    with tempfile.TemporaryDirectory(prefix="discobsd-driver-dispatch-") as temporary:
        directory = Path(temporary)
        for relative in SOURCES:
            body = source_body(ROOT / relative)
            needle = "if (is_controller_alive(dev->dev_cdriver, dev->dev_ctlr))"
            if body.count(needle) != 1:
                failures.append(f"{relative}: expected one controller admission")
                continue
            for compiler in compilers:
                compiler_parts = shlex.split(compiler)
                executable = shutil.which(compiler_parts[0]) if compiler_parts else None
                if executable is None:
                    failures.append(f"{compiler}: compiler unavailable")
                    continue
                for level in LEVELS:
                    for mutant in ("none", "admission", "pointer"):
                        if mutant != "none" and level != "O2":
                            continue
                        selected = body
                        if mutant == "admission":
                            selected = body.replace(
                                needle,
                                "if (is_controller_alive(dev->dev_cdriver, dev->dev_ctlr) || 1)",
                            )
                        elif mutant == "pointer":
                            call = "(*dev->dev_driver->d_init)(dev)"
                            if body.count(call) != 1:
                                failures.append(f"{relative}: expected one device callback")
                                continue
                            selected = body.replace(
                                call, "(*dev->dev_driver->d_init)(&conf_device_init[0])"
                            )
                        input_path = directory / "dispatch.c"
                        input_path.write_text(FIXTURE + selected + ASSERTIONS, encoding="utf-8")
                        output_path = directory / "dispatch"
                        command = [executable, *compiler_parts[1:], *FLAGS, f"-{level}",
                                   "-I", str(ROOT / "sys"),
                                   str(input_path), "-o", str(output_path)]
                        compiled = subprocess.run(command, capture_output=True, text=True,
                                                  check=False)
                        if compiled.returncode:
                            failures.append(f"{relative} {compiler} -{level}: "
                                            f"compile failed: {compiled.stderr.strip()}")
                            continue
                        result = subprocess.run([str(output_path)], check=False)
                        if (result.returncode == 0) == (mutant != "none"):
                            problem = "mutant accepted" if mutant != "none" else "control failed"
                            failures.append(f"{relative} {compiler} -{level}: "
                                            f"{problem} (exit {result.returncode})")
    if failures:
        for failure in failures:
            print(f"FAIL driver-dispatch: {failure}")
        return 1
    print("PASS driver-dispatch: RP2040 and STM32 controls and admission mutants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
