"""Extract exec_clear() and exec_finish() for an ILP32 host gate."""

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

CLEAR_SIGNATURE = "void exec_clear(struct exec_params *epp)"
FINISH_SIGNATURE = "static void\nexec_finish(struct exec_params *epp, struct inode *ip)"
PENDING = "    epp->flags |= EXEC_CLOSE_PENDING;\n"

# Each mutant breaks one half of the contract between exec_clear() and
# exec_finish() that the host fixture observes.
MUTATIONS = {
    # The close runs inside exec_clear(), under the executable's inode lock.
    "direct-close": (PENDING, "    fdcloseexec();\n" + PENDING),
    # The closes stay pending forever: exec_finish() has nothing to do.
    "flag-omitted": (PENDING, ""),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", choices=sorted(MUTATIONS))
    arguments = parser.parse_args()

    parts = []
    for name, signature in (
        ("sys/kern/exec_subr.c", CLEAR_SIGNATURE),
        ("sys/kern/kern_exec.c", FINISH_SIGNATURE),
    ):
        source_bytes = (arguments.root / name).read_bytes()
        function = extract_function(source_bytes.decode(), signature, signature)
        if name.endswith("exec_subr.c") and arguments.mutation:
            before, after = MUTATIONS[arguments.mutation]
            if function.count(before) != 1:
                raise SystemExit(arguments.mutation + ": expected one production pattern")
            function = function.replace(before, after, 1)
        parts.append(
            "/* Extracted from "
            + name
            + " SHA-256 "
            + hashlib.sha256(source_bytes).hexdigest()
            + ". */\n"
            + function
        )
    arguments.output.write_text("\n\n".join(parts) + "\n")


if __name__ == "__main__":
    main()
