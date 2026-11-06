# ruff: noqa: E501
# flake8: noqa: E501
"""Case definitions for the dc and bc host gate.

Three kinds of case share one record:

  - explicit: the expected stdout is computed here, from Python integers or
    from a model of the register table, so the oracle is independent of any
    dc or bc source;
  - golden: the expected rc, stdout and stderr come from golden.json, which
    gen_golden.sh writes by running the pinned OpenBSD dc and bc linked
    against libcrypto and libedit;
  - corpus: golden cases made of random operator soup, the generator of
    tests/libbn/dc-differential/gen_corpus.py at a fixed seed.

A case is a dict with: name, kind ("dc" or "bc"), args, stdin (bytes),
optional files (name -> bytes), optional expect (bytes, explicit cases), and
tier ("base" or "corpus").
"""

import random

# Corpus programs whose pinned-source run exceeds the generator's time bound
# (a number raised to a huge exponent); filled in by gen_golden.sh output.
CORPUS_SKIP = frozenset({20, 24, 32, 39, 67, 96})
CORPUS_SIZE = 300


def _wrap(text):
    """dc's output wrapping (putcharwrap in inout.c).

    A line holds 68 characters and then a backslash; because the last
    character is held back until the next arrives, a final line may hold 69.
    """
    out = []
    while len(text) > 69:
        out.append(text[:68] + "\\\n")
        text = text[68:]
    out.append(text + "\n")
    return "".join(out)


def _dec(n):
    return _wrap(str(n))


def _tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def _trem(a, b):
    return a - b * _tdiv(a, b)


def _isqrt(n):
    import math

    return math.isqrt(n)


def _dcnum(n):
    return ("_" + str(-n)) if n < 0 else str(n)


def _case(kind, name, stdin, args=(), expect=None, tier="base", files=None):
    if isinstance(stdin, str):
        stdin = stdin.encode("latin-1")
    if isinstance(expect, str):
        expect = expect.encode("latin-1")
    return {
        "name": kind + "." + name,
        "kind": kind,
        "args": list(args),
        "stdin": stdin,
        "expect": expect,
        "tier": tier,
        "files": files or {},
    }


def arithmetic_cases():
    """Scale-zero integer arithmetic against Python integers."""
    rng = random.Random(0x44434243)
    cases = []
    sizes = [1, 2, 5, 9, 10, 19, 20, 38, 77, 150, 400]
    for i in range(110):
        a = rng.getrandbits(rng.choice(sizes) * 3 + 1) * rng.choice([1, -1])
        b = rng.getrandbits(rng.choice(sizes) * 3 + 1) * rng.choice([1, -1])
        op = rng.choice("+-*/%~^v")
        if op == "^":
            a = rng.randrange(-50, 50)
            b = rng.randrange(0, 40)
            exp = a**b
            prog = "%s %s ^ p" % (_dcnum(a), _dcnum(b))
            out = _dec(exp)
        elif op == "v":
            a = abs(a)
            prog = "%s v p" % _dcnum(a)
            out = _dec(_isqrt(a))
        elif op in "/%~":
            if b == 0:
                b = 7
            if op == "/":
                out = _dec(_tdiv(a, b))
                prog = "%s %s / p" % (_dcnum(a), _dcnum(b))
            elif op == "%":
                out = _dec(_trem(a, b))
                prog = "%s %s %% p" % (_dcnum(a), _dcnum(b))
            else:
                out = _dec(_trem(a, b)) + _dec(_tdiv(a, b))
                prog = "%s %s ~ f" % (_dcnum(a), _dcnum(b))
        else:
            r = {"+": a + b, "-": a - b, "*": a * b}[op]
            prog = "%s %s %s p" % (_dcnum(a), _dcnum(b), op)
            out = _dec(r)
        cases.append(_case("dc", "arith%03d" % i, prog + "\n", expect=out))
    for base, fmt in ((2, "{:b}"), (8, "{:o}"), (16, "{:X}")):
        for j in range(6):
            n = rng.getrandbits(rng.choice([8, 31, 32, 33, 64, 200]))
            if j == 0:
                n = 0
            body = fmt.format(n)
            if n == 0:
                body = "0"
            out = _wrap(body)
            cases.append(
                _case(
                    "dc",
                    "obase%d_%d" % (base, j),
                    "%d o %d p\n" % (base, n),
                    expect=out,
                )
            )
    return cases


def _ext(idx):
    """The bytes naming register idx in a dc -x program."""
    if idx < 255:
        return bytes([idx])
    idx -= 256
    return bytes([0xFF, idx >> 8, idx & 0xFF])


