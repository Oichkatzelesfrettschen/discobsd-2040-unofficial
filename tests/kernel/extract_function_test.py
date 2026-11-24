# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
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
"""Check the shared C function extractor's verdicts on synthetic sources."""

import sys

from exec_elf_admission_extract import definition_signature, extract_function

SOURCE = """int
pipe_rw(struct file *fp, struct uio *uio)
{
    if (fp) {
        return (1);
    }
    return (0);
}
"""


def raises(call, text):
    try:
        call()
    except ValueError as error:
        return text in str(error)
    return False


def main():
    cases = [
        (
            "a definition without a space before its parameters is found",
            extract_function(
                SOURCE, definition_signature(SOURCE, "int", "pipe_rw"), "pipe_rw"
            ).endswith("return (0);\n}"),
        ),
        (
            "a definition with a space before its parameters is found",
            definition_signature(SOURCE.replace("pipe_rw(", "pipe_rw ("), "int", "pipe_rw")
            == "int\npipe_rw (",
        ),
        (
            "a missing definition names the function",
            raises(
                lambda: definition_signature(SOURCE, "int", "readp"), "readp definition not found"
            ),
        ),
        (
            "a repeated definition names the function",
            raises(
                lambda: definition_signature(SOURCE + SOURCE, "int", "pipe_rw"),
                "pipe_rw definition is not unique",
            ),
        ),
        (
            "a missing signature names the caller's label",
            raises(
                lambda: extract_function(SOURCE, "int\nwritep(", "writep"),
                "writep signature not found",
            ),
        ),
        (
            "a repeated signature names the caller's label",
            raises(
                lambda: extract_function(SOURCE + SOURCE, "int\npipe_rw(", "pipe_rw"),
                "pipe_rw signature is not unique",
            ),
        ),
        (
            "an unterminated function names the caller's label",
            raises(
                lambda: extract_function(SOURCE[:-3], "int\npipe_rw(", "pipe_rw"),
                "unterminated pipe_rw function",
            ),
        ),
        (
            "the default label stays the ELF loader",
            raises(
                lambda: extract_function(SOURCE, "int\nexec_elf_check("),
                "ELF loader signature not found",
            ),
        ),
    ]
    failed = [what for what, ok in cases if not ok]
    for what, ok in cases:
        print(f"{'PASS' if ok else 'FAIL'} extract-function: {what}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
