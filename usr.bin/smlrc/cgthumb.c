/*
Copyright (c) 2012-2015, Alexey Frunze
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*****************************************************************************/
/*                                                                           */
/*                                Smaller C                                  */
/*                                                                           */
/*       A simple and small single-pass C compiler ("small C" class).        */
/*                                                                           */
/*                    Thumb-1 (ARMv6-M) code generator                       */
/*                                                                           */
/*****************************************************************************/

/*
  Register assignment. Every role is fixed for the whole backend, because
  three separate sites -- computing r7 - N for a local, materializing a zero
  for tokAssign0, and materializing an out-of-range cmp immediate -- each need
  "a free low register", and tokAssign0 through a computed local address needs
  two of them at once.

    r0  working register W0, and the AAPCS argument/return register.
    r1  address scratch. Holds a computed effective address.
    r2  constant scratch. Holds a materialized immediate or a zero.
    r3  unused by the expression evaluator; free.
    r4  T0, the one subexpression temporary.
    r5  TEMP_REG_A, r6  TEMP_REG_B.
    r7  frame pointer.
    r12 holds an indirect call target across the argument register loads.

  TEMP_REG_A and TEMP_REG_B are callee-saved because the __aeabi_idivmod
  family is reached by BL and only preserves r4-r11; a caller-saved pair would
  lose its value across every division. r1, r2 and r3 are caller-saved and
  safe because arguments travel on the stack until the LDR sequence
  immediately before the BL, so no argument is ever live in them.

  The maxCallDepth == 1 fast path of the MIPS generator is not reproduced.
  There, the leftmost argument is evaluated directly into A0 while the working
  register is a distinct V0; on ARM both are r0, so a called expression
  producing a function pointer would overwrite the first argument. Evaluating
  every argument onto the stack removes the aliasing and frees r1-r3.

  Stack frame, laid out to the offsets smlrc.c assigns for MIPS so that the
  front end's parameter and local offsets carry over unchanged:

    [r7, #8] + 4n  parameter n + 1
    [r7, #4]       saved lr
    [r7, #0]       saved r7
    [r7, #-4]      first local, growing down to CurFxnMinLocalOfs
    below that     saved r0 (pad), r4, r5, r6

  The callee rebuilds parameters 1-4 into a home area with PUSH {r0-r3} so
  that they sit contiguously below the stacked parameters 5 and up, which is
  what makes &lastnamed + 4 walk correctly for va_arg.

  Branches. ARMv6-M B<cond> reaches +-256 bytes and B reaches +-2048, and
  GNU as does not relax either, so neither is safe for compiler output. Every
  jump is a BL, which reaches +-16MB, and every conditional jump is the
  inverted condition branching over a BL. BL clobbers lr, which is harmless
  because lr is saved in the prolog and the epilog returns through r3, so no
  function is ever treated as a leaf.

  Literal pools. LDR (literal) reaches 1020 bytes forward only, so a pool is
  flushed whenever the emitted distance since the last one passes a threshold
  well inside that range. The flush is a BL-free B over a .ltorg and is only
  ever inserted at a point where no partially emitted instruction sequence is
  open.
*/

#define ThumbOpRegW0                     0
#define ThumbOpRegAddr                   1
#define ThumbOpRegCnst                   2
#define ThumbOpRegCall                   3
#define ThumbOpRegT0                     4
#define ThumbOpRegFp                     7
#define ThumbOpRegIp                     12

#define TEMP_REG_A                       5
#define TEMP_REG_B                       6

#define MAX_TEMP_REGS                    1

/* Distance in bytes allowed between an LDR (literal) and its pool. The
   architectural limit is 1020; every emitter accounts its own bytes, and the
   remaining slack absorbs the one token handler that may still run after the
   check at the top of the loop, none of which emits close to that much. */
#define THUMB_POOL_LIMIT                 850

STATIC
void GenPrintLabel(char* Label);
STATIC
void GenPrintNumLabel(int label);
STATIC
void GenLoadConst(int reg, int val);
STATIC
void ThumbMaybeFlushPool(void);

int ThumbPoolBytes = 0;   /* bytes emitted since the last literal pool */
int ThumbLocalLabel = 1;  /* counter for backend-private .LT labels */
int ThumbFrameLabel = 0;  /* .LF symbol naming the current frame size */
int ThumbFrameSeq = 0;

/* AAPCS32 5.2.1.2 requires SP to be 8-byte aligned at every public
   interface. The expression evaluator builds an argument area out of
   single-word pushes, so the depth it reaches at a BL is whatever the
   argument count and the enclosing expression make it. ThumbSpDepth counts
   the bytes the evaluator has pushed below the function's own frame base,
   which GenFxnProlog leaves 8-byte aligned, and every call site reserves a
   padding word when the depth it would reach is 4 modulo 8. The pad is
   reserved before the arguments, so it sits above them and argument five
   still lands at the callee's entry SP.

   The pad depends on the depth at the opening parenthesis, and nothing at
   the closing parenthesis can recover that depth, so each open call carries
   its pad on ThumbCallPad. One entry per open call is bounded by STACK_SIZE
   because every call holds a '(' in the expression stack. */
int ThumbSpDepth = 0;
unsigned char ThumbCallPad[STACK_SIZE];
int ThumbCallSp = 0;

STATIC
void GenInit(void)
{
  SizeOfWord = 4;
  OutputFormat = FormatSegmented;
  CodeHeaderFooter[0] = "\t.text";
  DataHeaderFooter[0] = "\t.data";
  RoDataHeaderFooter[0] = "\t.section\t.rodata,\"a\",%progbits";
  BssHeaderFooter[0] = "\t.bss";
  UseLeadingUnderscores = 0;
  /* arm-none-eabi-gcc makes char unsigned, and every object the tree's libc
     contributes was compiled under that rule. */
  CharIsSigned = 0;
  FileHeader = "\t.syntax\tunified\n\t.thumb";
}

STATIC
int GenInitParams(int argc, char** argv, int* idx)
{
  (void)argc;
  if (!strcmp(argv[*idx], "-v"))
    return 1;

  return 0;
}

STATIC
void GenInitFinalize(void)
{
}

STATIC
void GenStartCommentLine(void)
{
  printf2(" @ ");
}

STATIC
void GenWordAlignment(int bss)
{
  (void)bss;
  printf2("\t.align\t2\n");
}

STATIC
void GenLabel(char* Label, int Static)
{
  if (!Static && GenExterns)
    printf2("\t.globl\t%s\n", Label);
  /* A BL to a label the assembler does not know to be Thumb produces an
     interworking veneer or a relocation the ARMv6-M linker cannot satisfy. */
  if (CurHeaderFooter == CodeHeaderFooter)
  {
    printf2("\t.thumb_func\n");
    printf2("\t.type\t%s, %%function\n", Label);
  }
  printf2("%s:\n", Label);
}

/* The single-precision helpers the front end names are the generic libgcc
   ones, and libgcc built for ARM EABI publishes those under their AEABI
   names instead: an arm-none-eabi libgcc for v6-m has __aeabi_fadd but no
   __addsf3, so a program using float assembles and then fails to link.
   Each pair below takes and returns its operands identically, in r0 and r1,
   so the substitution is a rename and nothing more. The comparison helpers
   __lesf2 and __gesf2 keep their generic names, which libgcc does define,
   and their sign-of-difference result, which the AEABI __aeabi_fcmp family
   does not share. */
char* ThumbFloatHelpers[8][2] =
{
  { "__addsf3",      "__aeabi_fadd" },
  { "__subsf3",      "__aeabi_fsub" },
  { "__mulsf3",      "__aeabi_fmul" },
  { "__divsf3",      "__aeabi_fdiv" },
  { "__negsf2",      "__aeabi_fneg" },
  { "__fixsfsi",     "__aeabi_f2iz" },
  { "__floatsisf",   "__aeabi_i2f"  },
  { "__floatunsisf", "__aeabi_ui2f" }
};

STATIC
void GenPrintLabel(char* Label)
{
  if (isdigit(*Label))
  {
    printf2(".L%s", Label);
  }
  else
  {
    int i;
    for (i = 0; i < 8; i++)
      if (!strcmp(Label, ThumbFloatHelpers[i][0]))
      {
        printf2("%s", ThumbFloatHelpers[i][1]);
        return;
      }
    printf2("%s", Label);
  }
}

