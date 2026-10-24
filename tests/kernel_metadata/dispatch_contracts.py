"""Exercise generated syscall declarations and configuration-selected tables."""

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path


class ContractError(ValueError):
    """A dispatch interface or calibrated verdict differs."""


def require(condition, message):
    if not condition:
        raise ContractError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_command(command, label, cases, *, cwd=None, reject=None):
    observed = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                              env=dict(os.environ, LC_ALL="C"))
    cases.append({"label": label, "argv": command, "exit_code": observed.returncode,
                  "stdout": observed.stdout, "stderr": observed.stderr,
                  "expected_rejection": reject})
    if reject is None:
        require(observed.returncode == 0, label + ": " + observed.stderr)
    else:
        require(observed.returncode == 1 and
                re.search(reject, observed.stderr + observed.stdout, re.I | re.S),
                label + " violated rejection contract: " + observed.stderr)
    return observed.stdout


def selected_rows(rows, inet, glob):
    selected = []
    for number, nargs, handler, condition, trace, _public in rows:
        require(int(number) == len(selected), "master order differs")
        count = int(nargs)
        if condition in {"IFNET", "ERRNET"} and not inet:
            handler = "nonet" if condition == "ERRNET" else "nosys"
            count = 0
        elif condition == "GLOB" and not glob:
            handler = "nosys"
        else:
            require(condition in {"ALWAYS", "IFNET", "ERRNET", "GLOB"},
                    "unknown master condition")
        selected.append((handler, count, trace))
    return selected


def harness(rows, handlers, inet, glob):
    lines = ['#include <sys/syscall_dispatch.h>',
             'static int observed_handler;',
             'static int equal_text(const char *left, const char *right)',
             '{ while (*left && *left == *right) { ++left; ++right; }',
             '  return *left == *right; }']
    identities = {name: index + 1 for index, name in enumerate(handlers)}
    for name in handlers:
        lines.append(f'void {name}(void) {{ observed_handler = {identities[name]}; }}')
    lines.extend(['int main(void)', '{', f' if (nsysent != {len(rows)}) return 1;',
                  ' if ((unsigned long)(const void *)syscall_handlers %',
                  '     _Alignof(syscall_handler_t)) return 2;'])
    for index, (handler, count, trace) in enumerate(selected_rows(rows, inet, glob)):
        lines.extend([f' if (syscall_handlers[{index}] != {handler} ||',
                      f'     syscall_nargs[{index}] != {count}) return 3;',
                      f' observed_handler = 0; syscall_handlers[{index}]();',
                      f' if (observed_handler != {identities[handler]}) return 4;',
                      f' if (!equal_text(syscall_name({index}), {json.dumps(trace)})) return 5;'])
    lines.extend([' if (!equal_text(syscall_name((unsigned int)nsysent), "?") ||',
                  '     !equal_text(syscall_name(~0U), "?")) return 6;',
                  ' return 0;', '}'])
    return '\n'.join(lines) + '\n'