def register_cases():
    """The register table against a model: values, stacks and arrays."""
    rng = random.Random(0x52454753)
    cases = []
    edge = [0, 1, 36, 254, 256, 257, 511, 512, 513, 4096, 65535, 65536, 65790, 65791]
    sample = sorted(set(edge + [rng.randrange(1, 65792) for _ in range(100)]))
    sample = [r for r in sample if r != 255]
    values = {r: rng.randrange(1, 10**12) for r in sample}

    # Store a distinct value in every sampled register, read each back.
    prog = bytearray()
    for r in sample:
        prog += ("%d s" % values[r]).encode() + _ext(r) + b"\n"
    for r in sample:
        prog += b"l" + _ext(r) + b" p\n"
    out = "".join(_dec(values[r]) for r in sample)
    cases.append(_case("dc", "reg_ext_values", bytes(prog), ["-x"], out))

    # Read back in reverse order and read neighbors that were never written.
    untouched = [r for r in (2, 255 + 1 + 9, 700, 20000, 65000) if r not in values]
    prog = bytearray()
    for r in sample[::2]:
        prog += ("%d s" % values[r]).encode() + _ext(r) + b"\n"
    out = ""
    for r in reversed(sample[::2]):
        prog += b"l" + _ext(r) + b" p\n"
        out += _dec(values[r])
    for r in untouched:
        if r in sample[::2]:
            continue
        prog += b"l" + _ext(r) + b" p\n"
        out += "0\n"
    cases.append(_case("dc", "reg_ext_neighbors", bytes(prog), ["-x"], out))

    # Stack registers: push three values on each of several registers, pop.
    regs = [3, 256, 300, 5000, 65791]
    prog = bytearray()
    out = ""
    for r in regs:
        for k in (11, 22, 33):
            prog += ("%d S" % (k + r)).encode() + _ext(r) + b"\n"
    for r in regs:
        for k in (33, 22, 11):
            prog += b"L" + _ext(r) + b" p\n"
            out += _dec(k + r)
    cases.append(_case("dc", "reg_ext_stacks", bytes(prog), ["-x"], out))

    # Arrays in extended registers; a register's array and plain value are
    # separate.
    prog = bytearray()
    out = ""
    for n, r in enumerate(regs):
        prog += ("%d s" % (1000 + n)).encode() + _ext(r) + b"\n"
        for i in (0, 1, 7, 2048):
            prog += ("%d %d :" % (r + i, i)).encode() + _ext(r) + b"\n"
    for n, r in enumerate(regs):
        prog += b"l" + _ext(r) + b" p\n"
        out += _dec(1000 + n)
        for i in (0, 1, 7, 2048):
            prog += ("%d ;" % i).encode() + _ext(r) + b" p\n"
            out += _dec(r + i)
    cases.append(_case("dc", "reg_ext_arrays", bytes(prog), ["-x"], out))

    # Without -x the byte 0xff names register 255 and a later byte is a
    # command, so a three-byte extended name is not a register.
    cases.append(_case("dc", "reg_small_255", b"5 s\xffl\xff p 6 S\xffL\xff p\n", [], "5\n6\n"))
    cases.append(
        _case(
            "dc",
            "reg_small_all",
            b"".join(b"%d s" % (c + 1) + bytes([c]) for c in range(1, 256))
            + b"\n0"
            + b"".join(b" l" + bytes([c]) + b" +" for c in range(1, 256))
            + b" p\n",
            [],
            _dec(sum(range(2, 257))),
        )
    )

    # Every one of the 257 pages is touched: one register per page, with the
    # low byte of its index varying so a page-relative slot error shows.
    pages = []
    for page in range(257):
        r = page * 256 + (page * 37) % 256
        if r != 255 and r <= 65791:
            pages.append((r, page + 1))
    prog = bytearray()
    for r, v in pages:
        prog += ("%d s" % v).encode() + _ext(r) + b"\n"
    prog += b"l" + _ext(pages[0][0])
    for r, _ in pages[1:]:
        prog += b" l" + _ext(r) + b" +"
    prog += b" p\n"
    cases.append(
        _case(
            "dc",
            "reg_ext_all_pages",
            bytes(prog),
            ["-x"],
            _dec(sum(v for _, v in pages)),
        )
    )
    return cases


