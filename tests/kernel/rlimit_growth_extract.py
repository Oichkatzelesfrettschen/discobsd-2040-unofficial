"""Extract the RLIMIT_DATA and RLIMIT_STACK growth checks for an ILP32 host gate."""

# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
# Neither the name of the copyright holder nor the names of its contributors
# may be used to endorse or promote products derived from this software
# without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import argparse
import hashlib
from pathlib import Path

from swap_ioctl_extract import extract_function

# (file under the source root, signature) of every extracted definition.
FUNCTIONS = (
    ("sys/kern/kern_resource.c", "int\nrlimit_data_admits(size_t bytes)"),
    ("sys/kern/kern_resource.c", "int\nrlimit_stack_admits(size_t depth)"),
    ("sys/kern/kern_mman.c", "void\nbrk(void)"),
    (
        "sys/arch/rp2040/rp2040/syscall.c",
        "static int\nuser_entry(const struct trapframe *frame)",
    ),
)

# Each mutant removes or weakens one admission the gate must observe.
MUTATIONS = {
    "data-unchecked": (
        "        if (!rlimit_data_admits(",
        "        if (0 && !rlimit_data_admits(",
    ),
    "data-counts-text": (
        "            (size_t)newsize - text : 0)) {",
        "            (size_t)newsize : 0)) {",
    ),
    "shrink-refused": (
        "    if ((size_t)newsize > u.u_dsize) {",
        "    if (newsize >= 0) {",
    ),
    "limit-exclusive": (
        "    return (long)bytes <= u.u_rlimit[RLIMIT_DATA].rlim_cur;",
        "    return (long)bytes < u.u_rlimit[RLIMIT_DATA].rlim_cur;",
    ),
    "stack-limit-exclusive": (
        "    return (long)depth <= u.u_rlimit[RLIMIT_STACK].rlim_cur;",
        "    return (long)depth < u.u_rlimit[RLIMIT_STACK].rlim_cur;",
    ),
    "stack-unchecked": (
        "\t\tif (!rlimit_stack_admits(USER_TOP(p) - sp))",
        "\t\tif (0 && !rlimit_stack_admits(USER_TOP(p) - sp))",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", choices=sorted(MUTATIONS))
    arguments = parser.parse_args()
    parts = []
    for name, signature in FUNCTIONS:
        source_bytes = (arguments.root / name).read_bytes()
        parts.append(
            "/* Extracted from "
            + name
            + " SHA-256 "
            + hashlib.sha256(source_bytes).hexdigest()
            + ". */\n"
            + extract_function(source_bytes.decode(), signature, signature)
        )
    text = "\n\n".join(parts) + "\n"
    if arguments.mutation:
        before, after = MUTATIONS[arguments.mutation]
        if text.count(before) != 1:
            raise SystemExit(arguments.mutation + ": expected one production pattern")
        text = text.replace(before, after, 1)
    arguments.output.write_text(text)


if __name__ == "__main__":
    main()
