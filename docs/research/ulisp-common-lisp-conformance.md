# uLisp against the Common Lisp standard

## Finding

uLisp is a deliberate subset of Common Lisp, and where it provides a name it
mostly behaves as the standard says. Of the 232 symbols in
`usr.bin/ulisp/builtins.def`, 131 conform on the cases tried, 38 implement a subset of the
standard entry, 13 differ, and 50 are uLisp's own (pins, drawing, images,
the line editor's keywords). The differences that were plain defects against
the standard and cheap to repair are repaired on the `ulisp/cl-conformance`
branch, each with a case in `tests/ulisp/repairs.lisp`, a section-cited case in
`tests/ulisp/conformance.lisp` and a mutant of `ulisp.c` that the check rejects. The
rest are design choices of the port (32-bit integers, no ratios, no multiple
values, no packages) or are listed under "Recorded, not repaired".

## The standard and the suites

- **The standard.** ANSI INCITS 226-1994, "Programming Language -- Common
  Lisp", is the current edition: no later revision of the standard exists. The
  Common Lisp HyperSpec (LispWorks, copyright 1996-2005) is its hypertext
  form; the dpANS3R draft is the freely redistributable text it derives from.
  The HyperSpec pages cited below were fetched on 2026-11-19 into the
  orchestration scratch area and checked for the section titles named here.
  Reaffirmation dates of the ANSI document were not looked up. De facto
  extensions (bignums are standard; MOP, threads, Gray streams, ASDF,
  Unicode, `loop` extensions) matter only where this note says so.
- **The ansi-test suite.** `ansi-test` (originally the GCL ANSI test suite,
  by Paul Dietz) is the community conformance suite: 17,385 `deftest` forms at
  `gitlab.common-lisp.net/ansi-test/ansi-test` commit
  `941290a6151b1c0e5ede819739fe5f46d4b3d6b6`, in one directory per area of the
  HyperSpec (numbers 1,447, cons 1,651, data-and-control-flow 1,454, iteration
  840, strings 503, characters 259, reader 167, and others). Its license is
  MIT (Copyright 2004 Paul F. Dietz); `rt.lsp`, the regression tester every
  file needs, carries the MIT 1990 permission notice. The README says the
  suite is partial and not a ranking.
- **What a harness must provide.** A file of the suite is Common Lisp: it
  expects the RT package (`deftest`, `do-test`), the `CL-TEST` package, bignums,
  ratios, multiple values, conditions (`handler-case`, `signals-error`),
  packages, `compile` and `format`, and loads through `init.lsp`. uLisp has none of
  these, so no file loads. SBCL 2.6.9 and ECL 26.5.5 are installed here and
  serve as the reference: every expectation in the conformance corpus is the
  value SBCL gives and the HyperSpec states. No ansi-test text was copied
  into the tree; the cases are written for this port and cite the section.

## Method

Each builtin was given from one to four expressions (about 780 in all, with
the reader and printer cases); each was run in the host build and in SBCL
with `*read-default-float-format*` single-float and `*print-pretty*` nil, and
the two answers were compared after folding case. The table below shows one
representative expression per name with both answers as the build prints them;
a status says the whole name, not the one expression. A name is "conforms"
when every case tried agreed, "subset" when the standard entry has arguments
or results uLisp lacks, "differs" when a case the standard defines gives
another answer, and "extension" when the standard has no such entry. This is
evidence for the exercised cases, not a proof of conformance.

## Reader syntax (CLHS 2.2 to 2.4)

| Input | uLisp | Common Lisp | Status |
| --- | --- | --- | --- |
| `1` `+5` `-5` `007` `.5` `-.5e-3` `1E5` `1.5e3` | the same number | the same number | conforms (2.3.1, 2.3.2) |
| `1.` | `1.0` | integer 1 (2.3.2.1) | differs |
| `1.5f0` `1.5d0` `1.5s0` `1.5l0` | a symbol | a float (2.3.2.2) | subset: only the exponent markers e and E |
| `4/2` | a symbol | 2 (2.3.2.1) | subset: no ratios |
| `100000000000` | 1.0e11 (a float; 1215752192 before this branch) | an integer | subset: no bignums; the wrap was a defect |
| `2147483648` | 2.14748e9 | 2147483648 | subset: 32-bit integers |
| `#xFF` `#b101` `#o17` `#X1F` | 255 5 15 31 | the same (2.4.8.7 to 2.4.8.9) | conforms |
| `#b102` | the symbol `102` | a reader error | differs |
| `#36rZZ` | an error | 1295 (2.4.8.10) | subset: no `#R` |
| `#xFFFFFFFF` | -1 | 4294967295 | differs: 32-bit wrap |
| `1e39` | Inf | a reader error | differs |
| `(a . b)` `(a . (b c))` | the same | the same (2.3.3) | conforms |
| `abc` `ABC` | one symbol, printed lowercase | one symbol, printed uppercase | differs by convention: `eq` holds in both, the printed case does not |
| `:k` and `:K` | two keywords | one (2.3.4) | differs: keywords are not case-folded |
| `\|a b\|` `a\ b` | two symbols split at the space | one symbol (2.3.4) | subset: no escape characters |
| `a:b` `cl:car` | symbols of those names | a package error, `CAR` | subset: no packages |
| `#\a` `#\Space` `#\Newline` `#\Tab` `#\(` `#\;` | the characters | the characters (2.4.8.1) | conforms |
| `#\Nul` `#\x41` | an error (`#\166` and `#\I` before this branch) | the characters | subset: `#\` plus three digits or a name from uLisp's table |
| `#\Rubout` `#\Linefeed` | an error | the characters | subset: names only below 33 |
| `"a\"b"` `"a\\b"` `"a;b"` | the strings | the strings (2.4.5) | conforms |
| `; c` then a form on the next line | the form (swallowed up to the next `(` before this branch) | the form (2.4.4) | conforms, repaired |
| `(a;b\nc)` | `(a c)` (the symbol `a;b` before) | `(a c)` | conforms, repaired |
| `#\| a #\| b \|# c \|#` | one comment (an error before) | one comment (2.4.8.19) | conforms, repaired |
| `#\| open` at end of a file | the end of input (an endless loop before) | an end-of-file error | subset |
| `'x` | `(quote x)` | `(QUOTE X)` or `'X` by `*print-pretty*` (2.4.3) | conforms |
| `#'car` | reads as `car` | `(function car)` (2.4.8.2) | subset: no `function` operator |
| backquote, comma, `,@` | symbols named by the characters | backquote templates (2.4.6, 2.4.7) | subset |
| `#*101` | a bit array | a bit vector (2.4.8.4) | conforms; the next read failed before this branch |
| `#+` `#-` `#.` `#S` `#P` `#=` | an error or a read-time evaluation of `#.` | feature expressions and others | subset |