STATIC
void GenPrintNumLabel(int label)
{
  printf2(".L%d", label);
}

STATIC
void GenNumLabel(int Label)
{
  /* A numeric label in the text section is a branch target with no partially
     emitted sequence open, so a pool may be flushed just above it. The same
     emitter also names static initializers and string literals, where a
     branch and a .ltorg would land in .data or .bss, hence the guard. */
  if (CurHeaderFooter == CodeHeaderFooter)
    ThumbMaybeFlushPool();
  printf2(".L%d:\n", Label);
}

STATIC
void GenZeroData(unsigned Size, int bss)
{
  (void)bss;
  printf2("\t.space\t%u\n", truncUint(Size));
}

STATIC
void GenIntData(int Size, int Val)
{
  Val = truncInt(Val);
  if (Size == 1)
    printf2("\t.byte\t%d\n", Val);
  else if (Size == 2)
    printf2("\t.short\t%d\n", Val);
  else if (Size == 4)
    printf2("\t.word\t%d\n", Val);
}

STATIC
void GenStartAsciiString(void)
{
  printf2("\t.ascii\t");
}

STATIC
void GenAddrData(int Size, char* Label, int ofs)
{
  ofs = truncInt(ofs);
  if (Size == 1)
    printf2("\t.byte\t");
  else if (Size == 2)
    printf2("\t.short\t");
  else if (Size == 4)
    printf2("\t.word\t");
  GenPrintLabel(Label);
  if (ofs)
    printf2(" %+d", ofs);
  puts2("");
}

/* Literal pool management. A pool may only be flushed where no multi-
   instruction sequence is open, so the call sites are the top of the
   GenExpr0 token loop, the numeric label emitter, and function boundaries. */
STATIC
void ThumbFlushPool(void)
{
  int l = ThumbLocalLabel++;
  printf2("\tb\t.LT%d\n", l);
  puts2("\t.ltorg");
  printf2(".LT%d:\n", l);
  ThumbPoolBytes = 0;
}

STATIC
void ThumbMaybeFlushPool(void)
{
  if (ThumbPoolBytes >= THUMB_POOL_LIMIT)
    ThumbFlushPool();
}

STATIC
void ThumbSpend(int bytes)
{
  ThumbPoolBytes += bytes;
}

STATIC
void ThumbMov(int rd, int rs)
{
  if (rd == rs)
    return;
  printf2("\tmov\tr%d, r%d\n", rd, rs);
  ThumbSpend(2);
}

/* Materialize a 32-bit constant. MOVS covers 0..255 and MOVS with NEGS covers
   -255..-1 without touching a pool; everything else becomes an LDR (literal)
   and is accounted against the pool distance. */
STATIC
void GenLoadConst(int reg, int val)
{
  int v = truncInt(val);
  unsigned u = truncUint(val);

  if (u <= 255)
  {
    printf2("\tmovs\tr%d, #%u\n", reg, u);
    ThumbSpend(2);
  }
  else if (v < 0 && v >= -255)
  {
    printf2("\tmovs\tr%d, #%d\n", reg, -v);
    printf2("\tnegs\tr%d, r%d\n", reg, reg);
    ThumbSpend(4);
  }
  else
  {
    printf2("\tldr\tr%d, =%d\n", reg, v);
    ThumbSpend(6);
  }
}

STATIC
void GenLoadLabelAddr(int reg, int label)
{
  printf2("\tldr\tr%d, =", reg);
  GenPrintLabel(IdentTable + label);
  puts2("");
  ThumbSpend(6);
}

/* sp += n, for any n. ADD/SUB (SP plus immediate) encodes a word-aligned
   0..508; anything larger goes through a register. ARMv6-M has ADD (SP plus
   register) and no SUB (SP plus register), so a decrease adds a negative. */
STATIC
void GenAddSp(int n)
{
  if (!n)
    return;

  if (n > 0)
  {
    if (n <= 508 && !(n & 3))
    {
      printf2("\tadd\tsp, #%d\n", n);
      ThumbSpend(2);
    }
    else
    {
      GenLoadConst(ThumbOpRegAddr, n);
      printf2("\tadd\tsp, r%d\n", ThumbOpRegAddr);
      ThumbSpend(2);
    }
  }
  else
  {
    if (-n <= 508 && !((-n) & 3))
    {
      printf2("\tsub\tsp, #%d\n", -n);
      ThumbSpend(2);
    }
    else
    {
      GenLoadConst(ThumbOpRegAddr, n);
      printf2("\tadd\tsp, r%d\n", ThumbOpRegAddr);
      ThumbSpend(2);
    }
  }
}

/* Effective address of a frame slot. Thumb-1 load and store offsets are
   unsigned, so a local, whose offset from r7 is negative, never encodes
   directly and always needs the address computed. */
STATIC
void GenLocalAddr(int reg, int ofs)
{
  ofs = truncInt(ofs);
  if (!ofs)
  {
    ThumbMov(reg, ThumbOpRegFp);
  }
  else if (ofs > 0)
  {
    if (ofs <= 7)
    {
      printf2("\tadds\tr%d, r%d, #%d\n", reg, ThumbOpRegFp, ofs);
      ThumbSpend(2);
      return;
    }
    GenLoadConst(reg, ofs);
    printf2("\tadds\tr%d, r%d, r%d\n", reg, ThumbOpRegFp, reg);
    ThumbSpend(2);
  }
  else
  {
    /* ADDS and SUBS (register plus 3-bit immediate) name the destination and
       the source separately, so a slot within seven bytes of the frame
       pointer is one halfword instead of a materialized constant and a
       register subtract. The first local sits at -4 and is the common case. */
    if (ofs >= -7)
    {
      printf2("\tsubs\tr%d, r%d, #%d\n", reg, ThumbOpRegFp, -ofs);
      ThumbSpend(2);
      return;
    }
    GenLoadConst(reg, -ofs);
    printf2("\tsubs\tr%d, r%d, r%d\n", reg, ThumbOpRegFp, reg);
    ThumbSpend(2);
  }
}

/* Whether an offset fits the immediate form of a load or store of this size.
   LDR/STR scale by 4 over a 5-bit field, LDRH/STRH by 2, LDRB/STRB by 1. */
STATIC
int ThumbOfsFits(int opSz, int ofs)
{
  if (ofs < 0)
    return 0;
  if (opSz == -1 || opSz == 1)
    return ofs <= 31;
  if (opSz == -2 || opSz == 2)
    return ofs <= 62 && !(ofs & 1);
  return ofs <= 124 && !(ofs & 3);
}

/* LDRSB and LDRSH have no immediate-offset encoding in Thumb-1, so a signed
   narrow load is an unsigned load followed by a sign extension. */
STATIC
void ThumbEmitLoad(int regDst, int opSz, int regBase, int ofs)
{
  char* op = "ldr";

  if (opSz == -1 || opSz == 1)
    op = "ldrb";
  else if (opSz == -2 || opSz == 2)
    op = "ldrh";

  printf2("\t%s\tr%d, [r%d, #%d]\n", op, regDst, regBase, ofs);
  ThumbSpend(2);

  if (opSz == -1)
  {
    printf2("\tsxtb\tr%d, r%d\n", regDst, regDst);
    ThumbSpend(2);
  }
  else if (opSz == -2)
  {
    printf2("\tsxth\tr%d, r%d\n", regDst, regDst);
    ThumbSpend(2);
  }
}

STATIC
void ThumbEmitStore(int regSrc, int opSz, int regBase, int ofs)
{
  char* op = "str";

  if (opSz == -1 || opSz == 1)
    op = "strb";
  else if (opSz == -2 || opSz == 2)
    op = "strh";

  printf2("\t%s\tr%d, [r%d, #%d]\n", op, regSrc, regBase, ofs);
  ThumbSpend(2);
}

STATIC
void GenExtendRegIfNeeded(int reg, int opSz)
{
  if (opSz == -1)
    printf2("\tsxtb\tr%d, r%d\n", reg, reg);
  else if (opSz == 1)
    printf2("\tuxtb\tr%d, r%d\n", reg, reg);
  else if (opSz == -2)
    printf2("\tsxth\tr%d, r%d\n", reg, reg);
  else if (opSz == 2)
    printf2("\tuxth\tr%d, r%d\n", reg, reg);
  else
    return;
  ThumbSpend(2);
}

