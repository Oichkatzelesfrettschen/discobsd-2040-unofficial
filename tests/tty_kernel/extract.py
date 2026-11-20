# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""Extract the production tty.c code a kernel termios gate runs on the host.

The gate compiles the extracted text against the kernel's own struct tty, so
a change to the layout or to the extracted function breaks it. Each snippet
is located by a signature that must occur once; a missing or repeated
signature stops the extraction rather than producing a smaller gate.
"""

import argparse
import hashlib
from pathlib import Path

FUNCTIONS = (
    ("ttychars", "void\nttychars(struct tty *tp)"),
    ("ttywait", "static void\nttywait(register struct tty *tp)"),
)

# ttioctl cases: (first label, text that ends the last case of the group).
CASE_GROUPS = (
    ("mintime", "    case TIOCGMINTIME:\n", "    case TIOCSMINTIME:\n        tp->t_vmin"),
    ("drain", "    case TIOCDRAIN:\n        ttywait", "    case TIOCDRAIN:\n        ttywait"),
)


def extract_function(source: str, signature: str, name: str) -> str:
    """Return one unique function definition, including its closing brace."""
    if source.count(signature) != 1:
        raise ValueError(name + " signature is not unique")
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for offset in range(opening, len(source)):
        character = source[offset]
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return source[start : offset + 1]
    raise ValueError("unterminated " + name + " function")


def extract_cases(source: str, first: str, last: str, name: str) -> str:
    """Return the ttioctl cases from the first label through the last break."""
    for label in (first, last):
        if source.count(label) != 1:
            raise ValueError(name + " case label is not unique: " + label)
    start = source.index(first)
    end_label = source.index(last)
    if end_label < start:
        raise ValueError(name + " case labels are out of order")
    stop = source.index("break;\n", end_label) + len("break;\n")
    return source[start:stop]


def check_payload_lists(source: str) -> None:
    """Require both requests in the payload lists of ioctl() (sys_generic.c).

    That function copies a request's parameter only for a listed command; an
    unlisted one reaches the tty with the user's pointer instead of a kernel
    copy. A get request belongs before the bzero of the payload, a set request
    between that call and the default label.
    """
    marker = "bzero((caddr_t)&payload, sizeof payload);"
    default = "        break;\n    default:\n"
    if source.count(marker) != 1 or source.count(default) != 1:
        raise ValueError("ioctl() payload lists are not where expected")
    split = source.index(marker)
    end = source.index(default)
    if source[:split].count("    case TIOCGMINTIME:\n") != 1:
        raise ValueError("TIOCGMINTIME is not in the get payload list")
    if source[split:end].count("    case TIOCSMINTIME:\n") != 1:
        raise ValueError("TIOCSMINTIME is not in the set payload list")


JOB_CONTROL_REQUESTS = (
    "TIOCSMINTIME",
    "TIOCDRAIN",
    "TIOCSTOP",
    "TIOCSTART",
)


def check_job_control_list(source: str) -> None:
    """Require the requests that change a terminal in ttioctl()'s first switch.

    A caller in a background process group that issues one of these requests
    gets SIGTTOU (unless it blocks or ignores the signal) before the request
    runs; the list of such requests is the run of case labels that ends at
    the while loop sending the signal.
    """
    loop = "        while (u.u_procp->p_pgrp != tp->t_pgrp"
    if source.count(loop) != 1:
        raise ValueError("the SIGTTOU loop is not where expected")
    end = source.index(loop)
    start = source.rindex("    switch (com) {", 0, end)
    region = source[start:end]
    for request in JOB_CONTROL_REQUESTS:
        if region.count("    case " + request + ":\n") != 1:
            raise ValueError(request + " is not in the SIGTTOU request list")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--payload-source", type=Path, required=True)
    arguments = parser.parse_args()

    check_payload_lists(arguments.payload_source.read_text())
    source_bytes = arguments.source.read_bytes()
    source = source_bytes.decode()
    check_job_control_list(source)
    parts = [
        "/* Extracted from "
        + arguments.source.name
        + " SHA-256 "
        + hashlib.sha256(source_bytes).hexdigest()
        + ". */\n"
    ]
    for name, signature in FUNCTIONS:
        parts.append(extract_function(source, signature, name) + "\n\n")
    for name, first, last in CASE_GROUPS:
        body = extract_cases(source, first, last, name)
        parts.append(
            "static int\n"
            + name
            + "_request(struct tty *tp, u_int com, caddr_t data)\n{\n"
            + "    (void)data;\n"
            + "    switch (com) {\n"
            + body
            + "    default:\n        return (-1);\n    }\n    return (0);\n}\n\n"
        )
    arguments.output.write_text("".join(parts))


if __name__ == "__main__":
    main()
