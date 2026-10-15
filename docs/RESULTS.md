# ParserX v2 — Experimental Results

All numbers below were produced by the `parserx` binary built from this
repository on **2026-10-15** (Windows 11, g++ 14.2.0 from MSYS2 UCRT64,
`-std=c++17 -O2`, static link). Every section gives the exact command
(run from the project root) and an excerpt of its raw output. Counts, state
numbers and fuzzing statistics are deterministic (fixed seed `20261015`
unless stated); **timings are machine-dependent** and vary between runs.

Contents

1. [Table sizes: v1, v2 and the demo grammars](#1-table-sizes)
2. [SLR vs LALR vs CLR inside a realistic language (v2)](#2-slr-vs-lalr-vs-clr-in-v2)
3. [Ambiguous grammar + precedence vs layered grammar](#3-ambiguous--precedence-vs-layered)
4. [LALR(1): DeRemer–Pennello vs merging CLR states](#4-lalr-deremerpennello-vs-merge)
5. [PGM (Pager's minimal LR(1))](#5-pgm-pagers-minimal-lr1)
6. [Differential fuzzing](#6-differential-fuzzing)
7. [Precision index](#7-precision-index)
8. [Table compression and its effect on error detection](#8-table-compression)
9. [Conflict explanations and witnesses](#9-conflict-explanations-and-witnesses)
10. [Grammar classifier and the grammar zoo](#10-classifier-and-zoo)
11. [Random grammar study](#11-random-grammar-study)
12. [GLR parse counts](#12-glr-parse-counts)
13. [Error recovery](#13-error-recovery)
14. [Benchmarks](#14-benchmarks)
15. [Where reality differed from the brief](#15-where-reality-differed-from-the-brief)

---

## 1. Table sizes

```
for g in language language_v2 language_v2_ambiguous expr_ambiguous expr_layered slr_vs_lalr lalr_vs_clr epsilon; do
  ./parserx --grammar grammar/$g.txt --code "" --trace none | grep -E "^TABLE"
done
```

| Grammar | SLR states / ACTION / GOTO | LALR | CLR | PGM | Unresolved conflicts (SLR / LALR / CLR / PGM) |
|---|---|---|---|---|---|
| v1 `language.txt` | 37 / 157 / 28 | 37 / 157 / 28 | 111 / 396 / 82 | 37 / 157 / 28 | 1 / 1 / 2 / 1 (all dangling else) |
| v2 `language_v2.txt` | 103 / 815 / 194 | 103 / 810 / 194 | 444 / 3026 / 751 | 103 / 810 / 194 | 4 / 1 / 2 / 1 |
| v2 ambiguous expressions | 98 / 823 / 107 | 98 / 818 / 107 | 443 / 3227 / 442 | 98 / 818 / 107 | 4 / 1 / 2 / 1 (+41/41/327/41 resolved by precedence) |
| `slr_vs_lalr.txt` | 10 / 17 / 7 | 10 / 17 / 7 | 14 / 22 / 9 | 10 / 17 / 7 | 1 / 0 / 0 / 0 |
| `lalr_vs_clr.txt` | 13 / 15 / 5 | 13 / 15 / 5 | 14 / 17 / 5 | 14 / 17 / 5 | 2 R/R / 2 R/R / 0 / 0 |
| `epsilon.txt` | 8 / 15 / 5 | 8 / 15 / 5 | 8 / 15 / 5 | 8 / 15 / 5 | 0 |

The v1 numbers are unchanged from the original project (37 / 37 / 111 states,
185 / 185 / 478 entries, conflict states 33, 103, 107).

## 2. SLR vs LALR vs CLR in v2

The v2 grammar (`grammar/language_v2.txt`) lets a statement start with an
l-value (`*p = x ;`) or with an expression (`*p ;`). As designed, SLR gets
shift/reduce conflicts on `=`, `+=` and `-=` in state 21
(`A -> L . = E` against `F -> L .`), while LALR and CLR do not:

```
./parserx --grammar grammar/language_v2.txt --code "x = 1 ;" --explain --trace none
```
```
CONFLICT SLR shift/reduce on '+=' state=21 -> resolved by SHIFT (default: shift preferred)
CONFLICT SLR shift/reduce on '-=' state=21 -> resolved by SHIFT (default: shift preferred)
CONFLICT SLR shift/reduce on '=' state=21 -> resolved by SHIFT (default: shift preferred)
CONFLICT SLR shift/reduce on 'else' state=89 -> resolved by SHIFT (default: shift preferred)
CONFLICT LALR shift/reduce on 'else' state=89 -> resolved by SHIFT (default: shift preferred)
CONFLICT CLR shift/reduce on 'else' state=406 -> resolved by SHIFT (default: shift preferred)
CONFLICT CLR shift/reduce on 'else' state=429 -> resolved by SHIFT (default: shift preferred)
    FOLLOW provenance: '=' in FOLLOW(F) because F appears at the end of U -> F; '=' in FOLLOW(U)
    because U appears at the end of L -> * U; '=' in FOLLOW(L) because '=' can begin what follows
    L in A -> L = E
EXPLAIN SLR state=21 symbol='=' witness="id =" verified=yes cause="FOLLOW(F) <- FOLLOW(U) <- FOLLOW(L) <- FIRST after L in A -> L = E"
```

The only other conflict is the dangling else, present in all parsers. The
`( B )` vs `( E )` ambiguity in conditions and unary `*` vs binary `*`
produced **no** conflict.

**Does the SLR conflict change any parse?** No. Resolved by the default
shift, SLR takes exactly the action LR(1) takes in that state (the reduce
`F -> L` is only correct when the next token is *not* `=`), so SLR accepts
the same language. This was checked on 10 000 inputs (§6): SLR, LALR, CLR
and PGM agree on acceptance for 100 % of them. What SLR does differently is
*error detection*: it performs more reductions before it notices an error
and offers a larger set of expected tokens. Hand-picked examples
(tests 34b–34g):

| Input | reductions before the error SLR / LALR / CLR | expected-set size SLR / LALR / CLR |
|---|---|---|
| `x = a < b ;` | 5 / 5 / 0 | 4 / 4 / 7 |
| `x = ( a + b ;` | 5 / 5 / 0 | 3 / 3 / 7 |
| `x = 1 ; }` | 2 / 2 / 0 | 15 / 15 / 15 |
| `{ x = 1 ;` | 2 / 2 / 0 | 15 / 15 / 15 |
| `x = 1` | 0 / 0 / 0 | 14 / 14 / 6 |

```
./parserx --grammar grammar/language_v2.txt --code "x = 1" --trace none
  CLR context: in an assignment "l-value = expression", after '1': expected ';' or an operator (% * + - /)
  LALR context: in an assignment "l-value = expression", after '1': expected '&&', ')', '+=', '-=', ';', '=', ']', 'relop', '||' or an operator (% * + - /)
```

**Natural LR(1)-but-not-LALR(1) construct.** None was found in v2: LALR has
exactly one unresolved conflict (the dangling else, inherited from CLR states
406 and 429, see §9), and PGM — which splits states only when LALR merging
would create a new conflict — ends with the same 103 states as LALR. The
random grammar study (§11) also found 0 such grammars among 1000. The
LR(1)-not-LALR(1) effect is demonstrated with `lalr_vs_clr.txt` (Dragon book
Example 4.58) instead; it was not forced into v2.

## 3. Ambiguous + precedence vs layered

```
./parserx --grammar grammar/expr_ambiguous.txt --code "a + b * c - d / e" --trace none
./parserx --grammar grammar/expr_layered.txt   --code "a + b * c - d / e" --trace none
```

| Grammar | SLR states | SLR ACTION+GOTO | CLR states | CLR ACTION+GOTO | parse steps | precedence-resolved S/R (SLR / CLR) |
|---|---|---|---|---|---|---|
| ambiguous + `%left/%right` | 17 | 93 | 32 | 160 | 19 | 20 / 40 |
| layered E/T/F | 19 | 107 | 36 | 184 | 23 | – |

Same trees (`[E [E a + [E b * c]] - [E d / e]]` vs
`[E [E a + [T b * c]] - [T d / e]]`). The layered grammar pays 4 extra steps
for unit reductions (`F -> id`, `T -> F`, `E -> T`).

On the whole v2 language (`tests/programs/v2_all_forms.src`, 22 statements):
the ambiguous variant has 98 vs 103 LALR states and needs **417 instead of
582 parse steps (−28 %)**, with the same unresolved conflicts.

## 4. LALR: DeRemer–Pennello vs merge

```
./parserx --grammar grammar/language_v2.txt --code "" --trace none --verify
```
```
DeRemer-Pennello: 194 non-terminal transitions, 0 reads edges, 429 includes edges, 680 lookback edges, 2 non-trivial SCCs
VERIFY LALR dp==merge OK (103 states, 1004 ACTION/GOTO cells compared)
```

`--verify-lalr` passes on **every** loadable grammar in the repository and
the zoo (41 grammars, test 35a and `tests/check_all.sh`), including the
nullable-chain (reads edges) and includes-cycle (SCC) grammars.

Honest build times (`--bench --repeat 10`, median, µs):

| Grammar | LALR-merge (CLR + merge) | LALR-dp (LR(0) + relations) | speed-up |
|---|---|---|---|
| v1 | 1601.5 | 169.6 | 9.4× |
| v2 | 27526.3 | 1219.1 | 22.6× |

## 5. PGM (Pager's minimal LR(1))

`VERIFY PGM` holds on every grammar: `LALR states ≤ PGM ≤ CLR`, and PGM has
no unresolved conflict whenever CLR has none.

| Grammar | LALR | PGM | CLR |
|---|---|---|---|
| v1 | 37 | 37 | 111 |
| v2 | 103 | 103 | 444 |
| `lalr_vs_clr.txt` | 13 | **14** | 14 |
| JSON (zoo) | 26 | 26 | 56 |
| C expressions (zoo) | 68 | 68 | 343 |
| Pascal subset (zoo) | 47 | 47 | 232 |

On `lalr_vs_clr.txt` PGM keeps the two `c .` states apart, has no conflict
and accepts `b c d` like CLR (test 36a).

## 6. Differential fuzzing

```
./parserx --grammar grammar/language.txt    --fuzz 2000
./parserx --grammar grammar/language_v2.txt --fuzz 2000
./parserx --grammar grammar/lalr_vs_clr.txt --fuzz 2000
./parserx --grammar grammar/slr_vs_lalr.txt --fuzz 2000
```
Each run: 2000 generated sentences + 8000 one-edit mutants (seed 20261015,
max depth 12). "Delay" = reductions performed on the error token before the
error is detected.

| Grammar | Parser | accepted | mean delay | max delay | mean expected-set size |
|---|---|---|---|---|---|
| v1 | SLR = LALR = PGM | 2349 | 0.229 | 8 | 4.317 |
| v1 | CLR | 2349 | 0.000 | 0 | 3.721 |
| v2 | SLR | 2961 | 0.357 | 10 | 8.873 |
| v2 | LALR = PGM | 2961 | 0.353 | 10 | 8.878 |
| v2 | CLR | 2961 | 0.000 | 0 | 6.597 |
| slr_vs_lalr | SLR = LALR = PGM | 4499 | 0.035 | 10 | 1.989 |
| slr_vs_lalr | CLR | 4499 | 0.000 | 0 | 1.919 |
| lalr_vs_clr | SLR = LALR | 1327 | 0.046 | 1 | 1.524 |
| lalr_vs_clr | CLR = PGM | 2680 | 0.000 | 0 | 1.524 |

```
FUZZ AGREEMENT inputs=10000 agree=10000 disagreements=0 explained=0 lalr_merged=0/0     (v1, v2, slr_vs_lalr)
FUZZ AGREEMENT inputs=10000 agree=8647 disagreements=1353 explained=1353 lalr_merged=1353/1353   (lalr_vs_clr)
FUZZ INVARIANTS OK   (all four runs)
```

Invariants checked automatically on every input (for parsers whose table has
no reduce/reduce conflict and no `%nonassoc` error entry): every generated
sentence is accepted; all such parsers agree with CLR on acceptance; all of
them detect the error at the **same token** (correct-prefix property); CLR
never reduces more often before an error than any other parser. On
`lalr_vs_clr.txt` every one of the 1353 disagreements involved a move through
a conflict-resolved cell of the merged LALR state.

Determinism: the same seed gives a byte-identical CSV, a different seed a
different one (test 38k).

## 7. Precision index

```
./parserx --grammar grammar/language_v2.txt --code "" --precision --trace none
```

Spurious reduce lookaheads compared with CLR (summed over CLR states and
completed items), SLR reduce cells absent from LALR, and the mean number of
extra filled ACTION cells per CLR state:

| Grammar | SLR | LALR | PGM | CLR | SLR cells not in LALR | inflation SLR / LALR / PGM |
|---|---|---|---|---|---|---|
| v1 | 82 | 82 | 82 | 0 | 0 | 0.721 / 0.721 / 0.721 |
| v2 | 972 | 964 | 964 | 0 | 8 | 2.178 / 2.167 / 2.167 |
| slr_vs_lalr | 4 | 3 | 3 | 0 | 1 | 0.214 / 0.214 / 0.214 |
| lalr_vs_clr | 4 | 4 | **0** | 0 | 0 | 0 / 0 / 0 |
| C expressions | 692 | 692 | 692 | 0 | 0 | 2.017 / 2.017 / 2.017 |
| Pascal subset | 551 | 547 | 547 | 0 | 2 | 2.371 / 2.353 / 2.353 |

The 8 SLR-only reduce cells of v2 are the 5 extra filled cells (815 vs 810)
plus the 3 reduce entries on `=`, `+=`, `-=` removed by the shift resolution.

## 8. Table compression

Default reductions + identical-row sharing + comb-vector packing, 16-bit cells:

| Grammar | SLR / LALR / PGM raw → packed bytes | CLR raw → packed bytes |
|---|---|---|
| v1 | 1702 → 482 (0.283) | 5106 → 1410 (0.276) |
| v2 | 9270 → 1674 (0.181) | 39960 → 7692 (0.192) |
| v2 ambiguous | 7840 → 1776 (0.227) | 35440 → 7334 (0.207) |
| lalr_vs_clr | 234 → 154 (0.658) | 252 → 178 (0.706) |

`VERIFY COMPRESS` checks every (state, symbol) pair: a filled raw cell must
decode to the same action, an empty one to an error or the row's default
reduction. Test 43f runs every `.src` test case with and without
`--compressed`: acceptance, error token, line and column are identical in
all 217 cases.

Effect of default reductions on error detection (`--fuzz 2000 --compressed`):

| Grammar | raw tables: mean / max delay (CLR) | compressed: mean / max delay (every parser) |
|---|---|---|
| v1 | 0.000 / 0 | 1.434 / 13 |
| v2 | 0.000 / 0 | 2.209 / 17 |

With default reductions **all four parsers behave identically** on every
input: the reduce decision no longer looks at the lookahead, so CLR's
precision advantage disappears entirely (its delay rises from 0 to the same
value as SLR's). Acceptance and the error token stay the same (invariants OK).

**Bug found by the tests and fixed:** a `%nonassoc` error entry is an empty
cell, which a default reduction would fill, so `a < b < c` became acceptable.
Such cells are now stored as explicit error entries (test 43g).

## 9. Conflict explanations and witnesses

Every unresolved conflict of every grammar in the repository gets a shortest
witness that is **verified by running the parser** into the conflict state
(test 39g, `tests/check_all.sh`). Examples:

```
EXPLAIN SLR state=4 symbol='=' witness="id =" verified=yes cause="FOLLOW(R) <- FOLLOW(L) <- FIRST after L in S -> L = R"     (slr_vs_lalr)
EXPLAIN LALR state=4 symbol='d' witness="a c d" verified=yes cause="created by merging CLR states { 4, 7 }, whose lookaheads collide"   (lalr_vs_clr)
      CLR 4:  reduce A -> c on { *d* }  reduce B -> c on { e }
      CLR 7:  reduce A -> c on { e }  reduce B -> c on { *d* }
EXPLAIN LALR state=33 symbol='else' witness="if ( id relop id ) id = id ; else" verified=yes cause="inherited from CLR state(s) { 103, 107 }, a conflict before merging"   (v1)
```

Precedence-resolved conflicts are also witnessed (counts only):

```
./parserx --grammar grammar/language_v2_ambiguous.txt --code "" --explain --trace none
EXPLAIN SLR precedence_cells=41 verified=41
EXPLAIN LALR precedence_cells=41 verified=41
EXPLAIN CLR precedence_cells=327 verified=302
EXPLAIN PGM precedence_cells=41 verified=41
```

**Finding:** 25 of the 327 precedence-resolved CLR cells of the ambiguous v2
grammar get no verified witness, even with 4× the search limits. The
resolved CLR parser never enters those states with that lookahead on any
candidate: precedence decisions taken earlier in the parse make them
unreachable. This was not proved formally; it is reported as observed.

## 10. Classifier and zoo

`--classify` judges raw conflicts before any precedence or default
resolution. Each zoo grammar states its expected class and source in its
header; test 41a asserts all 15.

| Zoo grammar | class | states SLR / LALR / CLR / PGM | raw conflicts LR(0) / SLR / LALR / CLR |
|---|---|---|---|
| dragon_cc (ALSU 4.55) | LR(0) | 7 / 7 / 10 / 7 | 0 / 0 / 0 / 0 |
| nested_lr0 (aⁿ c bⁿ) | LR(0) | 6 / 6 / 10 / 6 | 0 / 0 / 0 / 0 |
| json | LR(0) | 26 / 26 / 56 / 26 | 0 / 0 / 0 / 0 |
| dragon_expr (ALSU 4.1) | SLR(1) | 12 / 12 / 22 / 12 | 2 / 0 / 0 / 0 |
| epsilon | SLR(1) | 8 / 8 / 8 / 8 | 4 / 0 / 0 / 0 |
| c_expr | SLR(1) | 68 / 68 / 343 / 68 | 31 / 0 / 0 / 0 |
| dragon_lvalue (ALSU 4.49) | LALR(1) | 10 / 10 / 14 / 10 | 1 / 1 / 0 / 0 |
| slr_vs_lalr | LALR(1) | 10 / 10 / 14 / 10 | 1 / 1 / 0 / 0 |
| dragon_lr1_not_lalr (ALSU 4.58) | LR(1) | 13 / 13 / 14 / 14 | 6 / 2 / 2 / 0 |
| lalr_vs_clr | LR(1) | 13 / 13 / 14 / 14 | 6 / 2 / 2 / 0 |
| ambiguous_expr | not LR(1) | 10 / 10 / 18 / 10 | 4 / 4 / 4 / 8 |
| palindromes (unambiguous) | not LR(1) | 8 / 8 / 20 / 8 | 4 / 4 / 4 / 4 |
| pascal_subset | not LR(1) | 47 / 47 / 232 / 47 | 32 / 1 / 1 / 1 |
| v1_language | not LR(1) | 37 / 37 / 111 / 37 | 12 / 1 / 1 / 2 |
| v2_language | not LR(1) | 103 / 103 / 444 / 103 | 42 / 4 / 1 / 2 |

The C expression subset is SLR(1) even though it has the `unary = assign`
shape: the reduce item competing with the assignment operator is
`mul -> unary .`, and `=` is not in FOLLOW(mul). (An earlier draft of its
header said "LALR(1)"; the classifier and the FOLLOW sets showed that was
wrong, and the header was corrected.)

## 11. Random grammar study

```
./parserx --random-grammars 1000 --seed 20261015 --csv random.csv
```
Parameters: 2–4 non-terminals, 2–4 terminals, 1–3 alternatives per
non-terminal, right sides of 0–3 symbols (ε for 0), each symbol a
non-terminal with probability 1/3; grammars with an unproductive or
unreachable non-terminal or a duplicate production are redrawn (5544 draws
discarded).

| Class | Count | Fraction |
|---|---|---|
| LR(0) | 98 | 0.098 |
| SLR(1) | 203 | 0.203 |
| LALR(1) (not SLR) | 10 | 0.010 |
| LR(1) (not LALR) | 0 | 0.000 |
| not LR(1) | 689 | 0.689 |

Mean CLR/LALR state ratio 1.368; mean PGM/LALR state ratio 1.000; mean
ACTION+GOTO entries SLR 24.883, LALR 24.253, CLR 33.476, PGM 24.253. Small
random grammars are mostly ambiguous; LR(1)-not-LALR(1) grammars are rare
enough that none appeared.

## 12. GLR parse counts

`--glr` runs a Tomita-style GLR parser on the **unresolved** LALR table and
counts the trees of the shared packed parse forest:

| Grammar / input | parses |
|---|---|
| v1, test 07 dangling else | **2** (ambiguous S over tokens 0..20) |
| v1, `x = a + b * c ;` | 1 |
| `E -> E + E \| E * E`, `a + b * c` | 2 |
| same, `a + b + c + d` | 5 (Catalan 3) |
| `expr_ambiguous`, `a + b * c - d / e` (precedence ignored) | 14 (Catalan 4) |
| palindromes, `a b b b a` | 1 (CLR rejects it: not LR(1)) |
| `lalr_vs_clr`, `b c d` | 1 (LALR rejects it) |
| 40 operands joined by `+` | ≥ 10¹⁸ (saturated) |

## 13. Error recovery

```
printf 'x = ;\ny = 2 ;\nz = a + * b ;\nw = 3 ;\n{ q = ( 1 ; }\n' > multi.src
./parserx --input multi.src --recover --parser clr
```
```
Recovery (panic mode): 3 error(s) reported, 5 token(s) skipped
  Error 1: line 1, column 5: unexpected ';' in state 7; expected: ( id num
  Error 2: line 3, column 9: unexpected '*' in state 40; expected: ( id num
  Error 3: line 5, column 11: unexpected ';' in state 35; expected: ) * + - /
RECOVERY CLR errors=3 skipped=5 first=1:5
```
The first recovered error equals the non-recovering result for all four
parsers. Yacc-style `error` productions are also supported (test 37g).

## 14. Benchmarks

```
./parserx --code "x = a + b * c ; if ( a < b ) y = 1 ; else y = 2 ;" --bench --repeat 10
./parserx --grammar grammar/language_v2.txt --input tests/programs/v2_all_forms.src --bench --repeat 10
```

Build (median µs): v1 SLR 114.4, LALR-merge 1601.5, LALR-dp 169.6, CLR
1521.6, PGM 863.4; v2 SLR 872.0, LALR-merge 27526.3, LALR-dp 1219.1, CLR
27148.9, PGM 9660.2.

Parsing speed is the same for every table (one lookup per move): about
10⁷ tokens/s. Scaling (v1 input repeated, one timed parse each):

| tokens | lex µs | SLR µs | LALR µs | CLR µs | PGM µs |
|---|---|---|---|---|---|
| 990 | 84.2 | 78.1 | 77.9 | 79.8 | 82.5 |
| 9 983 | 780.7 | 771.2 | 757.5 | 713.1 | 713.3 |
| 99 982 | 7 042.4 | 7 643.3 | 7 584.5 | 7 469.6 | 7 284.7 |
| 999 995 | 67 155.1 | 76 245.0 | 76 811.5 | 76 574.5 | 77 256.0 |

Linear in the input size. The 1 MB v2 test (524 296 tokens) is lexed, parsed
four times and semantically checked in about 2 s end to end.

## 15. Where reality differed from the brief

- **The v2 SLR conflict never changes a parse.** The shift chosen by default
  is the LR(1)-correct action; 10 000 fuzzed inputs agree 100 %. The
  difference is only in error detection (§2, §6).
- **No natural LR(1)-not-LALR(1) construct in v2** (PGM = LALR = 103 states);
  demonstrated with `lalr_vs_clr.txt` instead.
- **Appending `sr_prec=` to every `TABLE` line would break existing tests**
  that anchor `rr=0$`; the precedence fields are therefore appended only for
  grammars that declare precedence.
- **Witnesses for precedence-resolved CLR cells:** 302 of 327 verified on the
  ambiguous v2 grammar; the rest appear unreachable after resolution (§9).
- **Compression + `%nonassoc`** needed explicit error entries (§8).
- **`&` of a literal** cannot reach semantic analysis: `U -> & L` makes it a
  syntax error.
- The **recovery loop guard** never triggers in the test-suite: panic mode
  always consumes a token and suppressed errors in error-production mode
  discard one, so recovery is guaranteed to progress; the guard stays as a
  safety net and the end-of-input cases stop with a message (tests 37c, 37e,
  47h).
- **ASan/UBSan** cannot run under MinGW; `make sanitize` exists for Linux but
  was not run here.