STATIC
void GenReadIdent(int regDst, int opSz, int label)
{
  GenLoadLabelAddr(ThumbOpRegAddr, label);
  ThumbEmitLoad(regDst, opSz, ThumbOpRegAddr, 0);
}

STATIC
void GenReadLocal(int regDst, int opSz, int ofs)
{
  if (ThumbOfsFits(opSz, ofs))
  {
    ThumbEmitLoad(regDst, opSz, ThumbOpRegFp, ofs);
  }
  else
  {
    GenLocalAddr(ThumbOpRegAddr, ofs);
    ThumbEmitLoad(regDst, opSz, ThumbOpRegAddr, 0);
  }
}

STATIC
void GenReadIndirect(int regDst, int regSrc, int opSz)
{
  ThumbEmitLoad(regDst, opSz, regSrc, 0);
}

STATIC
void GenWriteIdent(int regSrc, int opSz, int label)
{
  GenLoadLabelAddr(ThumbOpRegAddr, label);
  ThumbEmitStore(regSrc, opSz, ThumbOpRegAddr, 0);
}

STATIC
void GenWriteLocal(int regSrc, int opSz, int ofs)
{
  if (ThumbOfsFits(opSz, ofs))
  {
    ThumbEmitStore(regSrc, opSz, ThumbOpRegFp, ofs);
  }
  else
  {
    GenLocalAddr(ThumbOpRegAddr, ofs);
    ThumbEmitStore(regSrc, opSz, ThumbOpRegAddr, 0);
  }
}

STATIC
void GenWriteIndirect(int regDst, int regSrc, int opSz)
{
  ThumbEmitStore(regSrc, opSz, regDst, 0);
}

/* Condition codes indexed by [unsigned][op], with op numbered
   0 '<', 1 '<=', 2 '>', 3 '>=', 4 '==', 5 '!='. */
char* ThumbCond[2][6] =
{
  { "lt", "le", "gt", "ge", "eq", "ne" },
  { "lo", "ls", "hi", "hs", "eq", "ne" }
};

char* ThumbCondInv[2][6] =
{
  { "ge", "gt", "le", "lt", "ne", "eq" },
  { "hs", "hi", "ls", "lo", "ne", "eq" }
};

/* An unconditional jump is a BL, whose +-16MB reach makes it the only
   branch that is always in range. lr is dead throughout the body because
   the prolog saved it and the epilog returns through r3. */
STATIC
void GenJumpUncond(int label)
{
  printf2("\tbl\t");
  GenPrintNumLabel(label);
  puts2("");
  ThumbSpend(4);
}

/* A conditional jump is the inverted condition branching over that BL, so
   the only short branch is the six bytes to the label just below it. */
STATIC
void GenCondJump(char* skipCond, int label)
{
  int l = ThumbLocalLabel++;
  printf2("\tb%s\t.LT%d\n", skipCond, l);
  printf2("\tbl\t");
  GenPrintNumLabel(label);
  puts2("");
  printf2(".LT%d:\n", l);
  ThumbSpend(6);
}

extern int GenWreg;

STATIC
void ThumbCmpConst(int reg, int val)
{
  int v = truncInt(val);

  if (v >= 0 && v <= 255)
  {
    printf2("\tcmp\tr%d, #%d\n", reg, v);
    ThumbSpend(2);
  }
  else
  {
    GenLoadConst(ThumbOpRegCnst, v);
    printf2("\tcmp\tr%d, r%d\n", reg, ThumbOpRegCnst);
    ThumbSpend(2);
  }
}

STATIC
void GenJumpIfEqual(int val, int label)
{
  ThumbCmpConst(GenWreg, val);
  GenCondJump("ne", label);
}

STATIC
void GenJumpIfZero(int label)
{
#ifndef NO_ANNOTATIONS
  printf2(" @ JumpIfZero\n");
#endif
  printf2("\tcmp\tr%d, #0\n", GenWreg);
  ThumbSpend(2);
  GenCondJump("ne", label);
}

STATIC
void GenJumpIfNotZero(int label)
{
#ifndef NO_ANNOTATIONS
  printf2(" @ JumpIfNotZero\n");
#endif
  printf2("\tcmp\tr%d, #0\n", GenWreg);
  ThumbSpend(2);
  GenCondJump("eq", label);
}

/* The frame size is unknown until the body has been parsed, so the prolog
   refers to it through a symbol that the epilog defines with .equ. That
   removes any need to seek back over the output and leaves the frame size
   unbounded. */
STATIC
void GenFxnProlog(void)
{
  ThumbFrameLabel = ThumbFrameSeq++;

  /* Rebuild parameters 1-4 into a home area directly below the stacked
     parameters, so that all parameters are contiguous from [r7, #8] up. */
  puts2("\tpush\t{r0, r1, r2, r3}");
  puts2("\tpush\t{r7, lr}");
  printf2("\tmov\tr%d, sp\n", ThumbOpRegFp);
  /* The frame symbol carries the negated size so that the reserve is the
     one SP-by-register form ARMv6-M offers. Thumb-1 encodes ADD (SP plus
     register) and no SUB (SP plus register), so adding a negative is what
     replaces a materialize-subtract-writeback triple. */
  printf2("\tldr\tr%d, =.LF%d\n", ThumbOpRegCall, ThumbFrameLabel);
  printf2("\tadd\tsp, r%d\n", ThumbOpRegCall);
  /* r0 is a pad keeping the push a multiple of eight bytes. */
  puts2("\tpush\t{r0, r4, r5, r6}");
  ThumbSpend(16);
}

STATIC
void GenFxnEpilog(void)
{
  unsigned size = -CurFxnMinLocalOfs;

  size = (size + 7) & ~7u;

  puts2("\tpop\t{r3, r4, r5, r6}");
  printf2("\tmov\tsp, r%d\n", ThumbOpRegFp);
  printf2("\tpop\t{r%d}\n", ThumbOpRegFp);
  printf2("\tpop\t{r%d}\n", ThumbOpRegCall);
  /* Discard the home area the prolog pushed; the caller removed the rest. */
  puts2("\tadd\tsp, #16");
  printf2("\tbx\tr%d\n", ThumbOpRegCall);
  printf2("\t.equ\t.LF%d, -%u\n", ThumbFrameLabel, size);
  puts2("\t.ltorg");
  ThumbPoolBytes = 0;
}

/* The interrupt attribute has no meaning for this target: a DiscoBSD user
   process never installs a handler, and the ARMv6-M exception entry sequence
   is not what GenFxnProlog builds. */
void GenIsrProlog(void)
{
  errorInternal(105);
}

void GenIsrEpilog(void)
{
  errorInternal(106);
}

STATIC
int GenMaxLocalsSize(void)
{
  return 0x7FFFFFFF;
}

/* rd = rl op rr, with every operand in a low register. Thumb-1 data
   processing is mostly two-operand and destructive, so an operand that is
   not already the destination is moved first; a non-commutative operator
   whose destination aliases its right operand stages that operand through
   the constant scratch. */
STATIC
void ThumbBinOpReg(int tok, int rd, int rl, int rr)
{
  char* op;
  int commutative = 0;
  int shift = 0;

  switch (tok)
  {
  case tokPostAdd: case tokAssignAdd: case '+':
    printf2("\tadds\tr%d, r%d, r%d\n", rd, rl, rr);
    ThumbSpend(2);
    return;
  case tokPostSub: case tokAssignSub: case '-':
    printf2("\tsubs\tr%d, r%d, r%d\n", rd, rl, rr);
    ThumbSpend(2);
    return;
  case '&': case tokAssignAnd:      op = "ands"; commutative = 1; break;
  case '^': case tokAssignXor:      op = "eors"; commutative = 1; break;
  case '|': case tokAssignOr:       op = "orrs"; commutative = 1; break;
  case '*': case tokAssignMul:      op = "muls"; commutative = 1; break;
  case tokLShift: case tokAssignLSh:  op = "lsls"; shift = 1; break;
  case tokRShift: case tokAssignRSh:  op = "asrs"; shift = 1; break;
  case tokURShift: case tokAssignURSh: op = "lsrs"; shift = 1; break;
  default:
    errorInternal(101);
    return;
  }

  if (commutative && rd == rr && rd != rl)
  {
    /* Fold the move away by taking the operands in the other order. */
    rr = rl;
    rl = rd;
  }
  else if (shift && rd == rr && rd != rl)
  {
    ThumbMov(ThumbOpRegCnst, rr);
    rr = ThumbOpRegCnst;
  }

  if (rd != rl)
  {
    ThumbMov(rd, rl);
  }

  if (tok == '*' || tok == tokAssignMul)
    printf2("\tmuls\tr%d, r%d, r%d\n", rd, rr, rd);
  else
    printf2("\t%s\tr%d, r%d\n", op, rd, rr);
  ThumbSpend(2);
}

