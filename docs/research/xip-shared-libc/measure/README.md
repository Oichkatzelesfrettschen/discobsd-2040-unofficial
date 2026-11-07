# Relink harness and retained rows

`relink_against_libcx.sh REPO OUTDIR [union|whole]` relinks the shipped
RP2040 executables of a completed build twice from their existing objects:
static against `lib/libc.a`, and against a fixed-address libc image imported
with `--just-symbols`. It generates its libc image (`libcx.ld` plus either the
members in `libc-union-members.txt` or the whole archive), the symbol file,
the export list and the shifted linker script under OUTDIR, and prints raw,
packed and packed-block sizes per form through `elf2aout` and `hsaout -s`. A
row fails on an export collision, a residual libc.a member, or a link error.
Set `PYTHON` to the interpreter that runs `map_libc_bytes.py`.

`map_libc_bytes.py MAP NAME OUT` attributes a GNU ld map's allocatable input
bytes to libc.a members, crt0 and other inputs.

Retained rows, from build products dated 2026-10-24 and arm-none-eabi-gcc
16.2.0:

| File | Contents |
| --- | --- |
| `relink-results-union.txt` | per-executable sizes against the 257-member union image |
| `relink-results-whole.txt` | the same against the whole-archive image |
| `applet-results.txt` | each applet of five boxes linked standalone, static (`base`) and shared (`x`), packed blocks; predates the residual-member rule |
| `static-libc-bytes.txt` | linker attribution of libc bytes per executable in the static links |
| `libc-union-members.txt` | the 257 libc.a members the 30 relinked executables use |
