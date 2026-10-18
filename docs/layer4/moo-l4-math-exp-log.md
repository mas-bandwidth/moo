# Layer 4 exp/log family

## Decision

Use `fixExp2` and `fixLog2` as the only math sources for these seven entry points, and keep their library and bytecode fastcall paths on the same helpers. The two offline Q48.16 constants are nearest-integer literals: `log2(e) = round((1 / ln(2)) * 65536) = 94548`, from `94548.4621996991...`; `1 / log2(10) = round((ln(2) / ln(10)) * 65536) = 19728`, from `19728.3017958347...`.

`exp(x)` scales with one `fixMul(x, log2(e))` (half-up) before `fixExp2` (nearest, half-up on dropped bits, saturating). `sinh` and `cosh` form `exp(+-x)/2` by subtracting one from the scaled binary exponent, then use exact saturating subtraction/addition. `tanh` uses the stable form `sign(x) * (1 - exp(-2|x|)) / (1 + exp(-2|x|))`; its fixed multiply rounds half-up, the additions and subtraction are exact with saturation, and the final `fixDiv` truncates toward zero. Halving before the `sinh`/`cosh` sum keeps representable results available until the final saturation.

`log(x)` divides `fixLog2(x)` by the rounded `log2(e)` literal with `fixDiv` (toward zero). `log10(x)` multiplies `fixLog2(x)` by the rounded reciprocal of `log2(10)` with `fixMul` (half-up). `log(x, base)` uses `fixLog2(x) / fixLog2(base)` with `fixDiv` (toward zero), using the same direct `log2` and scaled `log10` paths for bases 2 and 10. Any non-positive value argument, or non-positive base, raises `math.log of a non-positive number`; base 1 raises `division by zero` because its fixed logarithm is zero.

## Changes

- `VM/src/lmathlib.cpp`: added the shared fixed helpers `moo_math_exp`, `moo_math_sinh`, `moo_math_cosh`, `moo_math_tanh`, `moo_math_log10`, and `moo_math_log`; the six library wrappers use them and validate the log domains before calling `fixLog2`.
- `VM/src/lbuiltins.cpp`: the seven bytecode fastcalls call those helpers. Invalid log domains return to the library wrapper so both paths raise the same error.
- `tests/moo/moo-l4-math-exp-log.test.cpp`: checks each function at edges and 4096 seeded inputs, verifies library and fastcall results against the fixed composition, checks log errors, and prints the maximum absolute error against each available oracle vector.
- `tests/conformance/moo/l3/moo-l4-math-exp-log.luau`: pins saturation, identities, alias/fastcall agreement, and the named log errors.
- `tests/conformance/moo/math.luau` (in PATHS from pass 7): four upstream lines that assumed the libm bridge now pin the fixed results, each beside a `moo:` comment that keeps the upstream line: `math.exp(1)` is `178144/65536`, one raw below `math.e` (`round(e * 2^16) = 178145`) because `log2(e)` is rounded to Q48.16 before `fixExp2`; `math.log10("10")` and `math.log("10", 10)` are `65535/65536`, one raw below 1, through `fixLog2(10) = 217706` times the rounded reciprocal `19728` (`217706 * 19728 / 2^16 = 65535.03`, half-up to 65535); and `math.log("0")` is the named error `math.log of a non-positive number`, now asserted with `pcall`. The `eq`-tolerance lines (`math.log10(2)`, `math.log(9, 3)`, `math.log(100, 10)`, `math.tanh(3.5)`) pass unchanged.

The current `moo-l4-math-oracle` vectors produced these maximum absolute errors in raw Q48.16 units for this composition:

| Function | Samples | Maximum error |
|---|---:|---:|
| `exp` | 47,337 | 1,503,472,452,145,152 |
| `log` | 47,156 | 11 |
| `log10` | 47,115 | 15 |
| `sinh` | 47,349 | 1,498,395,532,686,336 |
| `cosh` | 47,327 | 1,174,843,021,516,800 |
| `tanh` | 47,405 | 2 |

These are measurements, not accepted error bounds. The largest absolute errors occur near the upper representable range, where a small relative error becomes many raw units. The layer-closing conformance card must decide and document any bounds before locking the layer.

## Required document updates

`docs/DESIGN.md`, "Layer 3 as built", currently says:

> - the libm functions of `math`, `^`, `math.noise`, and the float `vector` type convert through doubles (`moo_numbridgetodouble`, `moo_numbridgefromdouble`), until the math library is rebuilt on fixed and the vector type is removed;

This sentence must be narrowed to the bridge families that remain after the other layer 4 math cards, and must no longer describe this card's seven functions as double bridges.

`docs/DESIGN.md`, "math library", currently says:

> | `exp2`-based: `exp`, `sinh`, `cosh`, `tanh` | `fixExp2` with exact scaling by log2(e) |
> | `log2`-based: `log`, `log10`, `log(x, base)` | `fixLog2`; `x <= 0` is a named error |

Keep these source rows and add the rounding compositions above, the base-1 error, and the measured error table with any accepted bounds the layer-closing card decides.

`docs/DETERMINISM.md`, row 7 currently says:

> | 7 | platform libm | **partly**: `+ - * / // %` are fixed's integer arithmetic, and the compiler folds with the same functions; the libm functions and `^` still cross a double bridge until the math library is rebuilt on fixed |

Replace the status with the remaining platform-libm paths after the other layer 4 cards land; this card removes the `exp`, `sinh`, `cosh`, `tanh`, `log`, `log10`, and `log(x, base)` bridges and shares their fixed calculations with fastcalls.

## Gate status (pass 7)

`tools/revendor-fixed.sh --check` and `tools/moo-check-no-clock.sh build/libLuau.VM.a` pass, and the seven functions carry no `moo_numbridge` line. `Moo.Test` runs the four exp/log cases green (`MathExpLogLibraryFastcallAndComposition`, `MathExpLogNamedDomainErrors`, `MathExpLogOracleMaximumErrors`, `MathExpLogConformanceScript`). The `Moo.Test` and `Luau.UnitTest` failures that remain are identical on the unchanged base and belong to other cards. `Luau.Conformance` `Math` passes with the `math.luau` changes above.

## Finding for the layer-closing card

`math.log(10, 10)` is not 1 and `math.exp(1)` is not `math.e`: both are the one-raw cost of rounding the scaling constants to Q48.16. Dividing by `fixLog2(10)` (or a rounded `log2(10) = 217706`) instead of multiplying by the rounded reciprocal would make `log10(10)` and `log(10, 10)` exactly 1, and a wider `log2(e)` constant is expected (not measured) to make `exp(1) == math.e`; both change the card's stated compositions, so they are left for the layer-closing card to decide.