/* rd = rl op imm. The immediate forms Thumb-1 offers are ADDS/SUBS with an
   8-bit value into the destination and the shifts with a 5-bit count;
   everything else materializes the constant and uses the register form. */
STATIC
void ThumbBinOpConst(int tok, int rd, int rl, int val)
{
  int v = truncInt(val);

  switch (tok)
  {
  case tokPostAdd: case tokAssignAdd: case '+':
  case tokPostSub: case tokAssignSub: case '-':
    if (tok == '-' || tok == tokPostSub || tok == tokAssignSub)
      v = -v;
    if (rd == rl && v >= 0 && v <= 255)
    {
      if (v)
      {
        printf2("\tadds\tr%d, #%d\n", rd, v);
        ThumbSpend(2);
      }
      return;
    }
    if (rd == rl && v < 0 && v >= -255)
    {
      printf2("\tsubs\tr%d, #%d\n", rd, -v);
      ThumbSpend(2);
      return;
    }
    GenLoadConst(ThumbOpRegCnst, v);
    printf2("\tadds\tr%d, r%d, r%d\n", rd, rl, ThumbOpRegCnst);
    ThumbSpend(2);
    return;

  case tokLShift: case tokAssignLSh:
  case tokRShift: case tokAssignRSh:
  case tokURShift: case tokAssignURSh:
    {
      char* op = "lsls";
      int arith = (tok == tokRShift || tok == tokAssignRSh);
      if (arith)
        op = "asrs";
      else if (tok == tokURShift || tok == tokAssignURSh)
        op = "lsrs";

      /* A shift count of zero must not use the shift encoding, where a zero
         count in LSRS and ASRS means a shift by 32. */
      if (!v)
      {
        if (rd != rl)
        {
          printf2("\tmovs\tr%d, r%d\n", rd, rl);
          ThumbSpend(2);
        }
        return;
      }
      /* Counts of 32 and above are undefined in C; produce the result the
         architecture gives for the largest encodable count. */
      if (v < 0 || v > 31)
      {
        if (arith)
          v = 31;
        else
        {
          printf2("\tmovs\tr%d, #0\n", rd);
          ThumbSpend(2);
          return;
        }
      }
      printf2("\t%s\tr%d, r%d, #%d\n", op, rd, rl, v);
      ThumbSpend(2);
    }
    return;

  default:
    GenLoadConst(ThumbOpRegCnst, v);
    ThumbBinOpReg(tok, rd, rl, ThumbOpRegCnst);
    return;
  }
}

/* Integer division and modulo go to the libgcc AAPCS helpers, which take the
   numerator in r0 and the denominator in r1. The denominator is staged
   through ip so that a right operand already sitting in r0 survives. These
   helpers are public interfaces reached from an arbitrary expression depth,
   so the BL takes the same 8-byte alignment pad an ordinary call site gets;
   they pass nothing on the stack, so the pad is the whole adjustment. */
STATIC
void ThumbDivMod(int rd, int rl, int rr, int isSigned, int wantMod)
{
  int pad = ThumbSpDepth & 4;

  ThumbMov(ThumbOpRegIp, rr);
  ThumbMov(ThumbOpRegW0, rl);
  ThumbMov(ThumbOpRegAddr, ThumbOpRegIp);
  if (pad)
    puts2("\tsub\tsp, #4");
  if (wantMod)
    printf2("\tbl\t%s\n", isSigned ? "__aeabi_idivmod" : "__aeabi_uidivmod");
  else
    printf2("\tbl\t%s\n", isSigned ? "__aeabi_idiv" : "__aeabi_uidiv");
  if (pad)
    puts2("\tadd\tsp, #4");
  ThumbMov(rd, wantMod ? ThumbOpRegAddr : ThumbOpRegW0);
  ThumbSpend(pad ? 16 : 12);
}

STATIC
void GenIncDecIdent(int regDst, int opSz, int label, int tok)
{
  GenReadIdent(regDst, opSz, label);
  ThumbBinOpConst('+', regDst, regDst, (tok == tokInc) ? 1 : -1);
  GenWriteIdent(regDst, opSz, label);
  GenExtendRegIfNeeded(regDst, opSz);
}

STATIC
void GenIncDecLocal(int regDst, int opSz, int ofs, int tok)
{
  GenReadLocal(regDst, opSz, ofs);
  ThumbBinOpConst('+', regDst, regDst, (tok == tokInc) ? 1 : -1);
  GenWriteLocal(regDst, opSz, ofs);
  GenExtendRegIfNeeded(regDst, opSz);
}

STATIC
void GenIncDecIndirect(int regDst, int regSrc, int opSz, int tok)
{
  GenReadIndirect(regDst, regSrc, opSz);
  ThumbBinOpConst('+', regDst, regDst, (tok == tokInc) ? 1 : -1);
  GenWriteIndirect(regSrc, regDst, opSz);
  GenExtendRegIfNeeded(regDst, opSz);
}

STATIC
void GenPostIncDecIdent(int regDst, int opSz, int label, int tok)
{
  int d = (tok == tokPostInc) ? 1 : -1;
  GenReadIdent(regDst, opSz, label);
  ThumbBinOpConst('+', regDst, regDst, d);
  GenWriteIdent(regDst, opSz, label);
  ThumbBinOpConst('+', regDst, regDst, -d);
  GenExtendRegIfNeeded(regDst, opSz);
}

STATIC
void GenPostIncDecLocal(int regDst, int opSz, int ofs, int tok)
{
  int d = (tok == tokPostInc) ? 1 : -1;
  GenReadLocal(regDst, opSz, ofs);
  ThumbBinOpConst('+', regDst, regDst, d);
  GenWriteLocal(regDst, opSz, ofs);
  ThumbBinOpConst('+', regDst, regDst, -d);
  GenExtendRegIfNeeded(regDst, opSz);
}

STATIC
void GenPostIncDecIndirect(int regDst, int regSrc, int opSz, int tok)
{
  int d = (tok == tokPostInc) ? 1 : -1;
  GenReadIndirect(regDst, regSrc, opSz);
  ThumbBinOpConst('+', regDst, regDst, d);
  GenWriteIndirect(regSrc, regDst, opSz);
  ThumbBinOpConst('+', regDst, regDst, -d);
  GenExtendRegIfNeeded(regDst, opSz);
}

int CanUseTempRegs;
int TempsUsed;
int GenWreg = ThumbOpRegW0;
int GenLreg, GenRreg;

STATIC
void GenWregInc(int inc)
{
  if (inc > 0)
  {
    if (GenWreg == ThumbOpRegW0)
      GenWreg = ThumbOpRegT0;
    else
      GenWreg++;
  }
  else
  {
    if (GenWreg == ThumbOpRegT0)
      GenWreg = ThumbOpRegW0;
    else
      GenWreg--;
  }
}

STATIC
void GenPushReg(void)
{
  if (CanUseTempRegs && TempsUsed < MAX_TEMP_REGS)
  {
    GenWregInc(1);
    TempsUsed++;
    return;
  }

  printf2("\tpush\t{r%d}\n", GenWreg);
  ThumbSpend(2);
  ThumbSpDepth += 4;
  TempsUsed++;
}

STATIC
void GenPopReg(void)
{
  TempsUsed--;

  if (CanUseTempRegs && TempsUsed < MAX_TEMP_REGS)
  {
    GenRreg = GenWreg;
    GenWregInc(-1);
    GenLreg = GenWreg;
    return;
  }

  printf2("\tpop\t{r%d}\n", TEMP_REG_A);
  ThumbSpend(2);
  ThumbSpDepth -= 4;
  GenLreg = TEMP_REG_A;
  GenRreg = GenWreg;
}