DC_PROGRAMS = (
    ("scale_div", "10 k 2 v p 1 3 / p 20 k 1 3 / p 0 k 7 2 / p"),
    ("neg_exp", "2 3 ^ p 2 _3 ^ p 5 k 2 _3 ^ p 4 _2 ^ p"),
    ("divmod_signs", "_7 3 ~ f c 7 _3 ~ f c _7 _3 ~ f c 7 _3 % p _7 3 % p"),
    ("fraction_mul", "12345.6789 3 k 0.1 * p 2 k 3.14159 2 * p"),
    ("strings", "[hello]P [ world]p [abc]Z p 1234 Z p 12.345 X p 12.345 Z p"),
    ("ascii", "97 a P [abc]a p 65 a p 256 a p 1000 a p"),
    ("fact_macro", "[d1=qd1-lfx*]sf [q]sq 20 lfx p"),
    ("count_macro", "0sa [la1+dsa 10 >f]sf lfx la p"),
    ("fib_loop", "0sa 1sb 0sc [lc1+sc la lb + lb sa sb lc 90 >f]sf lfx lb p"),
    ("gcd_macro", "1071 462 [dSarLa%d0<a]dsax+ p"),
    ("array_squares", "0si [li li * li :a li 1 + si li 20 >f]sf lfx 7;a p 19;a p"),
    ("nested_macro", "[[nested]p]sa [la x la x]sb lb x"),
    ("quit_levels", "[1p q]x 2p"),
    ("quit_n", "[[3 Q] x 4p] x 5p"),
    ("cond_ops", "1 2 >a 2 1 >a [yes]sa 1 2 <a 2 2 =a 3 2 !=a 1 2 !<a 1 2 !>a"),
    ("cond_else", "[yes]sa [no]sb 1 2 >a e b"),
    ("stack_ops", "1 2 3 d f r f R f z p c z p K p I p O p"),
    ("input_line", "? p\n41\n"),
    ("input_in_macro", "[?]x p p\n7\n8\n"),
    ("comment", "1 2 + # ignore this + 3\np"),
    ("ibase", "16i FF p 10 i 2 i 101 p 10 i A i"),
    ("big_pow", "2 1000 ^ p"),
    ("pi_digits", "60 k 22 7 / p 2 v p 3 v p 0.5 v p 144 v p"),
    ("div_zero", "1 0 / p 1 0 % p 1 0 ~ f"),
    ("sqrt_neg", "_4 v p"),
    ("empty_stack", "+ p - * / p d r"),
    ("unknown_op", "1 2 y 3 p"),
    ("bad_bases", "1 i 17 i 0 o 1 o _5 o 2 o 10 p"),
    ("modexp", "7 560 561 | p 4 13 497 | p"),
    ("scale_ops", "5 k 1.5 2.25 * p 1.5 2.25 + p 1 3 / p 7 k 2 v p 0 k 2 v p"),
    ("clear_reg", "5 sa 6 sa la p 7 Sa La p La p"),
    ("string_exec", "[2 3 +]x p [[nest]x]x"),
    ("print_n", "1 n 2 n [a]n 3 p"),
    ("print_P_num", "16705 P 10 P"),
    ("array_deep", "1 0:a 2 1:a 3 2:a 0;a 1;a 2;a + + p 5000;a p _1;a p"),
    ("neg_idx", "5 _1:a 5 5000:a 5 2048:a 2048;a p"),
    ("obase_wide", "100 o 12345 p 1000 o 123456789 p 17 o 5 p 17 o 100 p"),
    ("obase_neg_frac", "16 o _255.5 p 2 o 5.75 p 5 k 10 o 1 7 / p"),
    ("count_digits", "1000000 Z p 0 Z p 0.001 Z p 123.456 Z p _12345 Z p"),
    ("long_line", "99999999999999999999 d * d * d * d * d * p"),
)


def dc_program_cases():
    return [_case("dc", name, prog + "\n") for name, prog in DC_PROGRAMS]


def dc_argument_cases():
    c = []
    c.append(_case("dc", "arg_e", "", ["-e", "2 3 + p"]))
    c.append(_case("dc", "arg_e_twice", "", ["-e", "2 sa", "-e", "la p"]))
    c.append(_case("dc", "arg_e_then_stdin", "3 p\n", ["-e", "2 p"]))
    c.append(_case("dc", "arg_x_flag", "5 s\xff\x01\x02 l\xff\x01\x02 p\n", ["-x"]))
    c.append(_case("dc", "arg_dash", "4 p\n", ["-"], tier="host"))
    c.append(_case("dc", "arg_bad_option", "", ["-z"], tier="host"))
    c.append(_case("dc", "arg_two_files", "", ["a", "b"]))
    c.append(_case("dc", "arg_missing_file", "", ["no-such-file"]))
    c.append(_case("dc", "arg_directory", "", ["."], tier="host"))
    c.append(
        _case(
            "dc",
            "arg_file",
            "",
            ["prog.dc"],
            files={"prog.dc": b"2 1000 ^ Z p [done]P\n"},
        )
    )
    c.append(
        _case(
            "dc",
            "arg_file_no_stdin",
            "9 p\n",
            ["prog.dc"],
            files={"prog.dc": b"1 p\n"},
        )
    )
    return c


