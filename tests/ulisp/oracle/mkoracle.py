#!/usr/bin/env python3
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

"""Turn ulisp-picocalc.ino into a C++ file a host compiler accepts.

Usage: mkoracle.py INO SHA256 OUTPUT

The Arduino build adds a prototype for every function and supplies the
board libraries; this does the first and oracle/Arduino.h the second. The
interpreter is otherwise unchanged, apart from three lines the host needs:
the library includes are dropped, the stack pointer register takes its x86
name, and the serial read that the prompt waits on is told apart from the
polls that would swallow piped input.
"""

import hashlib
import re
import sys

FUNCTION = re.compile(
    r"^(?!typedef|static|else|if|for|while|switch|return|struct|enum)"
    r"([A-Za-z_][A-Za-z0-9_ \*]*?[ \*])([A-Za-z_0-9]+) ?\((.*)\) ?\{"
)
LIBRARY = re.compile(r"#include <(SPI|Wire|SD|LittleFS|TFT_eSPI|PCKeyboard|WiFi)\.h>")


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    data = open(sys.argv[1], "rb").read()
    if hashlib.sha256(data).hexdigest() != sys.argv[2]:
        raise SystemExit("mkoracle: %s is not the pinned source" % sys.argv[1])
    lines = data.decode("utf-8").split("\n")
    prototypes = []
    for line in lines:
        match = FUNCTION.match(line)
        if match:
            prototypes.append(
                "%s%s (%s);"
                % (match.group(1).replace("inline ", ""), match.group(2), match.group(3))
            )
    out = ['#include "Arduino.h"', "#define ARDUINO_RASPBERRY_PI_PICO"]
    for line in lines:
        if LIBRARY.match(line) or line.startswith('#include "hardware/'):
            line = "// " + line
        if line.startswith("// Error handling"):
            out.extend(prototypes)
        line = line.replace('asm ("sp")', 'asm ("esp")')
        if line == "    if (Serial.available()) {":
            line = "    if (Serial.avail_in()) {"
        out.append(line)
    out.append("int main(){ setvbuf(stdout, 0, _IONBF, 0); setup(); for(;;) loop(); }")
    with open(sys.argv[3], "w", encoding="utf-8") as output:
        output.write("\n".join(out))


main()