#define tokRevIdent    0x100
#define tokRevLocalOfs 0x101
#define tokAssign0     0x102
#define tokNum0        0x103
STATIC
void GenPrep(int* idx)
{
  int tok;
  int oldIdxRight, oldIdxLeft, t0, t1;

  if (*idx < 0)
    //error("GenFuse(): idx < 0\n");
    errorInternal(100);

  tok = stack[*idx][0];

  oldIdxRight = --*idx;

  switch (tok)
  {
  case tokUDiv:
  case tokUMod:
  case tokAssignUDiv:
  case tokAssignUMod:
    if (stack[oldIdxRight][0] == tokNumInt || stack[oldIdxRight][0] == tokNumUint)
    {
      // Change unsigned division to right shift and unsigned modulo to bitwise and
      unsigned m = truncUint(stack[oldIdxRight][1]);
      if (m && !(m & (m - 1)))
      {
        if (tok == tokUMod || tok == tokAssignUMod)
        {
          stack[oldIdxRight][1] = (int)(m - 1);
          tok = (tok == tokUMod) ? '&' : tokAssignAnd;
        }
        else
        {
          t1 = 0;
          while (m >>= 1) t1++;
          stack[oldIdxRight][1] = t1;
          tok = (tok == tokUDiv) ? tokURShift : tokAssignURSh;
        }
        stack[oldIdxRight + 1][0] = tok;
      }
    }
  }

  switch (tok)
  {
  case tokNumUint:
    stack[oldIdxRight + 1][0] = tokNumInt; // reduce the number of cases since tokNumInt and tokNumUint are handled the same way
    // fallthrough
  case tokNumInt:
  case tokNum0:
  case tokIdent:
  case tokLocalOfs:
    break;

  case tokPostAdd:
  case tokPostSub:
  case '-':
  case '/':
  case '%':
  case tokUDiv:
  case tokUMod:
  case tokLShift:
  case tokRShift:
  case tokURShift:
  case tokLogAnd:
  case tokLogOr:
  case tokComma:
    GenPrep(idx);
    // fallthrough
  case tokShortCirc:
  case tokGoto:
  case tokUnaryStar:
  case tokInc:
  case tokDec:
  case tokPostInc:
  case tokPostDec:
  case '~':
  case tokUnaryPlus:
  case tokUnaryMinus:
  case tok_Bool:
  case tokVoid:
  case tokUChar:
  case tokSChar:
  case tokShort:
  case tokUShort:
    GenPrep(idx);
    break;

  case '=':
    if (oldIdxRight + 1 == sp - 1 &&
        (stack[oldIdxRight][0] == tokNumInt || stack[oldIdxRight][0] == tokNumUint) &&
        truncUint(stack[oldIdxRight][1]) == 0)
    {
      // Special case for assigning 0 while throwing away the expression result value
      // TBD??? ,
      stack[oldIdxRight][0] = tokNum0; // this zero constant will not be loaded into a register
      stack[oldIdxRight + 1][0] = tokAssign0; // change '=' to tokAssign0
    }
    // fallthrough
  case tokAssignAdd:
  case tokAssignSub:
  case tokAssignMul:
  case tokAssignDiv:
  case tokAssignUDiv:
  case tokAssignMod:
  case tokAssignUMod:
  case tokAssignLSh:
  case tokAssignRSh:
  case tokAssignURSh:
  case tokAssignAnd:
  case tokAssignXor:
  case tokAssignOr:
    GenPrep(idx);
    oldIdxLeft = *idx;
    GenPrep(idx);
    // If the left operand is an identifier (with static or auto storage), swap it with the right operand
    // and mark it specially, so it can be used directly
    if ((t0 = stack[oldIdxLeft][0]) == tokIdent || t0 == tokLocalOfs)
    {
      t1 = stack[oldIdxLeft][1];
      memmove(stack[oldIdxLeft], stack[oldIdxLeft + 1], (oldIdxRight - oldIdxLeft) * sizeof(stack[0]));
      stack[oldIdxRight][0] = (t0 == tokIdent) ? tokRevIdent : tokRevLocalOfs;
      stack[oldIdxRight][1] = t1;
    }
    break;

  case '+':
  case '*':
  case '&':
  case '^':
  case '|':
  case tokEQ:
  case tokNEQ:
  case '<':
  case '>':
  case tokLEQ:
  case tokGEQ:
  case tokULess:
  case tokUGreater:
  case tokULEQ:
  case tokUGEQ:
    GenPrep(idx);
    oldIdxLeft = *idx;
    GenPrep(idx);
    // If the right operand isn't a constant, but the left operand is, swap the operands
    // so the constant can become an immediate right operand in the instruction
    t1 = stack[oldIdxRight][0];
    t0 = stack[oldIdxLeft][0];
    if (t1 != tokNumInt && t0 == tokNumInt)
    {
      int xor;

      t1 = stack[oldIdxLeft][1];
      memmove(stack[oldIdxLeft], stack[oldIdxLeft + 1], (oldIdxRight - oldIdxLeft) * sizeof(stack[0]));
      stack[oldIdxRight][0] = t0;
      stack[oldIdxRight][1] = t1;

      switch (tok)
      {
      case '<':
      case '>':
        xor = '<' ^ '>'; break;
      case tokLEQ:
      case tokGEQ:
        xor = tokLEQ ^ tokGEQ; break;
      case tokULess:
      case tokUGreater:
        xor = tokULess ^ tokUGreater; break;
      case tokULEQ:
      case tokUGEQ:
        xor = tokULEQ ^ tokUGEQ; break;
      default:
        xor = 0; break;
      }
      tok ^= xor;
    }
    // Handle a few special cases and transform the instruction
    if (stack[oldIdxRight][0] == tokNumInt)
    {
      unsigned m = truncUint(stack[oldIdxRight][1]);
      switch (tok)
      {
      case '*':
        // Change multiplication to left shift, this helps indexing arrays of ints/pointers/etc
        if (m && !(m & (m - 1)))
        {
          t1 = 0;
          while (m >>= 1) t1++;
          stack[oldIdxRight][1] = t1;
          tok = tokLShift;
        }
        break;
      case tokLEQ:
        // left <= const will later change to left < const+1, but const+1 must be <=0x7FFFFFFF
        if (m == 0x7FFFFFFF)
        {
          // left <= 0x7FFFFFFF is always true, change to the equivalent left >= 0u
          stack[oldIdxRight][1] = 0;
          tok = tokUGEQ;
        }
        break;
      case tokULEQ:
        // left <= const will later change to left < const+1, but const+1 must be <=0xFFFFFFFFu
        if (m == 0xFFFFFFFF)
        {
          // left <= 0xFFFFFFFFu is always true, change to the equivalent left >= 0u
          stack[oldIdxRight][1] = 0;
          tok = tokUGEQ;
        }
        break;
      case '>':
        // left > const will later change to !(left < const+1), but const+1 must be <=0x7FFFFFFF
        if (m == 0x7FFFFFFF)
        {
          // left > 0x7FFFFFFF is always false, change to the equivalent left & 0
          stack[oldIdxRight][1] = 0;
          tok = '&';
        }
        break;
      case tokUGreater:
        // left > const will later change to !(left < const+1), but const+1 must be <=0xFFFFFFFFu
        if (m == 0xFFFFFFFF)
        {
          // left > 0xFFFFFFFFu is always false, change to the equivalent left & 0
          stack[oldIdxRight][1] = 0;
          tok = '&';
        }
        break;
      }
    }
    stack[oldIdxRight + 1][0] = tok;
    break;

  case ')':
    while (stack[*idx][0] != '(')
    {
      GenPrep(idx);
      if (stack[*idx][0] == ',')
        --*idx;
    }
    --*idx;
    break;

  default:
    //error("GenPrep: unexpected token %s\n", GetTokenName(tok));
    errorInternal(101);
  }
}


/* ARM compares by setting flags, so a comparison feeding a conditional jump
   is just CMP and a branch. A comparison whose value is used materializes
   the 0 or 1 with a short forward branch, which is always in range because
   both arms are two instructions away. */
