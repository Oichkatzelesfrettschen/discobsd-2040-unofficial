"""Retired opt-in variable check shared by the board test drivers.

A board driver arms only when its GIZMOBSD_BOARD_* variable is exactly "run".
The former DISCOBSD_BOARD_* spelling does not arm: a driver that finds the old
variable set and the new one unset refuses with exit status 2 and names the new
variable, so an old command line stops instead of running closed and passing
unnoticed.
"""

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

import os
import sys

NEW_PREFIX = "GIZMOBSD_BOARD_"
OLD_PREFIX = "DISCOBSD_BOARD_"
REFUSED = 2


def retired_variable_status(variable, stream=None):
    """Return REFUSED when the old spelling of VARIABLE is set and VARIABLE is not.

    Return None otherwise. The refusal line goes to STREAM (stderr by default)."""
    if not variable.startswith(NEW_PREFIX):
        raise ValueError("%s is not a %s variable" % (variable, NEW_PREFIX))
    old = OLD_PREFIX + variable[len(NEW_PREFIX) :]
    if variable in os.environ or old not in os.environ:
        return None
    print(
        "board test refused: %s is retired and does not arm; set %s=run" % (old, variable),
        file=stream if stream is not None else sys.stderr,
    )
    return REFUSED