### Printer

| Value | uLisp | Common Lisp | Status |
| --- | --- | --- | --- |
| `0.0` | `0.0` (`0` before this branch) | `0.0` | conforms, repaired |
| `123456.7` | `1.23457e5` | `123456.7` | differs: six significant digits, so a printed float need not read back equal (22.1.3.1.3) |
| `1e6` | `1.0e6` | `1000000.0` | differs: exponent form from 1e5 up |
| `(sqrt 2)` | `1.41421` | `1.4142135` | differs: six digits |
| `#\a` `#\Space` | `#\a` `#\Space` | the same | conforms |
| `(code-char 127)` | `#\127` | `#\Rubout` | subset |
| `(code-char 955)` | `#\187` | a Greek letter | subset: characters are bytes |
| `(print 5)` | newline, `5`, space | the same | conforms |

## Builtins by HyperSpec section

Status counts: 131 conforms, 38 subset, 13 differs, 50 extension.
The Common Lisp column is SBCL 2.6.9's answer to the same expression.

### CLHS 3.4.1

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `&optional` | conforms | `(progn (defun o1 (&optional a b) (list a b)) (o1 1))` | `(1 nil)` | `(1 NIL)` | no &key or &aux |
| `&rest` | conforms | `(progn (defun r1 (&rest a) a) (r1 1 2))` | `(1 2)` | `(1 2)` |  |

### CLHS 3.8

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `quote` | conforms | `''a` | `(quote a)` | `(QUOTE A)` | prints as (quote a): *print-pretty* nil in CL does too |
| `defvar` | differs | `(progn (defvar v2 1) (defvar v2 2) v2)` | `2` | `1` | re-evaluates a bound variable's value form: 2 here, 1 in CL |
| `eval` | conforms | `(eval '(+ 1 2))` | `3` | `3` |  |

### CLHS 5.3

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `nil` | conforms | `(list nil '() (symbolp nil))` | `(nil nil t)` | `(NIL NIL T)` |  |
| `t` | conforms | `(eq t 't)` | `t` | `T` |  |
| `lambda` | conforms | `((lambda (x y) (+ x y)) 1 2)` | `3` | `3` |  |
| `let` | conforms | `(let ((a 1)) (let ((a 2) (b a)) b))` | `1` | `1` | duplicate variable names are not detected |
| `let*` | conforms | `(let* ((a 1) (b (+ a 1))) b)` | `2` | `2` |  |
| `defun` | conforms | `(progn (defun f3 (a &optional b) (list a b)) (f3 1))` | `(1 nil)` | `(1 NIL)` | a docstring is accepted |
| `eq` | conforms | `(eq 'a 'a)` | `t` | `T` | CLHS 5.3; numbers and strings are implementation-dependent there too |
| `or` | conforms | `(or nil 5)` | `5` | `5` |  |
| `setq` | conforms | `(let ((a 1) (b 1)) (setq a 5 b a) b)` | `5` | `5` | an unbound variable is an error |
| `push` | conforms | `(let ((l nil)) (push 1 l) l)` | `(1)` | `(1)` |  |
| `pop` | conforms | `(let ((l nil)) (list (pop l) l))` | `(nil nil)` | `(NIL NIL)` | an empty place was an error before this branch |
| `setf` | subset | `(let ((a (list 1 2 3))) (setf (nth 1 a) 9) a)` | `(1 9 3)` | `(1 9 3)` | places: variables, car cdr nth aref char; (setf (cadr a) 9) is an error |
| `progn` | conforms | `(progn 1 2 3)` | `3` | `3` |  |
| `if` | conforms | `(if nil 1)` | `nil` | `NIL` |  |
| `cond` | conforms | `(cond ((= 1 2) 1) (t 2))` | `2` | `2` |  |
| `when` | conforms | `(when t 1 2)` | `2` | `2` |  |
| `unless` | conforms | `(unless nil 1 2)` | `2` | `2` |  |
| `case` | subset | `(case 2 ((1 2) 'a) (t 'c))` | `a` | `A` | the t clause only: no otherwise; (case nil (nil 1) (t 2)) is 1 here, 2 in CL |
| `and` | conforms | `(and 1 nil 3)` | `nil` | `NIL` |  |
| `not` | conforms | `(not 5)` | `nil` | `NIL` |  |
| `equal` | conforms | `(list (equal '(1 2) '(1 2)) (equal "ab" "ab") (equal 1 1.0))` | `(t t nil)` | `(T T NIL)` |  |
| `apply` | conforms | `(apply #'+ 1 2 '(3 4))` | `10` | `10` |  |
| `funcall` | conforms | `(funcall #'+ 1 2)` | `3` | `3` |  |
| `return` | subset | `(dotimes (i 5) (when (= i 2) (return i)))` | `2` | `2` | no block names: return exits the innermost loop only |
| `unwind-protect` | conforms | `(let ((a 0)) (unwind-protect (setq a 1) (setq a 2)) a)` | `2` | `2` |  |