STATIC
void GenCmp(int* idx, int op)
{
  int isConst = (stack[*idx - 1][0] == tokNumInt);
  int condbranch = (*idx + 1 < sp) ?
                   (stack[*idx + 1][0] == tokIf) + (stack[*idx + 1][0] == tokIfNot) * 2 : 0;
  int unsign = op >> 4;
  int label = condbranch ? stack[*idx + 1][1] : 0;

  op &= 0xF;

  if (isConst)
  {
    ThumbCmpConst(GenWreg, stack[*idx - 1][1]);
  }
  else
  {
    GenPopReg();
    printf2("\tcmp\tr%d, r%d\n", GenLreg, GenRreg);
    ThumbSpend(2);
  }

  if (condbranch)
  {
    /* condbranch 1 jumps when the comparison holds, so the branch that skips
       the jump carries the inverted condition; condbranch 2 is the reverse. */
    GenCondJump((condbranch == 1) ? ThumbCondInv[unsign][op] : ThumbCond[unsign][op], label);
  }
  else
  {
    int lTrue = ThumbLocalLabel++;
    int lEnd = ThumbLocalLabel++;
    printf2("\tb%s\t.LT%d\n", ThumbCond[unsign][op], lTrue);
    printf2("\tmovs\tr%d, #0\n", GenWreg);
    printf2("\tb\t.LT%d\n", lEnd);
    printf2(".LT%d:\n", lTrue);
    printf2("\tmovs\tr%d, #1\n", GenWreg);
    printf2(".LT%d:\n", lEnd);
    ThumbSpend(8);
  }

  *idx += condbranch != 0;
}

STATIC
int GenIsCmp(int t)
{
  return
    t == '<' ||
    t == '>' ||
    t == tokGEQ ||
    t == tokLEQ ||
    t == tokULess ||
    t == tokUGreater ||
    t == tokUGEQ ||
    t == tokULEQ ||
    t == tokEQ ||
    t == tokNEQ;
}

#ifndef NO_STRUCT_BY_VAL
/* The structure-pushing helper of GenFin is the one callee that does not
   leave the stack as it found it: it grows the stack by the word-rounded
   size of the structure, returns the first word for the evaluator to push
   as an argument, and leaves the rest in place as part of the caller's
   argument area. Recognizing its call sites takes the numeric identifier
   the front end plants for it, and its size argument is the constant the
   front end plants immediately after the opening parenthesis. */
STATIC
int ThumbIsStructPushCall(int closeIdx)
{
  return StructPushLabel &&
         closeIdx > 0 &&
         stack[closeIdx - 1][0] == tokIdent &&
         stack[closeIdx - 1][1] == AddNumericIdent(StructPushLabel);
}

STATIC
int ThumbMatchingClose(int openIdx)
{
  int depth = 0;
  int j;

  for (j = openIdx + 1; j < sp; j++)
  {
    if (stack[j][0] == '(')
    {
      depth++;
    }
    else if (stack[j][0] == ')')
    {
      if (!depth)
        break;
      depth--;
    }
  }
  return j;
}

STATIC
int ThumbMatchingOpen(int closeIdx)
{
  int depth = 0;
  int j;

  for (j = closeIdx - 1; j > 0; j--)
  {
    if (stack[j][0] == ')')
    {
      depth++;
    }
    else if (stack[j][0] == '(')
    {
      if (!depth)
        break;
      depth--;
    }
  }
  return j;
}

/* Bytes the structure-pushing helper leaves behind beyond the word the
   evaluator pushes for its result. The front end has already counted them
   in the enclosing call's argument size, so the enclosing closing
   parenthesis reclaims them with everything else. */
STATIC
int ThumbCallLeavesBytes(int closeIdx)
{
  unsigned sz;

  if (!ThumbIsStructPushCall(closeIdx))
    return 0;

  sz = truncUint(stack[ThumbMatchingOpen(closeIdx) + 1][1]);
  return (int)(((sz + 3u) & ~3u) - 4u);
}
#endif

