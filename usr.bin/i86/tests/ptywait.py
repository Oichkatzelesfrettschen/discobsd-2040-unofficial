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
"""Wait for a child on a pseudo-terminal while reading what it writes.

Shared by check.py and panel.py, which load it by path next to themselves.
"""

import os
import select
import subprocess
import time


def wait_draining(child, master, seconds):
    """Wait for child to exit while reading its pseudo-terminal output.

    Returns the bytes read and the exit status, None when the child still
    runs after the time, however much it keeps writing. A child that writes
    more than the pseudo-terminal holds blocks in write() while nothing
    reads: the emulator draws the guest's screen again on its way out, and a
    macOS pseudo-terminal holds less than that screen where a Linux one
    holds more.
    """
    got = b""
    end = time.time() + seconds
    while child.poll() is None and time.time() < end:
        ready, _, _ = select.select([master], [], [], 0.05)
        if ready:
            try:
                got += os.read(master, 4096)
            except OSError:
                break
    if child.poll() is None:
        try:
            child.wait(timeout=max(0.0, end - time.time()))
        except subprocess.TimeoutExpired:
            return got, None
    # The child has exited, so what is left in the master is all it wrote.
    stop = time.time() + 2
    while time.time() < stop:
        ready, _, _ = select.select([master], [], [], 0.05)
        if not ready:
            break
        try:
            more = os.read(master, 65536)
        except OSError:
            break
        if not more:
            break
        got += more
    return got, child.poll()