### CLHS 6.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `loop` | subset | `(let ((i 0)) (loop (incf i) (when (> i 3) (return i))))` | `4` | `4` | simple loop only |
| `dolist` | conforms | `(dolist (x '(1 2 3) x))` | `nil` | `NIL` |  |
| `dotimes` | conforms | `(dotimes (i 3 i))` | `3` | `3` |  |
| `do` | conforms | `(do ((i 0 (1+ i)) (s 0 (+ s i))) ((= i 4) s))` | `6` | `6` |  |
| `do*` | conforms | `(do* ((i 0 (1+ i)) (j i (+ j 1))) ((= i 3) j))` | `3` | `3` |  |

### CLHS 9.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `break` | subset |  |  |  | enters a break REPL (:c continues, :a aborts, :b backtrace); CL enters the debugger |
| `ignore-errors` | differs | `(ignore-errors (car 5))` | `` | `NIL` | yields no value (nothing, not printed); CL returns NIL and the condition |
| `error` | subset | `(ignore-errors (error "x"))` | `` | `NIL` | a message string only |

### CLHS 10.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `symbolp` | conforms | `(list (symbolp nil) (symbolp :k) (symbolp "a"))` | `(t t nil)` | `(T T NIL)` |  |
| `boundp` | conforms | `(boundp 'nosuchvar)` | `nil` | `NIL` |  |
| `keywordp` | conforms | `(list (keywordp :a) (keywordp 'a))` | `(t nil)` | `(T NIL)` | returned the builtin's table index for :a before this branch's repair |
| `set` | differs | `(progn (set 'q 5) q)` | `Error: 'set' unknown variable: q` | `5` | uLisp raises an error for a symbol with no variable; CLHS SET creates the global value |
| `makunbound` | conforms | `(progn (defvar mb 1) (makunbound 'mb) (boundp 'mb))` | `nil` | `NIL` |  |
| `apropos` | subset |  |  |  | prints matching builtins; no package argument |
| `apropos-list` | subset |  |  |  | lists builtin names only |