STATIC
void GenExpr0(void)
{
  int i;
  int gotUnary = 0;
  int maxCallDepth = 0;
  int callDepth = 0;
  int t = sp - 1;

  if (stack[t][0] == tokIf || stack[t][0] == tokIfNot || stack[t][0] == tokReturn)
    t--;
  GenPrep(&t);

  for (i = 0; i < sp; i++)
    switch (stack[i][0])
    {
    case '(':
      if (++callDepth > maxCallDepth)
        maxCallDepth = callDepth;
      break;
    case ')':
      callDepth--;
      break;
    /* ARMv6-M has no divide instruction, so every division and modulo is a
       BL to an __aeabi helper that takes its arguments in r0 and r1 and
       returns in them. A temporary left in r0 would not survive it, which
       the MIPS generator never has to consider because its division writes
       only HI and LO. Counting these as calls keeps the working register at
       r0 and puts every temporary on the stack, where the helper's
       caller-saved clobbers cannot reach it. */
    case '/':
    case '%':
    case tokUDiv:
    case tokUMod:
    case tokAssignDiv:
    case tokAssignUDiv:
    case tokAssignMod:
    case tokAssignUMod:
      if (maxCallDepth < 1)
        maxCallDepth = 1;
      break;
    }

  CanUseTempRegs = maxCallDepth == 0;
  TempsUsed = 0;
  ThumbSpDepth = 0;
  ThumbCallSp = 0;
  if (GenWreg != ThumbOpRegW0)
    errorInternal(102);

  for (i = 0; i < sp; i++)
  {
    int tok = stack[i][0];
    int v = stack[i][1];

    /* The only point at which no instruction sequence is open. */
    ThumbMaybeFlushPool();

#ifndef NO_ANNOTATIONS
    switch (tok)
    {
    case tokNumInt: printf2(" @ %d\n", truncInt(v)); break;
    case tokIdent: case tokRevIdent: printf2(" @ %s\n", IdentTable + v); break;
    case tokLocalOfs: case tokRevLocalOfs: printf2(" @ local ofs\n"); break;
    case ')': printf2(" @ ) fxn call\n"); break;
    case tokUnaryStar: printf2(" @ * (read dereference)\n"); break;
    case '=': printf2(" @ = (write dereference)\n"); break;
    case tokShortCirc: printf2(" @ short-circuit "); break;
    case tokGoto: printf2(" @ sh-circ-goto "); break;
    case tokLogAnd: printf2(" @ short-circuit && target\n"); break;
    case tokLogOr: printf2(" @ short-circuit || target\n"); break;
    case tokIf: case tokIfNot: case tokReturn: break;
    default: printf2(" @ %s\n", GetTokenName(tok)); break;
    }
#endif

    switch (tok)
    {
    case tokNumInt:
      if (!(i + 1 < sp && ((t = stack[i + 1][0]) == '+' ||
                           t == '-' ||
                           t == '&' ||
                           t == '^' ||
                           t == '|' ||
                           t == tokLShift ||
                           t == tokRShift ||
                           t == tokURShift ||
                           GenIsCmp(t))))
      {
        if (gotUnary)
          GenPushReg();

        GenLoadConst(GenWreg, v);
      }
      gotUnary = 1;
      break;

    case tokIdent:
      if (gotUnary)
        GenPushReg();
      if (!(i + 1 < sp && ((t = stack[i + 1][0]) == ')' ||
                           t == tokUnaryStar ||
                           t == tokInc ||
                           t == tokDec ||
                           t == tokPostInc ||
                           t == tokPostDec)))
      {
        GenLoadLabelAddr(GenWreg, v);
      }
      gotUnary = 1;
      break;

    case tokLocalOfs:
      if (gotUnary)
        GenPushReg();
      if (!(i + 1 < sp && ((t = stack[i + 1][0]) == tokUnaryStar ||
                           t == tokInc ||
                           t == tokDec ||
                           t == tokPostInc ||
                           t == tokPostDec)))
      {
        GenLocalAddr(GenWreg, v);
      }
      gotUnary = 1;
      break;

    case '(':
      if (gotUnary)
        GenPushReg();
      gotUnary = 0;
      /* AAPCS reserves no home area at the call site, so the only thing
         allocated here is the word that keeps SP 8-byte aligned at the BL.
         Reserving it before the arguments puts it above them, which leaves
         argument five at the callee's entry SP. Whether it is needed is a
         property of the depth this expression has already reached and not of
         the argument count alone: a call passing nothing on the stack still
         needs the pad when it is issued from an odd depth. */
      {
        int inRegs = (v > 16) ? 16 : v;
        int pad = (ThumbSpDepth + v - inRegs) & 4;

#ifndef NO_STRUCT_BY_VAL
        /* The structure-pushing helper returns with SP lowered by the
           structure it built, so a pad above its arguments could only be
           reclaimed by cutting into that structure. It is back-end-private
           code, a leaf that calls nothing and executes only MOV, SUBS, BICS,
           LDRB and STRB, so its entry carries no public-interface
           obligation and it is the one call site left unpadded. */
        if (ThumbIsStructPushCall(ThumbMatchingClose(i)))
          pad = 0;
#endif
        if (ThumbCallSp >= STACK_SIZE)
          errorInternal(107);
        ThumbCallPad[ThumbCallSp++] = (unsigned char)pad;
        if (pad)
        {
          GenAddSp(-pad);
          ThumbSpDepth += pad;
        }
      }
      break;

    case ',':
      break;

    case ')':
      {
        /* Arguments occupy v bytes from sp upward, argument one lowest.
           The first four words move into r0-r3 and are then dropped, which
           leaves argument five at sp as AAPCS requires. The pad the opening
           parenthesis reserved sits above the whole block and is reclaimed
           with it, so SP returns to the depth the enclosing expression had. */
        int inRegs = (v > 16) ? 16 : v;
        int pad = ThumbCallPad[--ThumbCallSp];
        int k;
        int indirect = (stack[i - 1][0] != tokIdent);

        if (indirect)
        {
          /* ip is outside the range the argument loads overwrite. */
          ThumbMov(ThumbOpRegIp, GenWreg);
        }

        for (k = 0; k * 4 < inRegs; k++)
        {
          printf2("\tldr\tr%d, [sp, #%d]\n", k, k * 4);
          ThumbSpend(2);
        }

        GenAddSp(inRegs);
        ThumbSpDepth -= inRegs;

        if (indirect)
        {
          printf2("\tblx\tr%d\n", ThumbOpRegIp);
        }
        else
        {
          printf2("\tbl\t");
          GenPrintLabel(IdentTable + stack[i - 1][1]);
          puts2("");
        }
        ThumbSpend(4);

#ifndef NO_STRUCT_BY_VAL
        ThumbSpDepth += ThumbCallLeavesBytes(i);
#endif

        GenAddSp(v - inRegs + pad);
        ThumbSpDepth -= v - inRegs + pad;

        /* The result is in r0, which is GenWreg here because an expression
           containing a call never uses the temporary registers. */
      }
      break;

    case tokUnaryStar:
      if (stack[i - 1][0] == tokIdent)
        GenReadIdent(GenWreg, v, stack[i - 1][1]);
      else if (stack[i - 1][0] == tokLocalOfs)
        GenReadLocal(GenWreg, v, stack[i - 1][1]);
      else
        GenReadIndirect(GenWreg, GenWreg, v);
      break;

    case tokUnaryPlus:
      break;
    case '~':
      printf2("\tmvns\tr%d, r%d\n", GenWreg, GenWreg);
      ThumbSpend(2);
      break;
    case tokUnaryMinus:
      printf2("\tnegs\tr%d, r%d\n", GenWreg, GenWreg);
      ThumbSpend(2);
      break;

    case '+':
    case '-':
    case '*':
    case '&':
    case '^':
    case '|':
    case tokLShift:
    case tokRShift:
    case tokURShift:
      if (stack[i - 1][0] == tokNumInt && tok != '*')
      {
        ThumbBinOpConst(tok, GenWreg, GenWreg, stack[i - 1][1]);
      }
      else
      {
        GenPopReg();
        ThumbBinOpReg(tok, GenWreg, GenLreg, GenRreg);
      }
      break;

    case '/':
    case tokUDiv:
    case '%':
    case tokUMod:
      GenPopReg();
      ThumbDivMod(GenWreg, GenLreg, GenRreg,
                  tok == '/' || tok == '%',
                  tok == '%' || tok == tokUMod);
      break;

    case tokInc:
    case tokDec:
      if (stack[i - 1][0] == tokIdent)
      {
        GenIncDecIdent(GenWreg, v, stack[i - 1][1], tok);
      }
      else if (stack[i - 1][0] == tokLocalOfs)
      {
        GenIncDecLocal(GenWreg, v, stack[i - 1][1], tok);
      }
      else
      {
        ThumbMov(TEMP_REG_A, GenWreg);
        GenIncDecIndirect(GenWreg, TEMP_REG_A, v, tok);
      }
      break;
    case tokPostInc:
    case tokPostDec:
      if (stack[i - 1][0] == tokIdent)
      {
        GenPostIncDecIdent(GenWreg, v, stack[i - 1][1], tok);
      }
      else if (stack[i - 1][0] == tokLocalOfs)
      {
        GenPostIncDecLocal(GenWreg, v, stack[i - 1][1], tok);
      }
      else
      {
        ThumbMov(TEMP_REG_A, GenWreg);
        GenPostIncDecIndirect(GenWreg, TEMP_REG_A, v, tok);
      }
      break;

    case tokPostAdd:
    case tokPostSub:
      GenPopReg();
      if (GenWreg == GenLreg)
      {
        ThumbMov(TEMP_REG_B, GenLreg);
        GenReadIndirect(GenWreg, TEMP_REG_B, v);
        ThumbBinOpReg(tok, TEMP_REG_A, GenWreg, GenRreg);
        GenWriteIndirect(TEMP_REG_B, TEMP_REG_A, v);
      }
      else
      {
        ThumbMov(TEMP_REG_B, GenRreg);
        GenReadIndirect(GenWreg, GenLreg, v);
        ThumbBinOpReg(tok, TEMP_REG_B, GenWreg, TEMP_REG_B);
        GenWriteIndirect(GenLreg, TEMP_REG_B, v);
      }
      break;

    case tokAssignAdd:
    case tokAssignSub:
    case tokAssignMul:
    case tokAssignAnd:
    case tokAssignXor:
    case tokAssignOr:
    case tokAssignLSh:
    case tokAssignRSh:
    case tokAssignURSh:
      if (stack[i - 1][0] == tokRevLocalOfs || stack[i - 1][0] == tokRevIdent)
      {
        if (stack[i - 1][0] == tokRevLocalOfs)
          GenReadLocal(TEMP_REG_B, v, stack[i - 1][1]);
        else
          GenReadIdent(TEMP_REG_B, v, stack[i - 1][1]);

        ThumbBinOpReg(tok, GenWreg, TEMP_REG_B, GenWreg);

        if (stack[i - 1][0] == tokRevLocalOfs)
          GenWriteLocal(GenWreg, v, stack[i - 1][1]);
        else
          GenWriteIdent(GenWreg, v, stack[i - 1][1]);
      }
      else
      {
        int lsaved, rsaved;
        GenPopReg();
        if (GenWreg == GenLreg)
        {
          ThumbMov(TEMP_REG_B, GenLreg);
          lsaved = TEMP_REG_B;
          rsaved = GenRreg;
        }
        else
        {
          ThumbMov(TEMP_REG_B, GenRreg);
          rsaved = TEMP_REG_B;
          lsaved = GenLreg;
        }

        GenReadIndirect(GenWreg, GenLreg, v);
        ThumbBinOpReg(tok, GenWreg, GenWreg, rsaved);
        GenWriteIndirect(lsaved, GenWreg, v);
      }
      GenExtendRegIfNeeded(GenWreg, v);
      break;

    case tokAssignDiv:
    case tokAssignUDiv:
    case tokAssignMod:
    case tokAssignUMod:
      {
        int isSigned = (tok == tokAssignDiv || tok == tokAssignMod);
        int wantMod = (tok == tokAssignMod || tok == tokAssignUMod);

        if (stack[i - 1][0] == tokRevLocalOfs || stack[i - 1][0] == tokRevIdent)
        {
          if (stack[i - 1][0] == tokRevLocalOfs)
            GenReadLocal(TEMP_REG_B, v, stack[i - 1][1]);
          else
            GenReadIdent(TEMP_REG_B, v, stack[i - 1][1]);

          ThumbDivMod(GenWreg, TEMP_REG_B, GenWreg, isSigned, wantMod);

          if (stack[i - 1][0] == tokRevLocalOfs)
            GenWriteLocal(GenWreg, v, stack[i - 1][1]);
          else
            GenWriteIdent(GenWreg, v, stack[i - 1][1]);
        }
        else
        {
          int lsaved, rsaved;
          GenPopReg();
          if (GenWreg == GenLreg)
          {
            ThumbMov(TEMP_REG_B, GenLreg);
            lsaved = TEMP_REG_B;
            rsaved = GenRreg;
          }
          else
          {
            ThumbMov(TEMP_REG_B, GenRreg);
            rsaved = TEMP_REG_B;
            lsaved = GenLreg;
          }

          GenReadIndirect(GenWreg, GenLreg, v);
          ThumbDivMod(GenWreg, GenWreg, rsaved, isSigned, wantMod);
          GenWriteIndirect(lsaved, GenWreg, v);
        }
        GenExtendRegIfNeeded(GenWreg, v);
      }
      break;

    case '=':
      if (stack[i - 1][0] == tokRevLocalOfs)
      {
        GenWriteLocal(GenWreg, v, stack[i - 1][1]);
      }
      else if (stack[i - 1][0] == tokRevIdent)
      {
        GenWriteIdent(GenWreg, v, stack[i - 1][1]);
      }
      else
      {
        GenPopReg();
        GenWriteIndirect(GenLreg, GenRreg, v);
        if (GenWreg != GenRreg)
        {
          ThumbMov(GenWreg, GenRreg);
        }
      }
      GenExtendRegIfNeeded(GenWreg, v);
      break;

    case tokAssign0:
      /* No zero register exists, so one is materialized in the constant
         scratch, which is distinct from the address scratch a computed
         local or global destination uses. */
      printf2("\tmovs\tr%d, #0\n", ThumbOpRegCnst);
      ThumbSpend(2);
      if (stack[i - 1][0] == tokRevLocalOfs)
        GenWriteLocal(ThumbOpRegCnst, v, stack[i - 1][1]);
      else if (stack[i - 1][0] == tokRevIdent)
        GenWriteIdent(ThumbOpRegCnst, v, stack[i - 1][1]);
      else
        GenWriteIndirect(GenWreg, ThumbOpRegCnst, v);
      break;

    case '<':         GenCmp(&i, 0x00); break;
    case tokLEQ:      GenCmp(&i, 0x01); break;
    case '>':         GenCmp(&i, 0x02); break;
    case tokGEQ:      GenCmp(&i, 0x03); break;
    case tokULess:    GenCmp(&i, 0x10); break;
    case tokULEQ:     GenCmp(&i, 0x11); break;
    case tokUGreater: GenCmp(&i, 0x12); break;
    case tokUGEQ:     GenCmp(&i, 0x13); break;
    case tokEQ:       GenCmp(&i, 0x04); break;
    case tokNEQ:      GenCmp(&i, 0x05); break;

    case tok_Bool:
      /* (x | -x) >> 31 is one exactly when x is non-zero, including for
         INT_MIN, and needs no branch. */
      printf2("\tnegs\tr%d, r%d\n", ThumbOpRegCnst, GenWreg);
      printf2("\torrs\tr%d, r%d\n", ThumbOpRegCnst, GenWreg);
      printf2("\tlsrs\tr%d, r%d, #31\n", GenWreg, ThumbOpRegCnst);
      ThumbSpend(6);
      break;

    case tokSChar:
      printf2("\tsxtb\tr%d, r%d\n", GenWreg, GenWreg);
      ThumbSpend(2);
      break;
    case tokUChar:
      printf2("\tuxtb\tr%d, r%d\n", GenWreg, GenWreg);
      ThumbSpend(2);
      break;
    case tokShort:
      printf2("\tsxth\tr%d, r%d\n", GenWreg, GenWreg);
      ThumbSpend(2);
      break;
    case tokUShort:
      printf2("\tuxth\tr%d, r%d\n", GenWreg, GenWreg);
      ThumbSpend(2);
      break;

    case tokShortCirc:
#ifndef NO_ANNOTATIONS
      if (v >= 0)
        printf2("&&\n");
      else
        printf2("||\n");
#endif
      if (v >= 0)
        GenJumpIfZero(v);
      else
        GenJumpIfNotZero(-v);
      gotUnary = 0;
      break;
    case tokGoto:
#ifndef NO_ANNOTATIONS
      printf2("goto\n");
#endif
      GenJumpUncond(v);
      gotUnary = 0;
      break;
    case tokLogAnd:
    case tokLogOr:
      GenNumLabel(v);
      break;

    case tokVoid:
      gotUnary = 0;
      break;

    case tokRevIdent:
    case tokRevLocalOfs:
    case tokComma:
    case tokReturn:
    case tokNum0:
      break;

    case tokIf:
      GenJumpIfNotZero(stack[i][1]);
      break;
    case tokIfNot:
      GenJumpIfZero(stack[i][1]);
      break;

    default:
      errorInternal(103);
      break;
    }
  }

  if (GenWreg != ThumbOpRegW0)
    errorInternal(104);
  /* Every push the evaluator made has been reclaimed and every call site
     has popped its pad; an imbalance here would misalign the next call. */
  if (ThumbSpDepth || ThumbCallSp)
    errorInternal(108);
}

