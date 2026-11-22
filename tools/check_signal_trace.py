#!/usr/bin/env python3
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
"""Require a kern.systrace record on every rejection path of the signal code.

sys/arch/rp2040/rp2040/sig_machdep.c leaves a path that rejects a signal
frame or a sigreturn context without a SYSTRACE_SIGNAL record invisible to
a trace run, the failure a trace exists to show. The check requires that

  - sendsig() tests SYSTRACE_ON(SYSTRACE_SIGNAL) within the eight lines
    above its fatalsig(SIGILL) rejection, and
  - sigreturn() reaches u.u_error only through sigreturn_reject(): the one
    assignment of an errno other than EJUSTRETURN sits in that helper, which
    tests SYSTRACE_ON(SYSTRACE_SIGNAL).

Before it reads the source it runs both rules against a conforming text and
against one text per rule with the rule's feature removed, and stops with
status 1 unless the conforming text passes and each defective one fails.

Usage: check_signal_trace.py [SOURCE]
Exit status: 0 pass, 1 a rule failed.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.join(HERE, "..", "sys", "arch", "rp2040", "rp2040", "sig_machdep.c")
TRACE = "SYSTRACE_ON(SYSTRACE_SIGNAL)"


def function_body(text, name):
    """Return the text of function `name` from its definition line to the next one at column 0."""
    match = re.search(r"^%s\(.*?^\}" % re.escape(name), text, re.S | re.M)
    return match.group(0) if match else None


def verdict(text):
    """Return the failure messages for a sig_machdep.c text."""
    bad = []
    send = function_body(text, "sendsig")
    if send is None:
        return ["sendsig() not found"]
    lines = send.split("\n")
    rejects = [i for i, line in enumerate(lines) if "fatalsig(SIGILL)" in line]
    if not rejects:
        bad.append("sendsig() has no fatalsig(SIGILL) rejection")
    for index in rejects:
        if not any(TRACE in line for line in lines[max(0, index - 8) : index]):
            bad.append("sendsig() rejects a frame without a %s record" % TRACE)
    reject = function_body(text, "sigreturn_reject")
    if reject is None or TRACE not in reject:
        bad.append("sigreturn_reject() is missing or does not test %s" % TRACE)
    ret = function_body(text, "sigreturn")
    if ret is None:
        bad.append("sigreturn() not found")
    else:
        for line in ret.split("\n"):
            if re.search(r"u\.u_error\s*=", line) and "EJUSTRETURN" not in line:
                bad.append("sigreturn() sets u_error outside sigreturn_reject(): %s" % line.strip())
    return bad


GOOD = """\
void
sendsig(void)
{
\tif (x) {
\t\tif (SYSTRACE_ON(SYSTRACE_SIGNAL))
\t\t\tprintf("r");
\t\tfatalsig(SIGILL);
\t\treturn;
\t}
}

static void
sigreturn_reject(int error)
{
\tu.u_error = error;
\tif (SYSTRACE_ON(SYSTRACE_SIGNAL))
\t\tprintf("r");
}

void
sigreturn(void)
{
\tsigreturn_reject(EINVAL);
\tu.u_error = EJUSTRETURN;
}
"""


def self_test():
    failures = []
    if verdict(GOOD):
        failures.append("conforming text rejected: %r" % verdict(GOOD))
    mutants = {
        "sendsig without its record": GOOD.replace(
            'if (SYSTRACE_ON(SYSTRACE_SIGNAL))\n\t\t\tprintf("r");\n\t\tfatalsig', "fatalsig"
        ),
        "sigreturn_reject without its record": GOOD.replace(
            '\tif (SYSTRACE_ON(SYSTRACE_SIGNAL))\n\t\tprintf("r");\n}', "}"
        ),
        "sigreturn with a direct errno": GOOD.replace(
            "sigreturn_reject(EINVAL);", "u.u_error = EINVAL;"
        ),
    }
    for label, text in mutants.items():
        if text == GOOD:
            failures.append("%s: the mutant changed nothing" % label)
        elif not verdict(text):
            failures.append("%s: accepted" % label)
    return failures


def main(argv):
    failures = self_test()
    for message in failures:
        print("check-signal-trace: self-test FAIL %s" % message, file=sys.stderr)
    if failures:
        return 1
    path = os.path.abspath(argv[0] if argv else SOURCE)
    with open(path, encoding="utf-8") as stream:
        bad = verdict(stream.read())
    for message in bad:
        print("check-signal-trace: FAIL %s" % message, file=sys.stderr)
    if not bad:
        print("check-signal-trace: pass")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