### CLHS 12.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `incf` | conforms | `(let ((a 1)) (incf a 5))` | `6` | `6` |  |
| `decf` | conforms | `(let ((a 1)) (decf a 3))` | `-2` | `-2` |  |
| `+` | subset | `(+ 2147483647 1)` | `2.14748e9` | `2147483648` | 32-bit integers: the sum becomes a float, where CL has bignums |
| `-` | subset | `(- 5)` | `-5` | `-5` | same overflow rule as + |
| `*` | subset | `(* 100000 100000)` | `1.0e10` | `10000000000` | same overflow rule as + |
| `/` | subset | `(/ 7 2)` | `3.5` | `7/2` | no ratios: an inexact integer quotient is a float (3.5), CL gives 7/2; (/ 12 3) is 4 in both |
| `mod` | conforms | `(list (mod 13 -4) (mod -13 4) (mod 5 -1) (mod 6 -3))` | `(-3 3 0 0)` | `(-3 3 0 0)` | fixed on this branch: (mod 5 -1) was -1 and (mod 6 -3) was -3 (CLHS MOD) |
| `rem` | conforms | `(list (rem -13 4) (rem 13 -4) (rem 5 -1))` | `(-1 1 0)` | `(-1 1 0)` | fixed on this branch for the ARM divide path and the sign rule of TRUNCATE |
| `1+` | conforms | `(1+ 5)` | `6` | `6` |  |
| `1-` | conforms | `(1- 5)` | `4` | `4` |  |
| `abs` | conforms | `(abs -5)` | `5` | `5` |  |
| `random` | conforms | `(integerp (random 10))` | `t` | `T` | stream state differs; the limit must be positive in both |
| `max` | conforms | `(max 1 5 3)` | `5` | `5` | float contagion of mixed arguments differs: (max 1 3.0) is 3.0 in both |
| `min` | conforms | `(min 4 2 8)` | `2` | `2` |  |
| `/=` | conforms | `(/= 1 2 1)` | `nil` | `NIL` |  |
| `=` | conforms | `(= 1 1.0)` | `t` | `T` |  |
| `<` | conforms | `(< 1 2 3)` | `t` | `T` |  |
| `<=` | conforms | `(<= 1 1 2)` | `t` | `T` |  |
| `>` | conforms | `(> 3 2 1)` | `t` | `T` |  |
| `>=` | conforms | `(>= 3 3 1)` | `t` | `T` |  |
| `plusp` | conforms | `(plusp 0)` | `nil` | `NIL` |  |
| `minusp` | conforms | `(minusp -1)` | `t` | `T` |  |
| `zerop` | conforms | `(zerop 0)` | `t` | `T` |  |
| `oddp` | conforms | `(oddp -3)` | `t` | `T` |  |
| `evenp` | conforms | `(evenp 0)` | `t` | `T` |  |
| `integerp` | conforms | `(integerp 1.0)` | `nil` | `NIL` |  |
| `numberp` | conforms | `(numberp 'a)` | `nil` | `NIL` |  |
| `float` | subset | `(float 1)` | `1.0` | `1.0` | the optional prototype argument is not accepted: (float 1 2.0) is an error |
| `floatp` | conforms | `(floatp 1)` | `nil` | `NIL` |  |
| `sin` | conforms | `(sin 0)` | `0.0` | `0.0` | prints 0.0 after this branch's float-zero repair |
| `cos` | conforms | `(cos 0)` | `1.0` | `1.0` |  |
| `tan` | conforms | `(< (abs (- (tan 1.0) 1.5574077)) 0.0001)` | `t` | `T` |  |
| `asin` | subset | `(asin 2.0)` | `NaN` | `#C(1.5707964 -1.3169578)` | no complex numbers: NaN here, a complex in CL |
| `acos` | conforms | `(< (abs (- (acos 0.5) 1.0471976)) 0.0001)` | `t` | `T` |  |
| `atan` | conforms | `(list (atan 1 2) (atan -1 -1))` | `(0.463648 -2.35619)` | `(0.4636476 -2.3561945)` | prints 6 digits |
| `sinh` | conforms | `(< (abs (- (sinh 1.0) 1.1752012)) 0.0001)` | `t` | `T` |  |
| `cosh` | conforms | `(< (abs (- (cosh 1.0) 1.5430807)) 0.0001)` | `t` | `T` |  |
| `tanh` | conforms | `(< (abs (- (tanh 1.0) 0.7615942)) 0.0001)` | `t` | `T` |  |
| `exp` | conforms | `(exp 0)` | `1.0` | `1.0` |  |
| `sqrt` | subset | `(sqrt -1)` | `NaN` | `#C(0.0 1.0)` | NaN here, a complex in CL |
| `log` | subset | `(log 0)` | `Inf` | `ERR DIVISION-BY-ZERO` | Inf here, an error in SBCL; no complex results |
| `expt` | subset | `(expt 2 -1)` | `0.5` | `1/2` | 0.5 here, 1/2 in CL; (expt 0 0) is NaN here and 1 in CL (plain defect, not repaired within the size limit) |
| `ceiling` | subset | `(ceiling -7 2)` | `-3` | `-3` | first value only: no multiple values; repaired on this branch |
| `floor` | subset | `(floor -7 2)` | `-4` | `-4` | first value only; division by an integer zero crashed the host build before this branch |
| `truncate` | subset | `(truncate -7 2)` | `-3` | `-3` | first value only |
| `round` | subset | `(round 5 2)` | `2` | `2` | first value only; halves went away from zero (3) before this branch, now to even (2) |
| `logand` | conforms | `(logand 12 10)` | `8` | `8` |  |
| `logior` | conforms | `(logior 12 10)` | `14` | `14` |  |
| `logxor` | conforms | `(logxor 12 10)` | `6` | `6` |  |
| `lognot` | conforms | `(lognot 5)` | `-6` | `-6` |  |
| `ash` | subset | `(ash 1 40)` | `0` | `1099511627776` | 32-bit: 0 here, a bignum in CL |
| `logbitp` | conforms | `(logbitp 1 2)` | `t` | `T` |  |

### CLHS 13.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `char-code` | subset | `(char-code #\a)` | `97` | `97` | characters are bytes: (code-char 955) is #\187 here |
| `code-char` | subset | `(code-char 97)` | `#\a` | `#\a` | 8-bit characters |
| `characterp` | conforms | `(characterp #\a)` | `t` | `T` |  |

