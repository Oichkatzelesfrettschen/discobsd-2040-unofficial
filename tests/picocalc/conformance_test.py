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

"""Calibrate verify_conformance.py against the live tree and known-bad mutants."""

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/picocalc"))

import verify_conformance  # noqa: E402


def run_verifier(repo_root: Path) -> tuple[int, dict[str, object]]:
    """Run verify_conformance.py in a subprocess and parse its JSON verdict."""
    result = subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools/picocalc/verify_conformance.py"),
            "--root",
            str(repo_root),
            "--json",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode not in (0, 1):
        raise AssertionError(f"unexpected verifier exit {result.returncode}: {result.stderr}")
    payload = json.loads(result.stdout)
    return result.returncode, payload


def main() -> int:
    code, payload = run_verifier(ROOT)
    assert code == 0 and payload["ok"] is True, f"live tree failed: {payload}"

    mutants = [
        (
            "st7365p-f7-restored",
            "st7365p.c",
            "0xb7, 1, 0xc6,",
            "0xb7, 1, 0xc6,\n\t0xf7, 4, 0xa9, 0x51, 0x2c, 0x82,",
            "0xf7",
        ),
        (
            "st7365p-gamma-15-bytes",
            "st7365p.c",
            "0xe0, 14, 0xf0",
            "0xe0, 15, 0x00",
            "init_gamma",
        ),
        (
            "st7365p-unlock-left-open",
            "st7365p.c",
            "\trun_seq(init_lock, sizeof(init_lock));\n",
            "",
            "unlock..lock..wake",
        ),
        (
            "paneltest-missing-set-32",
            "paneltest_sets.h",
            'X("legacy-ili9488-78hz",\tUNLOCK,\tDRIVER,\tFRAME_78HZ)',
            "",
            "33 sets",
        ),
        (
            "picokbd-spklen-too-wide",
            "picokbd.c",
            "#define\tKBD_I2C_SPKLEN\t\t((CPU_KHZ * 50UL) / 1000000UL)",
            "#define\tKBD_I2C_SPKLEN\t\t8",
            "SPKLEN",
        ),
        (
            "picokbd-missing-bus-clear",
            "picokbd.c",
            "\ti2c_bus_clear();\n\ti2c_setup();",
            "\ti2c_setup();",
            "i2c_bus_clear()",
        ),
        (
            "picokbd-lcd-min-zero",
            "picokbd_req.h",
            "#define\tKBD_LCD_MIN\t\t16",
            "#define\tKBD_LCD_MIN\t\t0",
            "KBD_LCD_MIN",
        ),
        (
            "sdspi-ncr-off-by-one",
            "sdspi.c",
            "for (i = 0; i <= NCR_MAX && (r & 0x80); i++)",
            "for (i = 0; i < NCR_MAX && (r & 0x80); i++)",
            "NCR_MAX",
        ),
        (
            "sd-csd-v2-overflow-unguarded",
            "sdspi.c",
            "if (c_size == 0x3fffffU)\n\t\t\treturn ~0U;",
            "",
            "0x3fffffU",
        ),
        (
            "sd-missing-bsy-wait",
            "sd.c",
            "while ((SPI_REG(SSPSR) & SSPSR_BSY) != 0)\n\t\tcontinue;",
            "",
            "SSPSR_BSY",
        ),
        (
            "picopcm-close-ramp-removed",
            "picopcm.c",
            "\t\tpcm_ramp(PCM_SILENCE - 1, 0);",
            "",
            "pcmclose()",
        ),
        (
            "picopcm-open-rest-step",
            "picopcm.c",
            "PC_REG32(PWM_SLICE(PCM_SLICE) + PWM_CC) = 0;",
            "PC_REG32(PWM_SLICE(PCM_SLICE) + PWM_CC) = 125;",
            "PWM_CC",
        ),
    ]

    with tempfile.TemporaryDirectory(prefix="picocalc-conformance-") as tmp:
        stage = Path(tmp)
        dev_stage = stage / verify_conformance.DEV_DIR
        dev_stage.mkdir(parents=True)
        originals: dict[str, str] = {}
        for filename in verify_conformance.INSPECTED_FILES:
            src = ROOT / verify_conformance.DEV_DIR / filename
            dst = dev_stage / filename
            shutil.copy2(src, dst)
            originals[filename] = src.read_text(encoding="utf-8")

        stage_code, stage_payload = run_verifier(stage)
        assert stage_code == 0 and stage_payload["ok"] is True, (
            f"unmodified staged copy failed: {stage_payload}"
        )

        for name, filename, old, new, expected_text in mutants:
            body = originals[filename]
            assert old in body, f"mutant {name}: target text missing in {filename}"
            target_path = dev_stage / filename
            target_path.write_text(body.replace(old, new, 1), encoding="utf-8")
            mut_code, mut_payload = run_verifier(stage)
            target_path.write_text(body, encoding="utf-8")
            assert mut_code == 1 and mut_payload["ok"] is False, (
                f"mutant {name} unexpectedly passed"
            )
            joined = " ".join(str(e) for e in mut_payload["errors"])
            assert expected_text in joined, (
                f"mutant {name}: expected {expected_text!r} in {joined!r}"
            )

    print(
        f"PASS picocalc-conformance calibration: live tree + {len(mutants)} known-bad mutants "
        f"rejected"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