def corpus_cases():
    rng = random.Random(20261105)

    def num():
        k = rng.choice([1, 1, 2, 3, 8, 20, 60])
        n = rng.randrange(0, 10**k)
        s = str(n)
        if rng.random() < 0.3:
            s = s + "." + str(rng.randrange(0, 10 ** rng.choice([1, 3, 8])))
        return ("_" if rng.random() < 0.4 else "") + s

    ops = ["+", "-", "*", "/", "%", "~", "^", "v", "Z", "X", "p", "f", "c", "d", "r"]
    ops += ["o", "i", "a", "P", "|", "k"]
    bases = ["_2", "_1", "0", "1", "2", "3", "8", "10", "16", "36", "100", "_100"]
    bases += ["65536", "4294967297"]
    out = []
    for i in range(CORPUS_SIZE):
        toks = []
        for _ in range(rng.randrange(2, 12)):
            if rng.random() < 0.55:
                toks.append(num())
            else:
                op = rng.choice(ops)
                if op in ("o", "i"):
                    toks += [rng.choice(bases), op]
                elif op == "k":
                    toks += [rng.choice(["0", "5", "20", "_3"]), "k"]
                elif op == "|":
                    toks += [num(), num(), num(), "|"]
                else:
                    toks.append(op)
        toks.append("f")
        if i in CORPUS_SKIP:
            continue
        out.append(_case("dc", "corpus%03d" % i, " ".join(toks) + "\n", tier="corpus"))
    return out