### CLHS 14.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `car` | conforms | `(list (car '(1 2)) (car nil))` | `(1 nil)` | `(1 NIL)` |  |
| `first` | conforms | `(first '(1 2))` | `1` | `1` |  |
| `cdr` | conforms | `(list (cdr '(1 2)) (cdr nil))` | `((2) nil)` | `((2) NIL)` |  |
| `rest` | conforms | `(rest '(1 2 3))` | `(2 3)` | `(2 3)` |  |
| `nth` | conforms | `(list (nth 1 '(a b c)) (nth 5 '(a)))` | `(b nil)` | `(B NIL)` |  |
| `null` | conforms | `(null '())` | `t` | `T` |  |
| `cons` | conforms | `(cons 1 2)` | `(1 . 2)` | `(1 . 2)` |  |
| `atom` | conforms | `(atom nil)` | `t` | `T` |  |
| `listp` | conforms | `(listp nil)` | `t` | `T` |  |
| `consp` | conforms | `(consp nil)` | `nil` | `NIL` |  |
| `caar` | conforms | `(caar '((1) 2))` | `1` | `1` |  |
| `cadr` | conforms | `(cadr '(1 2 3))` | `2` | `2` |  |
| `second` | conforms | `(second '(1 2 3))` | `2` | `2` |  |
| `cdar` | conforms | `(cdar '((1 3) 2))` | `(3)` | `(3)` |  |
| `cddr` | conforms | `(cddr '(1 2 3))` | `(3)` | `(3)` |  |
| `caaar` | conforms | `(caaar '(((1))))` | `1` | `1` |  |
| `caadr` | conforms | `(caadr '(1 (2)))` | `2` | `2` |  |
| `cadar` | conforms | `(cadar '((1 2) 3))` | `2` | `2` |  |
| `caddr` | conforms | `(caddr '(1 2 3))` | `3` | `3` |  |
| `third` | conforms | `(third '(1 2 3))` | `3` | `3` |  |
| `cdaar` | conforms | `(cdaar '(((1 2))))` | `(2)` | `(2)` |  |
| `cdadr` | conforms | `(cdadr '(1 (2 3)))` | `(3)` | `(3)` |  |
| `cddar` | conforms | `(cddar '((1 2 3)))` | `(3)` | `(3)` |  |
| `cdddr` | conforms | `(cdddr '(1 2 3 4))` | `(4)` | `(4)` |  |
| `list` | conforms | `(list 1 2 3)` | `(1 2 3)` | `(1 2 3)` |  |
| `copy-list` | conforms | `(copy-list '(1 2 3))` | `(1 2 3)` | `(1 2 3)` |  |
| `assoc` | conforms | `(list (assoc 'b '((a 1) (b 2))) (assoc "b" '(("b" 1))))` | `((b 2) nil)` | `((B 2) NIL)` | default test is EQL, so the string key is not found in either |
| `member` | conforms | `(list (member 2 '(1 2 3)) (member "a" '("a")))` | `((2 3) nil)` | `((2 3) NIL)` |  |
| `append` | differs | `(append '(1) 3)` | `Error: 'append' argument is not a list: 3` | `(1 . 3)` | an improper last argument is an error here; CL returns (1 . 3) |
| `mapc` | conforms | `(let ((r nil)) (mapc (lambda (x) (push x r)) '(1 2)) r)` | `(2 1)` | `(2 1)` |  |
| `mapl` | conforms | `(mapl #'car '(1 2))` | `(1 2)` | `(1 2)` |  |
| `mapcar` | conforms | `(mapcar #'+ '(1 2 3) '(10 20))` | `(11 22)` | `(11 22)` |  |
| `mapcan` | conforms | `(mapcan (lambda (x) (list x x)) '(1 2))` | `(1 1 2 2)` | `(1 1 2 2)` |  |
| `maplist` | conforms | `(maplist #'car '((1) (2)))` | `((1) (2))` | `((1) (2))` |  |
| `mapcon` | conforms | `(mapcon #'copy-list '(1 2 3))` | `(1 2 3 2 3 3)` | `(1 2 3 2 3 3)` |  |

### CLHS 15.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `bit` | conforms |  |  |  | the element type of make-array |
| `aref` | conforms | `(aref (make-array '(2 2) :initial-element 9) 1 1)` | `9` | `9` |  |
| `arrayp` | differs | `(arrayp "abc")` | `nil` | `T` | strings are not arrays here; CL: T |
| `array-dimensions` | subset | `(array-dimensions (make-array 4))` | `(4)` | `(4)` | arrays only: (array-dimensions "abc") is an error here, (3) in CL |
| `make-array` | subset | `(array-dimensions (make-array '(2 3)))` | `(2 3)` | `(2 3)` | initial-element and element-type bit only; initial contents are nil, not 0 |

### CLHS 16.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `char` | conforms | `(char "abc" 1)` | `#\b` | `#\b` |  |
| `string` | extension | `(string 5)` | `"5"` | `ERR SIMPLE-TYPE-ERROR` | accepts a number: "5", an error in CL |
| `stringp` | conforms | `(stringp "a")` | `t` | `T` |  |
| `string=` | subset | `(list (string= "abc" "abc") (string= "abc" "ABC"))` | `(t nil)` | `(T NIL)` | no :start/:end keywords and no symbol designators |
| `string<` | conforms | `(list (string< "abc" "abd") (string< "b" "a"))` | `(2 nil)` | `(2 NIL)` | returns the mismatch index in both |
| `string>` | conforms | `(string> "b" "a")` | `0` | `0` |  |
| `string/=` | conforms | `(string/= "a" "b")` | `0` | `0` |  |
| `string<=` | conforms | `(string<= "a" "a")` | `1` | `1` |  |
| `string>=` | conforms | `(string>= "b" "a")` | `0` | `0` |  |

