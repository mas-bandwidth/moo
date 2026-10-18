# moo-math-oracle

The offline generator of `tests/moo/vectors/math-oracle.bin`, layer 4's accuracy oracle (docs/DESIGN.md, "math
library", "Accuracy oracle"). For each math function it writes sampled inputs and the **ideal** Q48.16 answer:
[CORE-MATH](https://core-math.gitlabpages.inria.fr/)'s correctly rounded double result, converted to the nearest
Q48.16 by moo's boundary rule, ties to even. `tests/moo/moo-l4-math-oracle.test.cpp` reads the file and prints each
function's error table.

## Why the file is frozen

moo's tests never call CORE-MATH or libm: CORE-MATH is not a dependency of moo, and libm differs between platforms
(docs/DETERMINISM.md, 7). So the ideal answers are computed once, here, and committed. The file changes only when the
sample definition below or the CORE-MATH pin changes, and then it is regenerated with the command below and
committed in the same PR, with the reason. A file the command does not rebuild bit for bit is wrong.

## Regenerate

On Linux x86-64, with cmake, ninja, curl and clang or gcc (both give the same bytes). Run from the root of the moo
checkout:

```sh
C=dae5dd8c0d5bd2691b45de28c9edbe12f067c3d3
for p in acos/acos.c asin/asin.c atan2/atan2.c atan2/tint.h atan/atan.c cos/cos.c cosh/cosh.c exp/exp.c \
         log10/dint.h log10/log10.c log2/log2.c log/dint.h log/log.c pow/dint.h pow/pow.c pow/pow.h pow/qint.h \
         sinh/sinh.c sin/sin.c tanh/tanh.c tan/tan.c; do
    mkdir -p ../core-math/src/binary64/$(dirname $p)
    curl -sfL -o ../core-math/src/binary64/$p https://gitlab.inria.fr/core-math/core-math/-/raw/$C/src/binary64/$p
done
cmake -S tools/moo-math-oracle -B build-oracle -G Ninja -DCORE_MATH_DIR=$PWD/../core-math -DCMAKE_C_COMPILER=clang
cmake --build build-oracle
build-oracle/moo-math-oracle tests/moo/vectors/math-oracle.bin
git diff --stat tests/moo/vectors/math-oracle.bin   # empty: the file is rebuilt bit for bit
```

A clone of https://gitlab.inria.fr/core-math/core-math checked out at the pin works as `CORE_MATH_DIR` too. The loop
fetches only the 21 files the generator compiles, because the whole repository is large and its server is slow to
clone from. The configure step checks the SHA-256 of each of those files against `MOO_CORE_MATH_FILES` in
`CMakeLists.txt`, and refuses a tree from any other commit. The generator prints each function's record count and
payload bytes, then the file's size and hash.

## The ideal

`ideal = moo_numfromdouble(cr_f(x))`, where `x` is the input raw value divided by 2^16 (exact: see the samples) and
`cr_f` is CORE-MATH's `cr_sin`, `cr_cos`, `cr_tan`, `cr_asin`, `cr_acos`, `cr_atan`, `cr_atan2`, `cr_exp`, `cr_log`,
`cr_log10`, `cr_log2`, `cr_sinh`, `cr_cosh`, `cr_tanh` or `cr_pow`. CORE-MATH has no `sqrt`, because IEEE 754
requires `sqrt` itself to be correctly rounded; the generator uses the C library's `sqrt`, built with `-fno-builtin`
and `-fno-fast-math`.

- The conversion is `mooOracleFromDouble` in `gen.c`, a copy of `moo_numfromdouble` (Common/include/moo_number.h):
  the IEEE bits are decoded in integer arithmetic and rounded once, ties to even.
- A result beyond the range saturates to `INT64_MAX` or `-INT64_MAX` raw (docs/DESIGN.md, "Overflow").
- A NaN result stops the generator: it means a sample left the function's domain.
- Below 2^37 the double has more fraction bits than Q48.16, so the ideal is the correctly rounded double rounded
  again. It can differ from rounding the real value once only when the double falls exactly on a half-unit tie.

## The samples

Every input has at most 53 significant bits (`mooOracleExact`), so the double that holds `raw / 2^16` is exact.
Inputs at and above 2^53 raw are therefore taken on the grid a double holds. The domain limit `INT64_MAX` raw becomes
`2^63 - 2^10` raw.

- **asin, acos: exhaustive.** Every raw value in [-1, 1], 131073 inputs each.
- **Every other function: a seeded dense sample.** There are 65536 PCG32 draws (seed `0x6d6f6f2d6d617468`, stream =
  the function's index in `gen.c`'s table) plus the edges. The records are sorted and duplicates removed, so a block
  holds somewhat fewer records than draws. Each draw is made one of four ways, with equal odds:
  - uniform in the function's dense core (below);
  - uniform anywhere in its domain;
  - any bit width from 1 to 63, with either sign where the domain has negatives;
  - an edge moved by up to 8 raw units.

  The draw is then clamped into the domain and put on the exact grid.
- **Edges**, for each input:
  - 0, ±1 and ±2 raw, and ±1;
  - the domain limits, and one raw unit inside each;
  - the ends of the dense core.

  For sin, cos, tan and atan they also include the nearest raw value of k·π/2, and the raw values on each side of
  it, for |k| <= 16 and for k = ±2^j with 5 <= j <= 40 (`mooHalfPiMultiple`, from π/2 · 2^16 with 64 fraction bits). For the
  two-input functions, the first 13 edges of each input are crossed.

| function | domain (raw) | dense core |
|---|---|---|
| sin, cos, tan, atan | [-INT64_MAX, INT64_MAX] | [-64, 64] |
| asin, acos | [-1, 1], exhaustive | |
| atan2 (y, x) | both full | both [-64, 64] |
| exp | full | [-12, 33] |
| log, log10, log2 | [1 raw, INT64_MAX] | [1 raw, 64] |
| sinh, cosh | full | [-33, 33] |
| tanh | full | [-12, 12] |
| sqrt | [0, INT64_MAX] | [0, 64] |
| pow (base, exponent) | full; base 0 only with exponent >= 0; a negative base only with an integer exponent | base [-16, 16], exponent [-8, 8] |

The domains leave out only the inputs where the real function has no value; moo raises a named error there
(docs/DESIGN.md, "math library" and "pow and ^").

## Size

The brief's sample was 2^20 draws per function plus an exhaustive pass wherever the domain is at most 2^24 raw
values. That would not fit the 8 MB limit: the records take 6 to 14 bytes each, so 2^20 records for each of 14
functions comes to about 120 MB. The generator therefore samples less, in two ways:

- it takes 65536 draws per function (`MOO_ORACLE_SAMPLES` in `CMakeLists.txt`);
- it runs the exhaustive pass only for asin and acos, the two functions whose whole domain is small.

exp, sinh, cosh and tanh have a live range (where the result neither saturates nor rounds to 0) of under 2^24 raw
values, but each would take 10 to 30 MB exhaustively. They are sampled densely in that range instead: a quarter of
the draws fall in the core. The file is about 5.8 MB.

## Format

All integers are little-endian.

- `MOOMATH1` (8 bytes).
- u32 version (1).
- u32 length and the header text. The text names the CORE-MATH commit and the sample definition.
- u32 number of functions.
- Each function, in the table's order:
  - u8 name length, then the name;
  - u8 arity (1 or 2);
  - u8 exhaustive (0 or 1);
  - u64 record count;
  - u64 payload bytes;
  - the payload.
- A u64 FNV-1a 64 hash of every byte before it.

The payload holds the records sorted by input (first input, then second), each one a sequence of zigzag LEB128
varints:

- the first input minus the previous record's first input;
- the second input as it is (arity 2 only);
- the ideal minus the previous record's ideal.

Differences are taken modulo 2^64, and the first record's are taken from 0.