def check(root, host_cc):
    cases = []
    header = root / 'sys/sys/syscall_dispatch.h'
    require(header.is_file(), 'generated syscall dispatch header is absent')
    rows = [line.strip().split('|') for line in
            (root / 'sys/kern/syscalls.master').read_text().splitlines()
            if line.strip() and not line.lstrip().startswith('#')]
    handlers = sorted({row[2] for row in rows} | {'nonet'})
    original = header.read_text()
    prototypes = re.findall(r'^void (\w+)\(void\);$', original, re.M)
    require(prototypes == handlers, 'dispatch prototypes differ from master/fallback set')
    # Assembly includes the numeric ABI output through libc/arm/sys/SYS.h.
    require(digest(root / 'include/syscall.h') ==
            'd807254755fae8fbd7e298b797495b73fdcf48df07460e01a8b7bf236859a528',
            'public numeric syscall header changed')
    host = shlex.split(host_cc)
    require(host and shutil.which(host[0]), 'host compiler is unavailable')
    host[0] = shutil.which(host[0])
    clang = shutil.which('clang')
    arm = shutil.which('arm-none-eabi-gcc')
    require(clang and arm, 'Clang and ARM GCC are required')
    common = ['-Wall', '-Wextra', '-Werror', '-Wstrict-prototypes',
              '-Wold-style-definition', '-ffreestanding', '-fno-builtin',
              '-nostdinc', '-I' + str(root / 'sys')]
    portable = ['-std=c17', '-pedantic-errors', *common]
    target = ['-mcpu=cortex-m0plus', '-mabi=aapcs', '-mlittle-endian',
              '-mthumb', '-mfloat-abi=soft']
    profiles = [('host.c17', [*host, *portable]),
                ('clang.c17', [clang, *portable]),
                ('arm.c17', [arm, *portable, *target]),
                ('clang.arm.c17', [clang, '--target=arm-none-eabi', *portable, *target])]
    source_hashes = {str(path.relative_to(root)): digest(path) for path in
                     [header, root / 'sys/kern/syscalls.master',
                      root / 'sys/kern/init_sysent.c', root / 'sys/kern/syscalls.c']}
    tools = {tool: {'sha256': digest(Path(tool)), 'version':
                   subprocess.check_output([tool, '--version'], text=True).splitlines()[0]}
             for tool in {host[0], clang, arm}}
    types = ('_Static_assert(sizeof(syscall_nargs[0]) == 1, "argument byte");\n'
             '_Static_assert(_Generic(&syscall_name, const char *(*)(unsigned int): 1, '
             'default: 0), "trace type");\n')
    types += ''.join(f'_Static_assert(_Generic(&{name}, syscall_handler_t: 1, '
                     f'default: 0), "{name} type");\n' for name in handlers)
    rename = ['-D' + name + '=dispatch_test_' + name for name in handlers]
    with tempfile.TemporaryDirectory(prefix='discobsd-dispatch-') as temporary:
        workspace = Path(temporary)
        forms = {'standalone': '#include <sys/syscall_dispatch.h>\n',
                 'repeated': '#include <sys/syscall_dispatch.h>\n' * 2,
                 'stdint-after': '#include <sys/syscall_dispatch.h>\n'
                                  '#include <sys/stdint.h>\n'
                                  '_Static_assert(_Generic(syscall_nargs, '
                                  'const uint8_t *: 1, default: 0), \"argument type\");\n',
                 'glob-repeated': '#define KERNEL\n#include <sys/glob.h>\n'
                                  '#include <sys/glob.h>\n',
                 'glob-first': '#define KERNEL\n#include <sys/glob.h>\n'
                               '#include <sys/syscall_dispatch.h>\n',
                 'glob-last': '#define KERNEL\n#include <sys/syscall_dispatch.h>\n'
                              '#include <sys/glob.h>\n'}
        for label, flags in profiles:
            for form, includes in forms.items():
                consumer = workspace / 'consumer.c'
                consumer.write_text(includes + types)
                run_command([*flags, '-fsyntax-only', str(consumer)],
                            label + '.' + form, cases)
            for name, mutation, expression in [
                ('missing-handler', original.replace('void sc_msec(void);\n', '', 1),
                 r'sc_msec.*undeclared|undeclared.*sc_msec'),
                ('missing-fallback', original.replace('void nonet(void);\n', '', 1),
                 r'nonet.*undeclared|undeclared.*nonet'),
                ('wrong-prototype', original.replace('void nosys(void);',
                                                     'void nosys(int);', 1),
                 r'static assertion.*nosys type')]:
                require(mutation != original, 'inert ' + name + ' mutation')
                override = workspace / name / 'sys'
                override.mkdir(parents=True, exist_ok=True)
                (override / header.name).write_text(mutation)
                consumer.write_text(forms['standalone'] + types)
                run_command([flags[0], '-I' + str(override.parent), *flags[1:],
                             '-fsyntax-only', str(consumer)], label + '.' + name,
                            cases, reject=expression)
            consumer.write_text(forms['standalone'] +
                                'void (*wrong_handler)(int) = nosys;\n')
            run_command([*flags, '-fsyntax-only', str(consumer)],
                        label + '.callback-mismatch', cases, reject='incompatible')
        consumer.write_text(forms['repeated'])
        for name, content, count in [('guarded', original, 1),
                                     ('guard-disabled', original.replace(
                                         '#ifndef _SYS_SYSCALL_DISPATCH_H_\n', '').replace(
                                         '#define _SYS_SYSCALL_DISPATCH_H_\n', '').replace(
                                         '#endif /* _SYS_SYSCALL_DISPATCH_H_ */', ''), 2)]:
            require(name == 'guarded' or content != original, 'inert guard mutation')
            override = workspace / name / 'sys'
            override.mkdir(parents=True, exist_ok=True)
            (override / header.name).write_text(content)
            output = run_command([*host, '-I' + str(override.parent), *portable,
                                  '-E', '-P', str(consumer)], name + '.preprocess', cases)
            found = len(re.findall(r'typedef void \(\*syscall_handler_t\)\(void\);', output))
            require(found == count, name + ' guard processing count differs')
        for label, flags in profiles:
            for inet, glob, zero in [(False, False, False), (True, False, False),
                                     (False, True, False), (True, True, False),
                                     (True, True, True)]:
                defines = ([f'-DINET={int(not zero)}'] if inet else []) + (
                           [f'-DGLOB_ENABLED={int(not zero)}'] if glob else [])
                variant = f'{label}.inet{int(inet)}.glob{int(glob)}.zero{int(zero)}'
                for source in ['init_sysent.c', 'syscalls.c']:
                    run_command([*flags, *defines, '-fsyntax-only',
                                 str(root / 'sys/kern' / source)], variant + '.' + source, cases)
                if label not in {'host.c17', 'clang.c17'}:
                    continue
                consumer.write_text(harness(rows, handlers, inet, glob))
                executable = workspace / 'dispatch-test'
                run_command([*flags, *defines, *rename, str(consumer),
                             str(root / 'sys/kern/init_sysent.c'),
                             str(root / 'sys/kern/syscalls.c'), '-o', str(executable)],
                            variant + '.link', cases)
                run_command([str(executable)], variant + '.execute', cases)
        consumer.write_text(harness(rows, handlers, False, False))
        broken_table = workspace / 'wrong-condition.c'
        table = (root / 'sys/kern/init_sysent.c').read_text()
        mutated_table = table.replace('    nonet,', '    nosys,', 1)
        require(mutated_table != table, 'inert conditional mutation')
        broken_table.write_text(mutated_table)
        executable = workspace / 'bad-dispatch-test'
        run_command([*host, *portable, *rename, str(consumer), str(broken_table),
                     str(root / 'sys/kern/syscalls.c'), '-o', str(executable)],
                    'conditional-control.link', cases)
        observed = subprocess.run([str(executable)], check=False)
        require(observed.returncode == 3, 'conditional control escaped the row oracle')
        cases.append({'label': 'conditional-control.execute', 'exit_code': 3,
                      'expected_exit_code': 3})
        # The generator's public entry point checks every tracked output, including
        # the private header. A private copied tree makes the stale-output control safe.
        generator = root / 'tools/gen_kernel_metadata.py'
        outputs = ['include/syscall.h', 'sys/sys/syscall_dispatch.h',
                   'sys/kern/init_sysent.c', 'sys/kern/syscalls.c', 'sys/kern/errlist.c']
        fixture = workspace / 'freshness'
        for name in [*outputs, 'sys/kern/syscalls.master', 'sys/kern/errlist.master']:
            destination = fixture / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(root / name, destination)
        command = [os.environ['PYTHON'], str(generator), '--root', str(fixture), '--check']
        run_command(command, 'fresh-output', cases)
        (fixture / 'sys/sys/syscall_dispatch.h').write_text(original + '\n/* stale */\n')
        run_command(command, 'stale-header', cases, reject='stale')
        consumer.write_text('#include <syscall.h>\n.word SYS_MAXSYSCALL\n'
                            '.word SYS_vfork\n.word SYS_openat\n'
                            '.word SYS_unlinkat_beneath\n'
                            '.word SYS_publishat_beneath\n')
        output = run_command([arm, '-E', '-P', '-x', 'assembler-with-cpp',
                              '-nostdinc', '-I' + str(root / 'include'), str(consumer)],
                             'numeric-assembly', cases)
        require(output.split() == ['.word', '160', '.word', '2', '.word', '157',
                                   '.word', '158', '.word', '159'],
                'numeric assembly differs')
        for configuration in ['PICO', 'PICO_UART']:
            build = root / 'sys/arch/rp2040/compile' / configuration
            evaluated = subprocess.check_output(
                ['bmake', '-C', str(build), '-V', '${CC} ${CFLAGS} ${INCLUDES} ${PARAM} -DKERNEL'],
                text=True)
            flags = shlex.split(evaluated)
            require(all(flag in flags for flag in ['-std=gnu17', '-Wall', '-Wextra',
                    '-Werror', '-mcpu=cortex-m0plus', '-mthumb', '-mfloat-abi=soft']),
                    'evaluated production flags differ')
            for includes in ['#include <sys/glob.h>\n#include <sys/systm.h>\n',
                             '#include <sys/systm.h>\n#include <sys/glob.h>\n',
                             '#include <sys/systm.h>\n#include <sys/systm.h>\n']:
                consumer.write_text('#include <sys/param.h>\n' + includes + types)
                run_command([*flags, '-fsyntax-only', str(consumer)],
                            configuration + '.compatibility-includes', cases, cwd=build)
            for defines in [[], ['-DINET', '-DGLOB_ENABLED']]:
                run_command([*flags, *defines, '-fsyntax-only',
                             str(root / 'sys/kern/init_sysent.c')],
                            configuration + '.production-table', cases, cwd=build)
        consumer.write_text('#include <sys/glob.h>\n#include <sys/glob.h>\n'
                            '_Static_assert(_Generic(&rdglob, int (*)(int): 1, default: 0),'
                            ' "user rdglob");\n'
                            '_Static_assert(_Generic(&wrglob, int (*)(int, int): 1, default: 0),'
                            ' "user wrglob");\n')
        for label, flags in profiles:
            run_command([*flags, '-fsyntax-only', str(consumer)], label + '.user-glob', cases)
    return {'schema_version': 1, 'handler_count': len(handlers), 'slots': len(rows),
            'source_hashes': source_hashes, 'tools': tools, 'cases': cases,
            'runtime_scope': 'host stubs exercise generated dispatch selection and names',
            'optional_implementation_runtime': 'unrun', 'hardware': 'unrun'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--host-cc', default='cc')
    parser.add_argument('--output', type=Path)
    arguments = parser.parse_args()
    try:
        result = check(arguments.root.resolve(), arguments.host_cc)
    except (ContractError, OSError, subprocess.CalledProcessError) as error:
        print('FAIL syscall dispatch contracts:', error)
        return 1
    if arguments.output:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f'PASS syscall dispatch contracts: {len(result["cases"])} cases, '
          f'{result["handler_count"]} handlers, {result["slots"]} slots')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