BC_PROGRAMS = (
    ("arith", "1+2\n3*4\n10/3\n10%3\n2^10\n-5+2\n7-9\n"),
    ("scale_div", "scale=5\n1/3\n2/3\nscale=0\n10/4\nscale=20\n1/7\n"),
    ("scale_read", "scale\nscale=7\nscale\n1/3\nscale\n"),
    ("scale_mul", "scale=2\n1.234*5.678\n1.5*1.5\nscale=10\n3.14159*2\n"),
    ("pow_neg", "scale=10\n2^(-3)\n2^3\n(-2)^3\n(-2)^4\n"),
    ("sqrt", "scale=30\nsqrt(2)\nsqrt(144)\nsqrt(0.25)\nscale=0\nsqrt(99)\n"),
    ("length", "length(12345)\nlength(123.456)\nscale(123.456)\nscale(0.001)\n"),
    ("assign_ops", "a=5\na+=3\na\na*=2\na\na/=4\na\na%=3\na\na^=3\na\na-=1\na\n"),
    ("incr", "a=1\n++a\na++\na\n--a\na--\na\n"),
    ("compare", "1<2\n2<1\n1==1\n1!=1\n2>=2\n1>=2\n1<=1\n!0\n!5\n"),
    ("logic", "1&&1\n1&&0\n0||0\n0||3\n!(1&&0)\n"),
    ("if_else", "a=3\nif (a>2) 10 else 20\nif (a>5) 10 else 20\nif (a==3) { 1; 2 }\n"),
    ("while", "i=0\nwhile (i<5) { i*i; i+=1 }\ni\n"),
    ("for_loop", "for (i=0;i<6;i++) { i*2 }\nfor (;;) { break }\n"),
    ("break_continue", "for (i=0;i<10;i++) { if (i==3) continue; if (i==7) break; i }\n"),
    ("nested_break", "for (i=0;i<3;i++) { for (j=0;j<5;j++) { if (j==2) break; i*10+j } }\n"),
    (
        "function",
        "define f(n) {\n if (n<2) return (1)\n return (n*f(n-1))\n}\nf(10)\nf(25)\n",
    ),
    (
        "function_auto",
        "define g(a,b) {\n auto c\n c=a*b\n return (c+1)\n}\ng(6,7)\nc\ng(2,3)\n",
    ),
    (
        "function_void",
        'define p(x) {\n print x, "\\n"\n}\np(5)\np(1+1)\n',
    ),
    ("fib_recursive", "define f(n) {\n if (n<2) return (n)\n return (f(n-1)+f(n-2))\n}\nf(15)\n"),
    (
        "arrays",
        "a[0]=1\na[1]=2\na[2]=a[0]+a[1]\na[2]\na[5]\nb[3]=7\nb[3]*a[2]\n",
    ),
    (
        "array_sum",
        "define s(n) {\n auto i, t\n for (i=0;i<n;i++) t+=x[i]\n return (t)\n}\nfor (i=0;i<10;i++) x[i]=i*i\ns(10)\n",
    ),
    ("strings", '"hello\\n"\n"a"\n"tab\\there\\n"\nprint "x", 1+1, "y\\n"\n'),
    ("ibase_obase", "obase=16\n255\nobase=2\n10\nobase=10\nibase=16\nFF\nibase=A\n10\n"),
    ("obase_frac", "scale=10\nobase=16\n3.14159\nobase=2\n0.5\n"),
    ("last", "5\n.\n. + 1\nlast*2\n"),
    ("comments", "1+1 /* two */\n2*2 # four\n/* multi\nline */ 3\n"),
    ("continuation", "1+\\\n2\n10 *\\\n3\n"),
    ("semicolons", "a=1;b=2;a+b;a*b\n"),
    ("big_numbers", "2^200\n3^100\n(2^200)*(3^100)\n(2^200)/(3^50)\n"),
    ("big_fraction", "scale=60\n1/3\n2/7\n10^30/7\n"),
    ("negative_scale_div", "scale=3\n-7/2\n7/-2\n-7/-2\n-7%2\n7%-2\n"),
    ("mod_scale", "scale=5\n7.5%2\n-7.5%2\n10%3.3\n"),
    ("div_zero", "1/0\n1%0\nscale=2\n5/0\n"),
    ("sqrt_negative", "sqrt(-4)\n"),
    ("undefined_function", "foo(3)\n"),
    ("syntax_error", "1+\n2 3 4 )\n5\n"),
    ("break_outside", "break\ncontinue\n7\n"),
    ("return_outside", "return (3)\n8\n"),
    ("quit_early", "1\nquit\n2\n"),
    ("halt_in_func", "define f(x) {\n return (x)\n}\nf(1)\nquit\nf(5)\n9\n"),
    (
        "scale_func",
        "define r(x) {\n auto s\n s=scale\n scale=2\n x=x/1\n scale=s\n return (x)\n}\nr(3.14159)\nr(2.71828)\n",
    ),
    (
        "scale_saved",
        "scale=3\ndefine f(x) {\n auto s\n s=scale\n scale=0\n x=x/3\n scale=s\n return (x)\n}\nf(10)\n10/3\n",
    ),
    ("long_names", "alpha=3\nbeta=4\nalpha_beta=alpha*beta\nalpha_beta\nx1y2=alpha_beta+1\nx1y2\n"),
    ("name_kinds", "xx=1\nxx[0]=2\ndefine xx(a) {\n return (a+3)\n}\nxx\nxx[0]\nxx(10)\n"),
    ("single_names", "a=1;b=2;c=3;d=4;e=5;f=6;g=7;h=8;i=9;j=10;a+b+c+d+e+f+g+h+i+j\n"),
    ("array_names", "a[1]=1;b[1]=2;z[1]=26;a[1]+b[1]+z[1]\n"),
    ("exp_big_precision", "scale=40\n2^0.5\n"),
    ("compare_chain", 'a=5\nif (a>3 && a<10) "in\\n"\nif (a<3 || a>10) "out\\n"\n'),
    ("while_break", "i=0\nwhile (1) { i+=1; if (i>6) break }\ni\n"),
    (
        "nested_functions",
        "define a(x) {\n return (x+1)\n}\ndefine b(x) {\n return (a(x)*2)\n}\ndefine c(x) {\n return (b(a(x)))\n}\nc(3)\n",
    ),
    (
        "param_shadow",
        "x=10\ndefine f(x) {\n x=x+1\n return (x)\n}\nf(1)\nx\n",
    ),
    (
        "recursion_array",
        "define f(n) {\n auto a[]\n a[0]=n\n if (n>0) f(n-1)\n return (a[0])\n}\nf(5)\n",
    ),
    ("else_if", 'a=2\nif (a==1) "one\\n" else if (a==2) "two\\n" else "many\\n"\n'),
    ("empty_lines", "\n\n1\n\n\n2\n"),
    ("exponent_assoc", "2^3^2\n(2^3)^2\n-2^2\n"),
    ("unary_minus", "-3\n- 3\n-(-3)\n2*-3\n"),
    ("decimal_input", ".5+.5\n0.1+0.2\n1.\n.\n"),
    ("hex_digits", "ibase=16\nA+B\nFFFF\nibase=10\n"),
)

