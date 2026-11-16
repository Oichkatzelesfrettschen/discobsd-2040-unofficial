# Two-Way primary proof and port correspondence

The author-hosted original is https://www-igm.univ-eiffel.fr/~mac/Articles-PDF/CP-1991-jacm.pdf.
The download completes with ordinary certificate verification on 2026-11-15 UTC.
Its SHA-256 is d0eb61ae8f875c43b5eb85999581201785aed0e1a09d8411626cb1171e9d967d.
The 25-page PDF identifies Crochemore and Perrin, Two-Way String-Matching,
JACM 38(3), July 1991, pages 651-675, DOI 10.1145/116825.116845.
Poppler pdftotext -layout extracts its text; its SHA-256 is
f79cae27dd6bed6a8cbf97c5b5110157314fcdd5e5e9a30f17fd844d04fd352c.
The image of printed page 670 verifies the extracted period propositions.
Theorem 3.1 and its proof, Propositions 4.1/4.2 and 5.1/5.2/5.3, and
Propositions 2.2/2.3 supply the inspected correctness and comparison premises.
This record supersedes the unread-original boundary for those proof sections.

Let m be needle length, s the split count, P the whole-needle least period,
p the least period of the selected maximal suffix, and M the remembered prefix.
Theorem 3.1 (printed pp.663-664) selects the shorter of the two maximal suffixes
under opposite alphabet orders and establishes s<P. The C sentinel suffix index
normalizes to s through +1. Choosing the larger normalized suffix position
therefore implements the theorem. Proposition 4.1 and Figure 17
(pp.665-668) justify the maximal-suffix state transitions; Proposition 4.2
bounds the abstract preprocessing comparisons. Each suffix pair computes its three-way order once. The potential i+j+k
increases after each pair and stays below 2m-1 in normalized zero-based
coordinates. Both passes cost at most 4m-4 order evaluations; period
validation adds at most m-1.

The suffix period satisfies p<=m-s, so the prefix comparison at needle+p for
s bytes stays inside the needle. A successful comparison makes p a period of
the entire needle. Every whole-needle period is a period of the suffix; p is
the suffix's least period. Hence P=p and s<p. This argument also applies
when s is large; it needs no separate half-length branch. A failed comparison
enters the large-period branch of Proposition 5.1 (pp.669-670), where
P>max(s,m-s). When s>=m/2, Theorem 3.1 independently supplies P>s and gives
the same lower bound. In both cases the chosen port shift is at most P.

The paper's nonperiodic shift is q_paper=max(s,m-s)+1. The donor/port uses
q=max(s,m-s+1). The difference occurs exactly when s>m-s, where q=s and
q_paper=s+1. Both preserve correctness because 0<q<=P. The original
Proposition 5.2 uses the stronger sufficient condition q>max(s,m-s) for
its 2n comparison proof. The port satisfies the precise conditions the
proof needs: q>=s and q+s>=m+1. These are the minimal integer conditions
q>=s and q>m-s. Thus:

- A right mismatch at index k shifts k-s+1, so the next right comparison
  starts at the following haystack byte, pos+k+1.
- A completed right scan followed by a left failure shifts q. The next right
  scan starts at pos+q+s>=pos+m+1, beyond the completed right scan.
- Left scans use intervals contained in [pos,pos+s). Successive left scans
  have at least q>=s position advance and therefore use disjoint intervals.

Each scan direction consequently evaluates at most n byte pairs. This explains
the one-byte donor/exposition difference without replacing code to match a
stronger sufficient hypothesis. It supplies the missing correspondence
argument; the finite period model remains separate corroboration.

For the periodic branch, Figure 21's right-mismatch shift is
max(k-s+1,M-p+1), with zero-based k. The port starts k>=max(s,M), and
s<p, so k-s+1>=M-s+1>M-p+1. Its simpler k-s+1 shift equals that maximum.
The remembered prefix M=m-p preserves the source's periodic invariant.
Propositions 2.2/2.3 therefore supply first-match correctness and the 2n
search charge.

The public wrapper handles empty/oversized needles before any byte access.
A one-byte needle uses memchr with at most n byte comparisons. A two-byte
needle checks at most two byte pairs at each admitted start, costing at most
2n. Both paths preserve first-match order and extent containment.

For longer needles, an optional memchr filter searches n-m+1 admitted starts.
Its pointer subtraction occurs only when the original haystack extent fits
PTRDIFF_MAX. The uintmax_t comparison preserves both operands before testing
the bound. The located pointer shares the haystack array and its offset fits
ptrdiff_t. Larger valid arrays bypass this acceleration and retain the full
remaining-length core. A forced unfiltered fixture separately exercises that
path without allocating an object larger than the host address space.

A complete initial comparison returns a match before preprocessing. A failed
probe excludes exactly its candidate, so the core starts one byte later.
With F skipped prefix bytes and remaining core extent R<=n-F-1, the optional
filter costs at most F+1 units, the first probe at most m, both suffix passes
at most 4m-4, period validation at most m-1, and the core at most 2R. Their
sum is at most 2n+6m-6-F and fits the conservative gate 2n+7m. A missing
filter candidate or exhausted remainder returns earlier. This decomposition
includes every wrapper scan; source simplification alone supplies no bound.

The fixture counts each MEMMEM_ORDER evaluation and each scalar memchr
byte comparison as one work unit. The count supplies neither machine cycles
nor individual relational operators inside the macro. Its arguments are pure
in-bounds array accesses. The unoptimized source expression evaluates at most
four byte lvalues per order evaluation, so 8n+28m is a conservative source
byte-access-evaluation bound for this provider. Compiler-generated instruction
counts, memory traffic and elapsed time require their own evidence.

The algorithm and the donor's smaller shift have established prior art.
The contribution is an explicit correspondence among the primary theorem,
integer-index C implementation and constrained target. A changed critical
split, period check, memory-reset rule or shift requires a fresh proof and
witness review. Source/counter instrumentation correspondence and the exact
oracle/control receipts remain necessary; literature alone supplies no port
execution evidence. The original aggregate receipt binds its exact 312-byte source. Guarded-prefix
qualification, installed resource admission and board timing retain their
separate evidence requirements.
