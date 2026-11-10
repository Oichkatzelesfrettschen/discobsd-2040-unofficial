"""Check that RP2040 compiler routes keep checkout paths out of debug data."""
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROUTES = ('bin/echo', 'usr.bin/smlrc', 'sys/arch/rp2040/compile/PICO')
ENVIRONMENT = {key: value for key, value in os.environ.items()
               if key not in ('MAKEFLAGS', 'MFLAGS', 'C17_CAPTURE_DIR', 'DEBUG_PATH_MAP')}


def invoke(arguments, directory):
    result = subprocess.run(arguments, cwd=directory, env=ENVIRONMENT,
                            capture_output=True, text=True, check=False, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stdout.replace(str(ROOT), '.') +
                           result.stderr.replace(str(ROOT), '.'))
    return result.stdout


def check_route(make, directory, temporary):
    cwd = ROOT / directory
    source = temporary / 'debug_path_probe.c'
    source.write_text('int debug_path_probe(int value) { return value + 1; }\n')
    stripped = []
    for mapped in (True, False):
        overrides = [] if mapped else ['DEBUG_PATH_MAP=']
        command = shlex.split(invoke([*make, '-k', 'MACHINE=rp2040', *overrides,
                                     '-V', '${CC} ${CFLAGS}'], cwd).strip())
        if not command:
            raise RuntimeError(f'{directory}: empty compiler command')
        output = temporary / ('mapped.o' if mapped else 'unmapped.o')
        invoke([*command, '-g', '-c', str(source), '-o', str(output)], cwd)
        contents = output.read_bytes()
        if b'.debug_info' not in contents:
            raise RuntimeError(f'{directory}: object has no debug information')
        contains_root = str(ROOT).encode() in contents
        if contains_root == mapped:
            raise RuntimeError(f'{directory}: debug-prefix mapping control failed')
        stripped_output = temporary / ('mapped-stripped.o' if mapped else 'unmapped-stripped.o')
        invoke(['arm-none-eabi-objcopy', '--strip-debug', str(output), str(stripped_output)], cwd)
        stripped.append(stripped_output.read_bytes())
    if stripped[0] != stripped[1]:
        raise RuntimeError(f'{directory}: prefix mapping changed the stripped object')
    print(f'PASS {directory}: mapped and unmapped debug controls; stripped objects match')


def main():
    make = shlex.split(os.environ.get('MAKE', 'bmake'))
    evidence = ROOT / '.build-logs'
    evidence.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='debug-path-check-', dir=evidence) as name:
        for directory in ROUTES:
            check_route(make, directory, Path(name))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f'debug paths: {error}', file=sys.stderr)
        sys.exit(1)