### CLHS 17.3

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `length` | conforms | `(list (length "abc") (length '(1 2)) (length nil) (length (make-array 3)))` | `(3 2 0 3)` | `(3 2 0 3)` |  |
| `reverse` | subset | `(reverse '(1 2 3))` | `(3 2 1)` | `(3 2 1)` | lists only: (reverse "abc") is an error here, "cba" in CL |
| `sort` | subset | `(sort (list 3 1 2) #'<)` | `(1 2 3)` | `(1 2 3)` | lists only; stable (CL's SORT is not required to be) |
| `concatenate` | subset | `(concatenate 'string "a" "b")` | `"ab"` | `"ab"` | only the string result type |
| `subseq` | subset | `(subseq "abc" 1 2)` | `"b"` | `"b"` | lists and strings, not vectors |
| `search` | subset | `(search "b" "abc")` | `1` | `1` | no :from-end or other keywords |

### CLHS 20.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `directory` | differs |  |  |  | returns file names of a directory path, not pathnames |

### CLHS 21.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `with-output-to-string` | conforms | `(with-output-to-string (s) (princ 1 s) (princ "a" s))` | `"1a"` | `"1a"` |  |
| `streamp` | conforms | `(streamp 5)` | `nil` | `NIL` |  |
| `terpri` | conforms | `(terpri)` | `nil` | `NIL` |  |
| `read-byte` | subset |  |  |  | reads a byte from a stream; no binary stream types |
| `read-line` | differs | `(read-line)` | `""` | `ERR END-OF-FILE` | at end of input returns ""; CL signals end-of-file |
| `write-byte` | subset |  |  |  | as read-byte |
| `write-string` | differs | `(write-string "abc")` | `abc nil` | `abc "abc"` | returns nil; CL returns the string |
| `write-line` | differs | `(write-line "abc")` | `abc nil` | `abc "abc"` | returns nil; CL returns the string |

### CLHS 22.3

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `format` | subset | `(format nil "~a ~s ~d ~x ~b~%" 'a "s" 5 255 5)` | `"a \"s\" 5 ff 101 "` | `"A \"s\" 5 FF 101 "` | directives a s d x b c% & ~ only: ~o ~c ~r ~{ are errors |

### CLHS 22.4

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `princ-to-string` | conforms | `(princ-to-string '(1 "a"))` | `"(1 a)"` | `"(1 a)"` |  |
| `prin1-to-string` | conforms | `(prin1-to-string '(1 "a"))` | `"(1 \"a\")"` | `"(1 \"a\")"` |  |
| `prin1` | conforms | `(prin1-to-string "a")` | `"\"a\""` | `"\"a\""` |  |
| `print` | conforms | `(print 5)` | `5  5` | `5 5` | newline before and space after, as CL |
| `princ` | conforms | `(princ-to-string "a b")` | `"a b"` | `"a b"` |  |
| `pprint` | subset |  |  |  | pretty-prints a list; no stream argument semantics |

### CLHS 23.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `read-from-string` | conforms | `(read-from-string "(1 2)")` | `(1 2)` | `(1 2)` |  |
| `read` | conforms | `(read-from-string "x")` | `x` | `X` |  |

### CLHS 24.1

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `*features*` | extension |  |  |  | a list of keywords for this platform |

### CLHS 24.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `require` | subset |  |  |  | loads a uLisp library name, not a module |

### CLHS 25.2

| Name | Status | Test expression | uLisp | Common Lisp | Note |
| --- | --- | --- | --- | --- | --- |
| `trace` | differs |  |  |  | prints calls; CL's format differs |
| `untrace` | differs |  |  |  | returns nil where CL returns the list |
| `time` | differs | `(time (+ 1 2))` | `3 Time: 0 ms` | `Evaluation took:
  0.000 seconds of real time
  0.000001 sec` | prints 'Time: n ms' and returns the value; CL's report differs |
| `room` | subset |  |  |  | free cells, not a storage report |
| `sleep` | conforms |  |  |  | seconds, as CL |
| `documentation` | differs |  |  |  | (documentation 'car) reads the .doc file; CL takes a doc type |

### Names with no CLHS entry (extensions) and unclassified syntax markers

| Name | Status | Note |
| --- | --- | --- |
| `nothing` | extension |  |
| `:initial-element` | conforms | keyword of make-array |
| `:element-type` | conforms | keyword of make-array |
| `:test` | conforms | keyword of assoc and member |
| `:a` | extension |  |
| `:b` | extension |  |
| `:c` | extension |  |
| `closure` | extension | internal marker of a closure |
| `*pc*` | extension |  |
| `defcode` | extension |  |
| `pinmode` | extension |  |
| `digitalwrite` | extension |  |
| `analogread` | extension |  |
| `register` | extension |  |
| `for-millis` | extension |  |
| `with-sd-card` | extension |  |
| `globals` | extension |  |
| `locals` | extension |  |
| `gc` | extension |  |
| `backtrace` | extension |  |
| `save-image` | extension |  |
| `load-image` | extension |  |
| `cls` | extension |  |
| `digitalread` | extension |  |
| `delay` | extension |  |
| `millis` | extension |  |
| `note` | extension |  |
| `edit` | extension |  |
| `pprintall` | extension |  |
| `list-library` | extension |  |
| `?` | extension |  |
| `with-gfx` | extension |  |
| `draw-pixel` | extension |  |
| `draw-line` | extension |  |
| `draw-rect` | extension |  |
| `fill-rect` | extension |  |
| `draw-circle` | extension |  |
| `fill-circle` | extension |  |
| `draw-round-rect` | extension |  |
| `fill-round-rect` | extension |  |
| `draw-triangle` | extension |  |
| `fill-triangle` | extension |  |
| `draw-char` | extension |  |
| `set-cursor` | extension |  |
| `set-text-color` | extension |  |
| `set-text-size` | extension |  |
| `set-text-wrap` | extension |  |
| `fill-screen` | extension |  |
| `get-key` | extension |  |
| `read-pixel` | extension |  |
| `save-bmp` | extension |  |

## Common Lisp names uLisp does not provide

`identity`, `last`, `nconc`, `copy-seq`, `list*`, `rplaca`, `eql`, `equalp`, `position`, `remove`,
`remove-if`, `find`, `reduce`, `some`, `every`, `nreverse`, `gcd`, `lcm`, `isqrt`, `signum`,
`string-upcase`, `char-upcase`, `char=`, `char<`, `digit-char-p`, `parse-integer`, `vector`,
`elt`, `count`, `make-string`, `values`, `multiple-value-bind`, `declare`, `function`,
`labels`, `flet`, `block`, `return-from`, `catch`, `throw`, `prog1`, `prog2`, `the`, `typep`,
`intern`. Each was applied and gave "illegal function" or "undefined".

## Repaired on this branch

| Defect | Before | After | Section |
| --- | --- | --- | --- |
| `mod` with a zero remainder and a differing sign | `(mod 5 -1)` is -1, `(mod 6 -3)` is -3, `(mod 4.0 -2)` is -2.0 | 0, 0, 0.0 | 12.2 MOD |
| `round` of a half | `(round 2.5)` is 3 | 2 (to even), integers exact | 12.2 ROUND |
| `floor` `ceiling` `truncate` `round` by zero | SIGFPE on the host; garbage for a float divisor | a Lisp error | 12.2 |
| `pop` of an empty place | an error | `nil` | 5.3 POP |
| `keywordp` of a builtin keyword | the table index | `t` | 10.2 KEYWORDP |
| float zero | printed `0` | `0.0` | 22.1.3.1.3 |
| integer literal past 32 bits | wrapped (`100000000000` read as 1215752192) | a float | 2.3.2 |
| `ash` by 32 or more | a C shift of undefined effect | 0, or the sign for a right shift | 12.2 ASH |
| semicolon comment | ended at the next `(`, swallowing forms; endless at the end of a file | ends at the line end | 2.4.4 |
| `;` inside a token | part of the symbol | ends the token | 2.4.4, 2.3.5 |
| `#| |#` | did not nest; endless at the end of a file | nests | 2.4.8.19 |
| `#\` plus three characters | read as three decimal digits whatever they were | digits only | 2.4.8.1 |
| `#*101` | the next read failed | reads on | 2.4.8.4 |

The merge of `usr.bin/ulisp-c17` changed one choice made here: a result that
no 32-bit integer holds (`(floor -2147483648 -1)`, `(floor 1e10)`) is a float,
as that branch does, not an error. `tests/ulisp/arith.expected` was regenerated
with `gen-arith.py`; every changed line is a `mod` or `round` result, the
printing of 0.0, a decimal literal read as a float, or a `round` of a large
integer now exact (the float path lost bits above 2^24).

## Recorded, not repaired

- Design choices: 32-bit integers and no bignums (`(+ 2147483647 1)` is a
  float; `(ash 1 31)` wraps), no ratios, no complex numbers, no multiple values
  (`floor` returns its first value), no packages, characters are bytes, lexical
  `let` over a dynamic environment, six-digit float printing.
- Left within the size limit: `(expt 0 0)` is NaN (CL 1); `defvar` re-evaluates a
  bound variable; `(append '(1) 3)` is an error; `(case nil (nil 1) (t 2))`
  selects the `nil` clause and `otherwise` is absent; `ignore-errors` yields no
  value; `write-string` and `write-line` return `nil`; `(read-line)` at the end of
  input returns `""`; `1.` reads as a float; `#b102` reads as a symbol; keywords
  are not case-folded; `#\Rubout` and `#\Linefeed` are unknown; `1e39` reads as
  Inf; `setf` of `cadr` and the other `c[ad]r` places is an error.

## The conformance gate

`tests/ulisp/conformance.lisp` holds 455 cases, each printing a tag that
starts with the HyperSpec section (`"12.2 MOD 3"`), grouped by entry with the
page name in a comment. A failing case prints `(fail tag got want)`.
`check.sh` runs it on the host (also under the address and undefined-behavior
sanitizers) and under qemu against `conformance.expected`. Calibration:
`mutants.sh` rejects `mod-sign`, `rem-sign`, `round-even`, `divide-zero`,
`literal-wrap`, `comment-end`, `block-nest`, `char-digits`, `bitarray-lastchar`,
`pop-nil`, `float-zero` and `quotient-min`, each a one-line change of `ulisp.c` that
the reader or arithmetic cases catch.

## The line editor's colors

When standard input and output are terminals, `TERM` is set and is not
`dumb`, and no `-n` was given, the REPL reads each line through an editor
(`editline()` in `unix.c`) with the terminal in unechoed character mode and
repaints the line after every key. `hilite.c` is the tokenizer: a function
of the state the earlier lines left (open string, `#|` depth, parenthesis
depth) and the line, shared with nothing else. It follows `nextitem()`: the token
delimiters are space, tab, newline, `( ) " # ' ;`; the potential-number rule is the
reader's own state machine (so `1+`, `-`, `e5`, `1e` are symbols and `.5`, `-.5e-3`,
`1.` numbers); symbols are looked up in the builtin table the reader uses.

| Class | SGR | Applies to |
| --- | --- | --- |
| parenthesis | 1 (bold) | `(` and `)`; the opener of a just-typed `)` also in reverse video (7) when it is on the line |
| error | 41 | a `)` with nothing open; a `#` before an unknown character |
| string | 32 | a string with its quotes and escapes, over line ends |
| comment | 90 | a `;` comment, and `#| |#` with nesting |
| number | 35 | integers, floats, `#b #o #x` numbers the reader accepts |
| character, quote | 93 | `#\x` literals; `'` and `#'` |
| keyword | 95 | a token that starts with a colon |
| special form | 94 | table entries of type special or tail form, and `lambda` `let` `let*` |
| function | 36 | table entries of type function |
| constant | 33 | `nil` and `t` |
| plain | none | every other symbol, including unbound ones |

The set is the one `sys/arch/rp2040/dev/lcdterm.c` accepts (SGR 0, 1, 7,
30-37, 90-97, 40-47) plus CHA, CUU and ED to repaint a wrapped line; the
width comes from `TIOCGWINSZ`, which the LCD console, the USB console and the UART
set. Not classified: the `#` of `#( #* #. #nA` (marked as a reader macro, what
follows classifies as a token) and tokens longer than 33 characters, which the
reader splits. Not painted: the value the REPL prints (the brief's "echoed
result"); the editor colors the input only. Keys: printable, backspace, DEL, ^U,
^D at an empty line, Return; an escape sequence is dropped; ^C discards the
line; a line that fills the 95-byte buffer is handed over and the next
continues it. Cursor movement within a line is not implemented.

Tests: `hilite-test.c` checks 42 lines (every class, and the hard cases:
`;` and `#|` inside strings, `#\;` `#\(` `#\)`, `|` in a symbol, `1+`, `-`, `1e5`
against `e5`, `.5`, `-.5e-3`, a block comment opened in a line comment,
strings and block comments that continue over a line end), also under the
sanitizers; `hilite-pty.py` drives the host build on a pseudo-terminal through a
small terminal model and checks the classes of the real builtin table, the
match highlight, backspace and kill, repaint across wrapped rows, and that no
escape reaches a dumb or absent terminal, `-n` or a pipe. Eight mutants of
`hilite.c` and four of `unix.c` and `ulisp.c` are rejected.
`tools/renode/check-ulisp.py` now starts the interpreter as `ulisp -n`, since
the UART console is a terminal with `TERM=xterm` and its expectations match
the uncolored transcript.

## Image check

`checkimage()` now also requires, for every array: a pair for the array whose
car is the tree and whose cdr is a list of number cells, the first negative
only for a bit array, with a product that `makearray()` would allow (in words for
a bit array); a tree with a pair on the path of every subscript (the
`buildarray()` shape), no pair used by two trees or twice in one; and a number
cell in every word of a bit array. The dimension list belongs to the caller
and may be shared, so it is bounded by the cell count, not claimed. Time is
one read per tree pair, at most the cell count; stack is two bitmaps of one
bit per cell and a recursion as deep as the tree (log2 of the elements).
Fifteen damaged images join the corpus (dimension grown, zero, flagged as bits,
second dimension negative, huge, not a number, looping; tree truncated, null,
looping at the root and deeper, shared; bit leaves null and not numbers) and
all are refused with the workspace unchanged. Not validated: the machine code
in an image (it runs by design) and cycles in ordinary list data, which the
collector and the printer's escape check already bound.

## Size

| Stage | a.out bytes | text+data | bss | Workspace under qemu |
| --- | --- | --- | --- | --- |
| 9e6b5ab71 | 54,640 | 54,604 | 2,100 | 14,385 cells |
| plus the repairs | 54,984 | | | |
| plus the image check | 55,664 | | | |
| plus the line editor (console buffers 128 to 96 bytes) | 57,640 | | | |
| after merging `usr.bin/ulisp-c17` | 57,824 | 57,792 | 1,988 | 14,001 cells |

The repairs cost 344 bytes (limit 1,500), the image check 680, the editor and
tokenizer about 2,000 net of 160 bytes of buffers. The workspace is the first
prompt of the linked a.out under `qemu-arm` with an empty environment, the
same measure as the first row; it is 1 cell above the 14,000 floor, so the next
addition to the program needs a matching saving. Renode was not rerun.

## Open decisions

- Whether the Renode UART console should run `ulisp -n` (as the gate now does)
  or color: the UART and USB consoles are both `TERM=xterm` terminals the
  program cannot tell apart from the PicoCalc panel.
- Whether to repair the remaining plain defects (`expt 0 0`, `defvar`,
  `ignore-errors`, `write-string` result) at about 10 to 60 bytes each, which
  needs the workspace margin above.
- Whether the printed value should be colored too.