BC_LIB_PROGRAMS = (
    ("lib_trig", "scale=20\ns(1)\nc(1)\na(1)\n4*a(1)\ns(0)\nc(0)\na(0)\n"),
    ("lib_exp_log", "scale=20\ne(1)\nl(10)\nl(e(3))\ne(0)\ne(-1)\ne(10)\n"),
    ("lib_bessel", "scale=20\nj(0,1)\nj(1,2)\nj(2,3)\n"),
    ("lib_scale_5", "scale=5\ns(3.14159/2)\nc(3.14159)\na(2)\nl(2)\n"),
    ("lib_identity", "scale=30\ns(0.5)^2+c(0.5)^2\n"),
    ("lib_big", "scale=50\n4*a(1)\ne(1)\nl(2)\n"),
    ("lib_zero_scale", "s(1)\nc(1)\ne(5)\nl(100)\n"),
    ("lib_neg", "scale=15\ns(-1)\nc(-1)\na(-1)\ne(-2)\n"),
)


def bc_cases():
    c = [_case("bc", name, prog) for name, prog in BC_PROGRAMS]
    c += [_case("bc", name, prog, ["-l"]) for name, prog in BC_LIB_PROGRAMS]
    c.append(_case("bc", "arg_e", "", ["-e", "scale=3; 1/3"]))
    c.append(_case("bc", "arg_e_twice", "", ["-e", "a=4", "-e", "a*a"]))
    c.append(_case("bc", "arg_e_then_stdin", "9*9\n", ["-e", "3+3"]))
    c.append(_case("bc", "arg_bad_option", "", ["-z"]))
    c.append(_case("bc", "arg_missing_file", "", ["no-such-file"]))
    c.append(_case("bc", "arg_file", "", ["p.bc"], files={"p.bc": b"2^64\nquit\n"}))
    c.append(
        _case(
            "bc",
            "arg_two_files",
            "",
            ["a.bc", "b.bc"],
            files={"a.bc": b"x=6\n", "b.bc": b"x*7\n"},
        )
    )
    c.append(
        _case("bc", "arg_c_emit", "scale=3\n1/3\ndefine f(a) { return (a+1) }\nf(2)\n", ["-c"])
    )
    c.append(
        _case(
            "bc",
            "arg_c_emit_long_names",
            "alpha=1\nbeta[2]=alpha\ndefine gamma(x) {\n return (x+alpha)\n}\ngamma(beta[2])\n",
            ["-c"],
        )
    )
    return c


def many_names_cases():
    """Programs with thousands of long names, against a Python model."""
    cases = []
    for count in (40, 700, 2500):
        lines = []
        out = []
        for i in range(count):
            lines.append("var%d=%d" % (i, i * 3 + 1))
            lines.append("var%d[%d]=%d" % (i, i % 50, i * 5 + 2))
            lines.append("define var%d(a) {\n return (a+%d)\n}" % (i, i))
        total = 0
        for i in range(count):
            total += (i * 3 + 1) + (i * 5 + 2) + (i + 1)
        lines.append("t=0")
        for i in range(count):
            lines.append("t=t+var%d+var%d[%d]+var%d(1)" % (i, i, i % 50, i))
        lines.append("t")
        out.append(str(total) + "\n")
        cases.append(
            _case(
                "bc",
                "names_%d" % count,
                "\n".join(lines) + "\n",
                expect="".join(out),
            )
        )
    # The same long name as a variable, an array and a function stays
    # three names; a name that differs in the last letter stays distinct.
    prog = (
        "nm=1\nnm[0]=2\ndefine nm(a) {\n return (a+3)\n}\nnn=10\nnl=20\nnm\nnm[0]\nnm(10)\nnn\nnl\n"
    )
    cases.append(_case("bc", "names_kinds", prog, expect="1\n2\n13\n10\n20\n"))
    return cases


def all_cases():
    cases = []
    cases += arithmetic_cases()
    cases += register_cases()
    cases += dc_program_cases()
    cases += dc_argument_cases()
    cases += bc_cases()
    cases += many_names_cases()
    cases += corpus_cases()
    return cases
