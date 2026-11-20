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

"""Verify quantitative hardware-conformance invariants across PicoCalc drivers."""

import argparse
import ast
import json
import re
import sys
from pathlib import Path

DEV_DIR = Path("sys/arch/rp2040/dev")

INSPECTED_FILES = (
    "picocalc.h",
    "picocalc_reg.h",
    "st7365p.c",
    "paneltest_sets.h",
    "picokbd.c",
    "picokbd_req.h",
    "picokbd_req.c",
    "sdspi.c",
    "sd.c",
    "picopcm.h",
    "picopcm.c",
)

EXPECTED_INIT_GAMMA = [
    0xE0, 14, 0xF0, 0x03, 0x05, 0x09, 0x0C, 0x0F, 0x3E, 0x77,
    0x4F, 0x0F, 0x17, 0x17, 0x21, 0x23,
    0xE1, 14, 0xF0, 0x03, 0x05, 0x09, 0x0C, 0x0F, 0x3E, 0x77,
    0x4F, 0x0F, 0x17, 0x17, 0x21, 0x23,
]

EXPECTED_INIT_UNLOCK = [0xF0, 1, 0xC3, 0xF0, 1, 0x96]
EXPECTED_INIT_LOCK = [0xF0, 1, 0x3C, 0xF0, 1, 0x69]


def strip_c_comments(text: str) -> str:
    """Remove C block and line comments while keeping string/char literals."""
    pattern = re.compile(
        r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*',
        re.DOTALL,
    )

    def replace(match: re.Match[str]) -> str:
        token = match.group(0)
        if token.startswith(("/*", "//")):
            return "\n" * token.count("\n")
        return token

    return pattern.sub(replace, text)


def parse_defines(clean: str) -> dict[str, str]:
    """Extract single- or backslash-continued #define values from C source."""
    joined = clean.replace("\\\n", " ")
    defines: dict[str, str] = {}
    for line in joined.splitlines():
        match = re.match(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+?)\s*$", line)
        if match and "(" not in match.group(1):
            defines[match.group(1)] = match.group(2)
    return defines


def safe_eval_int(expr: str, symbols: dict[str, int]) -> int:
    """Safely evaluate integer preprocessor expressions without C suffixes."""
    cleaned = re.sub(r"\b(0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]+\b", r"\1", expr)
    for name, value in sorted(symbols.items(), key=lambda kv: -len(kv[0])):
        cleaned = re.sub(rf"\b{re.escape(name)}\b", str(value), cleaned)
    cleaned = cleaned.replace("/", "//")
    node = ast.parse(cleaned, mode="eval")

    def eval_node(cur: ast.AST) -> int:
        if isinstance(cur, ast.Expression):
            return eval_node(cur.body)
        if isinstance(cur, ast.Constant) and isinstance(cur.value, int):
            return cur.value
        if isinstance(cur, ast.UnaryOp):
            val = eval_node(cur.operand)
            if isinstance(cur.op, ast.UAdd):
                return +val
            if isinstance(cur.op, ast.USub):
                return -val
        if isinstance(cur, ast.BinOp):
            left = eval_node(cur.left)
            right = eval_node(cur.right)
            if isinstance(cur.op, ast.Add):
                return left + right
            if isinstance(cur.op, ast.Sub):
                return left - right
            if isinstance(cur.op, ast.Mult):
                return left * right
            if isinstance(cur.op, ast.FloorDiv):
                return left // right
            if isinstance(cur.op, ast.LShift):
                return left << right
            if isinstance(cur.op, ast.RShift):
                return left >> right
            if isinstance(cur.op, ast.BitOr):
                return left | right
            if isinstance(cur.op, ast.BitAnd):
                return left & right
        raise ValueError(f"unsupported expression AST in {expr!r}")

    return eval_node(node)


