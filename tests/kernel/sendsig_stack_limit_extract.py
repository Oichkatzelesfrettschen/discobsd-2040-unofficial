"""Extract sendsig() and the stack-limit decision for an ILP32 host gate."""

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

from sigreturn_context_extract import extract_frame
from swap_ioctl_extract import extract_function

SENDSIG_FILE = "sys/arch/rp2040/rp2040/sig_machdep.c"
SENDSIG_SIGNATURE = "void\nsendsig(sig_t p, int sig, long mask)"
LIMIT_FILE = "sys/kern/kern_resource.c"
LIMIT_SIGNATURE = "int\nrlimit_stack_admits(size_t depth)"
ADMISSION = """\t\t\tif (!rlimit_stack_admits(USER_TOP(u.u_procp) -
\t\t\t    (u_int)sfp)) {"""

# Each mutant weakens the call site and leaves rlimit_stack_admits() intact.
MUTATIONS = {
    # The comparison is gone: every frame is admitted.
    "limit-unchecked": (
        ADMISSION,
        """\t\t\tif (0 && !rlimit_stack_admits(USER_TOP(u.u_procp) -
\t\t\t    (u_int)sfp)) {""",
    ),
    # The depth is measured from the data base instead of the stack top, so
    # a frame near the top of the window reads as the deepest one.
    "base-swapped": (
        ADMISSION,
        """\t\t\tif (!rlimit_stack_admits((u_int)sfp -
\t\t\t    (u_int)u.u_procp->p_daddr)) {""",
    ),
    "frame-span-unchecked": (
        """\tif (baduaddr_span((caddr_t)sfp, sizeof(*sfp))) {
\t\tfatalsig(SIGILL);
\t\treturn;
\t}
""",
        "",
    ),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", choices=sorted(MUTATIONS))
    arguments = parser.parse_args()

    sig_bytes = (arguments.root / SENDSIG_FILE).read_bytes()
    limit_bytes = (arguments.root / LIMIT_FILE).read_bytes()
    sig_text = sig_bytes.decode()
    sendsig = extract_function(sig_text, SENDSIG_SIGNATURE, "sendsig")
    if arguments.mutation:
        before, after = MUTATIONS[arguments.mutation]
        if sendsig.count(before) != 1:
            raise SystemExit(arguments.mutation + ": expected one production pattern")
        sendsig = sendsig.replace(before, after, 1)
    limit = extract_function(limit_bytes.decode(), LIMIT_SIGNATURE, "limit")
    arguments.output.write_text(
        "/* Extracted from "
        + SENDSIG_FILE
        + " SHA-256 "
        + hashlib.sha256(sig_bytes).hexdigest()
        + " and "
        + LIMIT_FILE
        + " SHA-256 "
        + hashlib.sha256(limit_bytes).hexdigest()
        + ". */\n"
        + limit
        + "\n\n"
        + extract_frame(sig_text)
        + "\n\n"
        + sendsig
        + "\n"
    )


if __name__ == "__main__":
    main()