STATIC
void GenDumpChar(int ch)
{
  if (ch < 0)
  {
    if (TokenStringLen)
      printf2("\"\n");
    return;
  }

  if (TokenStringLen == 0)
  {
    GenStartAsciiString();
    printf2("\"");
  }

  if (ch >= 0x20 && ch <= 0x7E)
  {
    if (ch == '"' || ch == '\\')
      printf2("\\");
    printf2("%c", ch);
  }
  else
  {
    printf2("\\%03o", ch);
  }
}

STATIC
void GenExpr(void)
{
  GenExpr0();
}

/* The two runtime helpers the front end synthesizes calls to. Both are
   hand-written leaves: they take their arguments in r0-r3 under AAPCS and
   never build the home area a compiled prolog does. Each copies backwards so
   that the loop counter doubles as the byte index, and because loads and
   stores leave the flags alone, the counter's SUBS still governs the branch.
   Both require a size of at least one byte, which a complete structure or
   union type always has. */
STATIC
void GenFin(void)
{
  if (StructCpyLabel)
  {
    int lbl = ThumbLocalLabel++;

    puts2(CodeHeaderFooter[0]);
    puts2("\t.thumb_func");
    GenNumLabel(StructCpyLabel);

    /* r0 = count, r1 = source, r2 = destination; returns the destination. */
    puts2("\tpush\t{r2}");
    printf2(".LT%d:\n", lbl);
    puts2("\tsubs\tr0, #1\n"
          "\tldrb\tr3, [r1, r0]\n"
          "\tstrb\tr3, [r2, r0]");
    printf2("\tbne\t.LT%d\n", lbl);
    puts2("\tpop\t{r0}\n"
          "\tbx\tlr");
    puts2("\t.ltorg");

    puts2(CodeHeaderFooter[1]);
  }

#ifndef NO_STRUCT_BY_VAL
  if (StructPushLabel)
  {
    int lbl = ThumbLocalLabel++;

    puts2(CodeHeaderFooter[0]);
    puts2("\t.thumb_func");
    GenNumLabel(StructPushLabel);

    /* r0 = source, r1 = size. The structure is copied into freshly grown
       stack space and all but its first word is left there; the first word
       is returned so that the caller pushes it as one more argument word,
       which is the contract GenExpr0 expects of every call. */
    puts2("\tadds\tr2, r1, #3\n"
          "\tmovs\tr3, #3\n"
          "\tbics\tr2, r3\n"
          "\tmov\tr3, sp\n"
          "\tsubs\tr3, r3, r2\n"
          "\tmov\tsp, r3\n"
          "\tmov\tr2, r3");
    printf2(".LT%d:\n", lbl);
    puts2("\tsubs\tr1, #1\n"
          "\tldrb\tr3, [r0, r1]\n"
          "\tstrb\tr3, [r2, r1]");
    printf2("\tbne\t.LT%d\n", lbl);
    puts2("\tldr\tr0, [r2, #0]\n"
          "\tadd\tsp, #4\n"
          "\tbx\tlr");
    puts2("\t.ltorg");

    puts2(CodeHeaderFooter[1]);
  }
#endif
}
