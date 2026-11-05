"""Write one mutated copy of bn.c per semantic fault.

Usage: bn_mutate.py BN_C OUTPUT_DIRECTORY

Each mutation replaces text that must occur exactly once, so a refactor of
bn.c that moves or duplicates a site fails here instead of silently
producing an unmutated "mutant". The output files are bn_NAME.mutant.c.
"""

import sys
from pathlib import Path

# (name, original text, replacement)
MUTATIONS = (
    ("add_carry", "\tr[na] = (limb_t)c;\n", "\tr[na] = 0;\n"),
    (
        "sub_borrow",
        "\t\tt = (uint32_t)a[i] - borrow;\n",
        "\t\tt = (uint32_t)a[i];\n",
    ),
    (
        "mul_carry_in",
        "t = (uint32_t)a[j] * bi + r[i + j] + carry;",
        "t = (uint32_t)a[j] * bi + r[i + j];",
    ),
    (
        "div_norm_shift",
        "((uint32_t)a[i - 1] >> (LIMB_BITS - s))) & LIMB_MASK);",
        "((uint32_t)a[i - 1] >> (LIMB_BITS - s + 1))) & LIMB_MASK);",
    ),
    (
        "div_norm_top",
        "\tu[na] = (limb_t)((uint32_t)a[na - 1] >> (LIMB_BITS - s));\n",
        "\tu[na] = 0;\n",
    ),
    ("div_addback", "\t\tif (brw) {\n", "\t\tif (0) {\n"),
    (
        "div_estimate",
        "qh * v[nd - 2] > ((rh << LIMB_BITS) | u[j + nd - 2])",
        "qh * v[nd - 2] > ((rh << LIMB_BITS) | u[j + nd - 2]) + 1",
    ),
    (
        "neg_zero",
        "\tr->neg = (r->top != 0 && neg != 0);\n",
        "\tr->neg = (neg != 0);\n",
    ),
    ("rem_sign", "\t\tsettop(rem, rn, aneg);\n", "\t\tsettop(rem, rn, dneg);\n"),
    (
        "exp_top_bit",
        "for (i = BN_num_bits(p) - 2; i >= 0; i--) {",
        "for (i = BN_num_bits(p) - 3; i >= 0; i--) {",
    ),
    (
        "mul_alias",
        "if (r->d != NULL && (r->d == ad || r->d == bd)) {",
        "if (0) {",
    ),
    (
        "dec_chunk",
        "rem = mdiv1(tmp, tmp, n, 10000);",
        "rem = mdiv1(tmp, tmp, n, 9999);",
    ),
    (
        "mask_width",
        "a->d[w] &= (limb_t)((1u << b) - 1u);",
        "a->d[w] &= (limb_t)((1u << (b + 1)) - 1u);",
    ),
    (
        "rshift_carry",
        "uint32_t hi = (i + ls + 1 < a->top) ? a->d[i + ls + 1] : 0;",
        "uint32_t hi = 0;",
    ),
)


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    source = Path(sys.argv[1]).read_text(encoding="utf-8")
    out = Path(sys.argv[2])
    for name, old, new in MUTATIONS:
        count = source.count(old)
        if count != 1:
            print(f"bn_mutate: {name}: site occurs {count} times, expected 1", file=sys.stderr)
            return 1
        (out / f"bn_{name}.mutant.c").write_text(source.replace(old, new), encoding="utf-8")
    print("bn_mutate: " + " ".join(name for name, _, _ in MUTATIONS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
