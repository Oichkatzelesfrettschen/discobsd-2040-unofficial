"""Exercise the service attachment loop extracted from kernel startup."""

import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PREFIX = """\
#include <stddef.h>
#include <sys/config.h>
static int events[3], count;
static void first(void) { events[count++] = 11; }
static void second(void) { events[count++] = 22; }
struct conf_service conf_service_init[] = { { first }, { second }, { NULL }, { NULL } };
static void attach_services(void)
{
"""
SUFFIX = """\
}
int main(void)
{
    attach_services();
    return count != 2 || events[0] != 11 || events[1] != 22;
}
"""


def main():
    source = (ROOT / "sys/kern/init_main.c").read_text(encoding="utf-8")
    start = source.index("\tstruct conf_service *svc;")
    end = source.index("\n\n", start)
    body = source[start:end]
    if body.count("svc++") != 1:
        raise SystemExit("FAIL service-dispatch: iteration boundary changed")
    with tempfile.TemporaryDirectory(prefix="discobsd-service-dispatch-") as temporary:
        directory = Path(temporary)
        for compiler in ("cc", "clang"):
            for mutant in (False, True):
                selected = body.replace("svc++", "svc += 2") if mutant else body
                fixture = directory / "service.c"
                fixture.write_text(PREFIX + selected + SUFFIX, encoding="utf-8")
                binary = directory / "service"
                subprocess.run([compiler, "-std=gnu17", "-Wall", "-Wextra", "-Werror",
                                "-I", str(ROOT / "sys"), str(fixture), "-o", str(binary)],
                               check=True)
                status = subprocess.run([str(binary)], check=False).returncode
                if (status == 0) == mutant:
                    raise SystemExit(f"FAIL service-dispatch: {compiler} mutant={mutant}")
    print("PASS service-dispatch: ordered calls and skipped-entry mutant")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
