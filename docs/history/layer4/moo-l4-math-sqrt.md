# moo-l4-math-sqrt: `math.sqrt` on `fixSqrt`

## What changed

- `VM/src/lmathlib.cpp`: `math_sqrt` now calls `fixSqrt` (fixed's exact integer square root,
  rounded toward zero) instead of libm. A negative input is the named error
  `math.sqrt of a negative number`.
- `VM/src/lbuiltins.cpp`: the `sqrt` fastcall (`luauF_sqrt`) does the same; a negative input
  returns -1 so the regular call path raises the named error.
- `tests/moo/moo-l4-math-sqrt.test.cpp` and `tests/conformance/moo/l3/moo-l4-math-sqrt.luau`:
  the exhaustive (raw 0..2^24) and 4096 seeded-large-input tests, each result r checked with
  128-bit integer arithmetic against `r*r <= a*2^16 < (r+1)*(r+1)`, never by squaring in double.
- `tests/conformance/moo/math.luau:432`: moved-upstream line `assert(math.sqrt(2) == math.sqrt2)`
  is replaced by `assert(math.sqrt(2) == 92681/65536)` because `fixSqrt` rounds toward zero, so
  `sqrt(2)` is one raw below the nearest `sqrt2` constant. Kept the original as a `-- moo:` comment.

## The seed question (Glenn decides)

DESIGN.md "math library" says `fixSqrt` seeds with the IEEE double square root and repairs it, and
that layer 4 either keeps that with the argument written down or switches to `fixISqrt128High`.
This card does not decide: it keeps `fixSqrt` as it is at the pin and measures both options.

Both functions at the pin are the same integer square root (`fixISqrt128High` is the helper
`fixSqrt` calls after forming the 128-bit input), so they produce identical bits on every input by
construction. Measured on this bench (Linux, clang 18, -O2, 2^20 seeded inputs, median of 3 runs):

| function | ns/call | inputs 2^20+2 | mismatches |
|---|---|---|---|
| `fixSqrt` | 7.30 | full non-negative raw range | 0 |
| `fixISqrt128High` | 7.48 | same | 0 |

The two are the same cost within noise (7.30 vs 7.48 ns); there is no measurable reason to prefer
one over the other. The only argument left is conceptual: `fixSqrt` uses a double seed (a double
inside the VM, albeit correct and deterministic), while `fixISqrt128High` is integer-only. If the
no-double-inside-the-VM rule is read strictly, switching `fixSqrt`'s seed to an integer path today
the `hi != 0` 128-bit branch, or a pure integer seed) removes the double; if not, keeping
`fixSqrt` is simpler and bit-identical.

## DESIGN.md and DETERMINISM.md sentences that must change when moo-l4-conformance folds this

- `docs/DESIGN.md`, "Layer 3 as built", TEMPORARY bridges: "the libm functions of `math`, `^`,
  `math.noise`, and the float `vector` type convert through doubles (`moo_numbridgetodouble`,
  `moo_numbridgefromdouble`), until the math library is rebuilt on fixed" — `sqrt` no longer
  converts through doubles; the list must drop it (the `^`, noise, and vector bridges remain).
- `docs/DESIGN.md`, "math library", table row `sqrt` is already "`fixSqrt` (exact, rounded toward
  zero; negative is a named error)" — the build now matches it; the following paragraph about the
  `fixSqrt` seed ("Layer 4 either keeps it with that argument written down, or switches to
  `fixed`'s integer-only path (`fixISqrt128High`); Glenn decides") can record this card's measurement
  and Glenn's decision.
- `docs/DETERMINISM.md`, item 7 ("platform libm"), removes `sqrt` from the enumerated libm
  functions (`VM/src/lmathlib.cpp` and the fastcall in `VM/src/lbuiltins.cpp`); the "partly"
  status line stays until the rest of the math library is on fixed.
