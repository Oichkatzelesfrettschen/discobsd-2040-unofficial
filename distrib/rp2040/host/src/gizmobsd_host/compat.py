# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
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
#
"""Entry points for the command names that predate the gizmobsd rename.

For one release each former name (discobsd-term, discobsd-web, discobsd-link,
discobsd-console, discobsd-release, discobsd-sramdump) stays installed. It
prints one line on stderr naming the new command and runs that command's
main() with the same arguments.
"""

from __future__ import annotations

import importlib
import sys


def _forward(tool: str, argv=None):
    sys.stderr.write("discobsd-%s is renamed gizmobsd-%s; use the new name\n" % (tool, tool))
    return importlib.import_module("gizmobsd_host." + tool).main(argv)


def term(argv=None):
    return _forward("term", argv)


def web(argv=None):
    return _forward("web", argv)


def link(argv=None):
    return _forward("link", argv)


def console(argv=None):
    return _forward("console", argv)


def release(argv=None):
    return _forward("release", argv)


def sramdump(argv=None):
    return _forward("sramdump", argv)