def extract_array_tokens(clean: str, decl_pattern: str) -> list[str]:
    """Return raw comma-separated tokens from a C array initializer."""
    match = re.search(decl_pattern + r"\s*=\s*\{(.*?)\};", clean, re.DOTALL)
    if not match:
        raise ValueError(f"missing initializer for {decl_pattern}")
    return [tok.strip() for tok in match.group(1).split(",") if tok.strip()]


def extract_function_body(clean: str, func_sig_pattern: str) -> str:
    """Return body of a C function matched by header pattern up to balanced brace."""
    match = re.search(func_sig_pattern + r"\s*\{", clean, re.DOTALL)
    if not match:
        raise ValueError(f"missing function {func_sig_pattern}")
    depth = 1
    pos = match.end()
    while pos < len(clean) and depth > 0:
        ch = clean[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        pos += 1
    if depth != 0:
        raise ValueError(f"unterminated body for {func_sig_pattern}")
    return clean[match.end() : pos - 1]


def decode_command_list(bytes_list: list[int]) -> list[tuple[int, list[int]]]:
    """Parse [cmd, argc, arg0..arg_{argc-1}, ...] sequence tables."""
    commands: list[tuple[int, list[int]]] = []
    idx = 0
    while idx < len(bytes_list):
        cmd = bytes_list[idx]
        if idx + 1 >= len(bytes_list):
            raise ValueError(f"truncated command entry at index {idx}")
        argc = bytes_list[idx + 1]
        end = idx + 2 + argc
        if end > len(bytes_list):
            raise ValueError(f"truncated arguments for command {cmd:#x}")
        commands.append((cmd, bytes_list[idx + 2 : end]))
        idx = end
    return commands


def check_pins_and_regs(
    sources: dict[str, str], errors: list[str], metrics: dict[str, object]
) -> None:
    """Check PicoCalc V2.0 pin assignments and RP2040 SIO register constants."""
    pch_defs = parse_defines(sources["picocalc.h"])
    expected_pins = {
        "PICOCALC_KBD_SDA": 6,
        "PICOCALC_KBD_SCL": 7,
        "PICOCALC_KBD_ADDR": 0x1F,
        "PICOCALC_LCD_SCK": 10,
        "PICOCALC_LCD_TX": 11,
        "PICOCALC_LCD_RX": 12,
        "PICOCALC_LCD_CS": 13,
        "PICOCALC_LCD_DC": 14,
        "PICOCALC_LCD_RST": 15,
        "PICOCALC_SD_RX": 16,
        "PICOCALC_SD_CS": 17,
        "PICOCALC_SD_SCK": 18,
        "PICOCALC_SD_TX": 19,
        "PICOCALC_SD_DET": 22,
        "PICOCALC_AUDIO_L": 26,
        "PICOCALC_AUDIO_R": 27,
    }
    for name, want in expected_pins.items():
        if name not in pch_defs:
            errors.append(f"picocalc.h: missing {name}")
            continue
        got = safe_eval_int(pch_defs[name], {})
        if got != want:
            errors.append(f"picocalc.h: {name}={got}, expected {want}")

    reg_defs = parse_defines(sources["picocalc_reg.h"])
    expected_regs = {
        "PC_SIO_GPIO_IN": 0xD0000004,
        "PC_SIO_OUT_SET": 0xD0000014,
        "PC_SIO_OUT_CLR": 0xD0000018,
        "PC_SIO_OE_SET": 0xD0000024,
        "PC_SIO_OE_CLR": 0xD0000028,
    }
    for name, want in expected_regs.items():
        if name not in reg_defs:
            errors.append(f"picocalc_reg.h: missing {name}")
            continue
        got = safe_eval_int(reg_defs[name], {})
        if got != want:
            errors.append(f"picocalc_reg.h: {name}={got:#x}, expected {want:#x}")
    metrics["pin_count"] = len(expected_pins)


def check_display(
    sources: dict[str, str], errors: list[str], metrics: dict[str, object]
) -> None:
    """Check ST7365P native initialization sequence, Command 2 lock, and sets."""
    st = sources["st7365p.c"]
    st_defs = parse_defines(st)
    sym: dict[str, int] = {}
    for key in ("CMD_CSCON", "CMD_MADCTL", "CMD_COLMOD", "CMD_INVON",
                "MADCTL_MX_BGR", "COLMOD_RGB565"):
        if key in st_defs:
            sym[key] = safe_eval_int(st_defs[key], sym)

    gamma = [safe_eval_int(t, sym) for t in extract_array_tokens(st, r"init_gamma\[\]")]
    if gamma != EXPECTED_INIT_GAMMA:
        errors.append(
            f"st7365p.c: init_gamma does not match 14-byte ST7365P defaults (len={len(gamma)})"
        )

    seq = [safe_eval_int(t, sym) for t in extract_array_tokens(st, r"init_seq\[\]")]
    cmds = decode_command_list(seq)
    cmd_map = {cmd: args for cmd, args in cmds}
    if 0xF7 in cmd_map:
        errors.append("st7365p.c: init_seq still contains undefined ILI9488P command 0xf7")
    expected_native = {
        0xC0: [0x80, 0x25],
        0xC1: [0x13],
        0xC2: [0xA5],
        0xC5: [0x1C],
        0x36: [0x48],
        0x3A: [0x55],
        0xB0: [0x00],
        0x21: [],
        0xB4: [0x02],
        0xB6: [0x80, 0x02, 0x3B],
        0xB7: [0xC6],
    }
    for opcode, want_args in expected_native.items():
        if cmd_map.get(opcode) != want_args:
            errors.append(
                f"st7365p.c: init_seq opcode {opcode:#04x}={cmd_map.get(opcode)}, "
                f"expected {want_args}"
            )

    unlock = [safe_eval_int(t, sym) for t in extract_array_tokens(st, r"init_unlock\[\]")]
    lock = [safe_eval_int(t, sym) for t in extract_array_tokens(st, r"init_lock\[\]")]
    if unlock != EXPECTED_INIT_UNLOCK:
        errors.append(f"st7365p.c: init_unlock={unlock}, expected {EXPECTED_INIT_UNLOCK}")
    if lock != EXPECTED_INIT_LOCK:
        errors.append(f"st7365p.c: init_lock={lock}, expected {EXPECTED_INIT_LOCK}")

    init_body = extract_function_body(st, r"void\s+st7365p_init\(void\)")
    expected_order = [
        "run_seq(init_unlock",
        "run_seq(init_gamma",
        "run_seq(init_seq",
        "run_seq(init_frame",
        "run_seq(init_lock",
        "panel_wake()",
    ]
    positions = [init_body.find(token) for token in expected_order]
    if any(p < 0 for p in positions) or positions != sorted(positions):
        errors.append("st7365p.c: st7365p_init() missing ordered unlock..lock..wake sequence")

    sets_hdr = sources["paneltest_sets.h"]
    entries = re.findall(
        r'X\(\s*"([^"]+)"\s*,\s*([A-Z0-9_]+)\s*,\s*([A-Z0-9_]+)\s*,\s*([A-Z0-9_]+)\s*\)',
        sets_hdr,
    )
    if len(entries) != 33:
        errors.append(f"paneltest_sets.h: expected 33 sets (0..32), found {len(entries)}")
    else:
        if entries[0] != ("shipped", "UNLOCK", "NATIVE", "FRAME_78HZ"):
            errors.append(f"paneltest_sets.h: set 0={entries[0]}, expected NATIVE boot set")
        if entries[32] != ("legacy-ili9488-78hz", "UNLOCK", "DRIVER", "FRAME_78HZ"):
            errors.append(
                f"paneltest_sets.h: set 32={entries[32]}, expected legacy-ili9488-78hz"
            )
    metrics["st7365p_init_commands"] = len(cmds)
    metrics["paneltest_sets"] = len(entries)


def check_keyboard(
    sources: dict[str, str], errors: list[str], metrics: dict[str, object]
) -> None:
    """Check I2C1 100.00 kHz timing equations, 9-clock bus clear, and steps."""
    kbd = sources["picokbd.c"]
    defs = parse_defines(kbd)
    sym: dict[str, int] = {"CPU_KHZ": 125000}
    for name in (
        "KBD_I2C_HZ",
        "KBD_I2C_PERIOD",
        "KBD_I2C_SPKLEN",
        "KBD_I2C_LCNT",
        "KBD_I2C_HCNT",
        "KBD_I2C_SDA_HOLD",
    ):
        if name not in defs:
            errors.append(f"picokbd.c: missing {name}")
            return
        sym[name] = safe_eval_int(defs[name], sym)

    spklen_ns = sym["KBD_I2C_SPKLEN"] * 8
    t_low_ns = (sym["KBD_I2C_LCNT"] + 1) * 8
    t_high_ns = (sym["KBD_I2C_HCNT"] + sym["KBD_I2C_SPKLEN"] + 7) * 8
    period_ns = t_low_ns + t_high_ns
    sda_hold_ns = sym["KBD_I2C_SDA_HOLD"] * 8

    if sym["KBD_I2C_HZ"] != 100000:
        errors.append(f"picokbd.c: KBD_I2C_HZ={sym['KBD_I2C_HZ']}, expected 100000")
    if spklen_ns > 50 or sym["KBD_I2C_SPKLEN"] < 1:
        errors.append(
            f"picokbd.c: SPKLEN={sym['KBD_I2C_SPKLEN']} ({spklen_ns} ns) exceeds 50 ns"
        )
    if t_low_ns < 4700:
        errors.append(f"picokbd.c: t_LOW={t_low_ns} ns < 4700 ns minimum")
    if t_high_ns < 4000:
        errors.append(f"picokbd.c: t_HIGH={t_high_ns} ns < 4000 ns minimum")
    if period_ns != 10000:
        errors.append(f"picokbd.c: SCL period={period_ns} ns != 10000 ns (100.00 kHz)")
    if sda_hold_ns < 300:
        errors.append(f"picokbd.c: t_SDA_HOLD={sda_hold_ns} ns < 300 ns")
    if "_Static_assert(" not in kbd:
        errors.append("picokbd.c: missing compile-time _Static_assert checks")

    clear_body = extract_function_body(kbd, r"static void\s+i2c_bus_clear\(void\)")
    for token in ("PC_SIO_OE_CLR", "PC_SIO_OUT_CLR", "PC_SIO_GPIO_IN", "PC_SIO_OE_SET", "i < 9"):
        if token not in clear_body:
            errors.append(f"picokbd.c: i2c_bus_clear() missing {token}")
    for caller_sig in (
        r"void\s+picokbd_init\(void\)",
        r"static void\s+i2c_restart\(void\)",
        r"static void\s+recover_reset\(void\)",
    ):
        body = extract_function_body(kbd, caller_sig)
        if "i2c_bus_clear();" not in body:
            errors.append(f"picokbd.c: {caller_sig} does not invoke i2c_bus_clear()")

    sysctl_body = extract_function_body(kbd, r"int\s+picocalc_sysctl\([^)]*\)")
    if "kbdreq_level_for(" not in sysctl_body:
        errors.append("picokbd.c: picocalc_sysctl() does not invoke kbdreq_level_for()")

    req_defs = parse_defines(sources["picokbd_req.h"])
    expected_steps = {
        "KBD_LCD_STEP": 16,
        "KBD_LCD_MIN": 16,
        "KBD_LCD_MAX": 240,
        "KBD_KBD_STEP": 32,
        "KBD_KBD_MAX": 224,
    }
    for name, want in expected_steps.items():
        if name not in req_defs:
            errors.append(f"picokbd_req.h: missing {name}")
            continue
        got = safe_eval_int(req_defs[name], {})
        if got != want:
            errors.append(f"picokbd_req.h: {name}={got}, expected {want}")
    if "kbdreq_level_for(" not in sources["picokbd_req.c"]:
        errors.append("picokbd_req.c: missing kbdreq_level_for()")

    metrics["i2c_scl_hz"] = 1_000_000_000 // period_ns
    metrics["i2c_spklen_ns"] = spklen_ns
    metrics["i2c_t_low_ns"] = t_low_ns
    metrics["i2c_t_high_ns"] = t_high_ns


def check_sd(sources: dict[str, str], errors: list[str], metrics: dict[str, object]) -> None:
    """Check SD SPI N_CR window, CSD v2.0 clamp, PL022 BSY wait, and SD_DET."""
    sdspi = sources["sdspi.c"]
    sdspi_defs = parse_defines(sdspi)
    if safe_eval_int(sdspi_defs.get("NCR_MAX", "-1"), {}) != 8:
        errors.append("sdspi.c: NCR_MAX must equal 8")
    cmd_body = extract_function_body(
        sdspi, r"static unsigned char\s+sd_cmd\([^)]*\)"
    )
    if not re.search(r"\bi\s*<=\s*NCR_MAX\b", cmd_body):
        errors.append("sdspi.c: sd_cmd() must loop with i <= NCR_MAX for 0..8 byte N_CR")
    csd_body = extract_function_body(
        sdspi, r"unsigned\s+sdspi_csd_sectors\([^)]*\)"
    )
    if "0x3fffff" not in csd_body.lower():
        errors.append("sdspi.c: sdspi_csd_sectors() missing 0x3fffffU CSD v2.0 overflow guard")

    sd = sources["sd.c"]
    sd_defs = parse_defines(sd)
    if "SSPSR_BSY" not in sd_defs or safe_eval_int(sd_defs["SSPSR_BSY"], {}) != 16:
        errors.append("sd.c: SSPSR_BSY must equal (1UL << 4)")
    speed_body = extract_function_body(sd, r"static void\s+sd_speed\(int fast\)")
    bsy_pos = speed_body.find("SSPSR_BSY")
    sse_clr_pos = speed_body.find("SPI_REG(SSPCR1) = 0;")
    if bsy_pos < 0 or sse_clr_pos < 0 or bsy_pos > sse_clr_pos:
        errors.append("sd.c: sd_speed() must poll SSPSR_BSY before clearing SSPCR1")

    if "SD_DET_PRESENT" not in sd_defs or safe_eval_int(sd_defs["SD_DET_PRESENT"], {}) != 0:
        errors.append("sd.c: SD_DET_PRESENT must equal 0 (active-low J303 TF-03A)")
    present_body = extract_function_body(sd, r"static int\s+sd_card_present\(void\)")
    if "sd_det_pin() == SD_DET_PRESENT" not in present_body:
        errors.append("sd.c: sd_card_present() must compare sd_det_pin() == SD_DET_PRESENT")
    setup_body = extract_function_body(sd, r"static int\s+sd_setup\(void\)")
    chk_pos = setup_body.find("!sd_card_present()")
    init_pos = setup_body.find("sdspi_init(")
    if chk_pos < 0 or init_pos < 0 or chk_pos > init_pos or "ENODEV" not in setup_body[:init_pos]:
        errors.append("sd.c: sd_setup() must fast-fail !sd_card_present() with ENODEV")
    metrics["sd_ncr_max"] = 8
    metrics["sd_det_present"] = 0


def check_audio(sources: dict[str, str], errors: list[str], metrics: dict[str, object]) -> None:
    """Check PicoCalc PWM audio open/close DC-bias ramp and closing zero hold."""
    pcm_h_defs = parse_defines(sources["picopcm.h"])
    for name, want in (
        ("PCM_RATE", 8000),
        ("PCM_RING", 256),
        ("PCM_LEVELS", 250),
        ("PCM_SILENCE", 0x80),
    ):
        if name not in pcm_h_defs or safe_eval_int(pcm_h_defs[name], {}) != want:
            errors.append(f"picopcm.h: {name} must equal {want}")

    pcm_c = sources["picopcm.c"]
    if "pcm_closing" not in pcm_c:
        errors.append("picopcm.c: missing pcm_closing state variable")
    isr_body = extract_function_body(pcm_c, r"void\s+PWM_IRQ_WRAP_Handler\(void\)")
    if "pcm_closing" not in isr_body or "pcm_fill(&pcm) == 0" not in isr_body:
        errors.append("picopcm.c: PWM_IRQ_WRAP_Handler must hold 0 during pcm_closing empty ring")
    if not re.search(r"static void\s+pcm_ramp\(", pcm_c):
        errors.append("picopcm.c: missing pcm_ramp() helper")

    open_body = extract_function_body(pcm_c, r"int\s+pcmopen\([^)]*\)")
    if not re.search(r"PWM_CC\)\s*=\s*0;", open_body):
        errors.append("picopcm.c: pcmopen() must initialize slice 5 PWM_CC to 0")
    if "pcm_ramp(1, PCM_SILENCE);" not in open_body:
        errors.append("picopcm.c: pcmopen() must invoke pcm_ramp(1, PCM_SILENCE);")

    close_body = extract_function_body(pcm_c, r"int\s+pcmclose\([^)]*\)")
    ramp_pos = close_body.find("pcm_ramp(PCM_SILENCE - 1, 0);")
    mute_pos = close_body.find("picoaudio_mute();")
    if "pcm_closing = 1;" not in close_body or ramp_pos < 0 or mute_pos < 0 or ramp_pos > mute_pos:
        errors.append("picopcm.c: pcmclose() must sweep PCM_SILENCE - 1..0 before picoaudio_mute()")
    if "if (pcm_fill(&pcm) == 0)" not in close_body[:mute_pos]:
        errors.append("picopcm.c: pcmclose() must guard close ramp on empty drained ring")
    metrics["audio_ramp_samples"] = 128


def verify_root(root: Path) -> tuple[bool, list[str], dict[str, object]]:
    """Run all hardware-conformance checks against root."""
    errors: list[str] = []
    metrics: dict[str, object] = {}
    sources: dict[str, str] = {}
    dev = root / DEV_DIR
    for name in INSPECTED_FILES:
        path = dev / name
        if not path.is_file():
            errors.append(f"missing source file: {path}")
            continue
        sources[name] = strip_c_comments(path.read_text(encoding="utf-8"))

    if len(sources) == len(INSPECTED_FILES):
        for check in (check_pins_and_regs, check_display, check_keyboard, check_sd, check_audio):
            try:
                check(sources, errors, metrics)
            except ValueError as err:
                errors.append(str(err))
    return (not errors), errors, metrics


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="Repository root to verify",
    )
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON verdict")
    args = parser.parse_args()
    ok, errors, metrics = verify_root(args.root.resolve())
    if args.json:
        payload = {"ok": ok, "errors": errors, "metrics": metrics}
        print(json.dumps(payload, indent=2, sort_keys=True))
    elif ok:
        print(
            "PASS picocalc-conformance: pins=%d st7365p_cmds=%d sets=%d "
            "i2c_hz=%d spklen=%dns sd_ncr=%d audio_ramp=%d"
            % (
                metrics["pin_count"],
                metrics["st7365p_init_commands"],
                metrics["paneltest_sets"],
                metrics["i2c_scl_hz"],
                metrics["i2c_spklen_ns"],
                metrics["sd_ncr_max"],
                metrics["audio_ramp_samples"],
            )
        )
    else:
        for err in errors:
            print(f"FAIL picocalc-conformance: {err}", file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
