# moo-l5-fuzz-differential: moo arithmetic and formatting against the oracle, fuzzed

Layer 5 of [DESIGN.md](../DESIGN.md) ("Layers") asks for "bit-exact agreement with `fixed`'s C; fuzzing". The layer 3
oracle ([moo-l3-oracle-harness](../layer3/moo-l3-oracle-harness.md)) already checks every operator over edges and a
seeded random stream, but only over a finite set chosen in C++. This card adds libFuzzer targets: the fuzzer chooses
the operands and the operator, and the target aborts the moment moo and the oracle differ, so the search is no longer
limited to the harness's own pairs. It also adapts the two upstream Luau fuzz targets that still test doubles
(`fuzz/number.cpp`, `fuzz/format.cpp`) to moo's fixed number.

## Decision

No change to the language. The only decision is where the fuzz targets' shared checks live and how they are built:

- The differential core sits in the new target, `fuzz/moo_arith.cpp`, and reuses the layer 3 oracle's C reference
  unchanged (`tests/moo/moo_oracle_fixed.c`, compiled as C into the target). It does not duplicate the oracle.
- The moo fuzz targets are added to `fuzz/CMakeLists.txt` before the upstream macOS/ARM64 guard, because they use no
  protobuf and must build with clang on the Linux bench. The upstream protobuf targets are untouched and still do not
  build off x86_64 macOS.
- Fuzzing is a functional activity and none of it runs in `Moo.Test`. What `Moo.Test` gets is the same comparison,
  round trip and a negative control on a fixed input set, so a broken target is caught by the push gate without a
  fuzzing run.

## What changed

- `fuzz/moo_arith.cpp` (new): `LLVMFuzzerTestOneInput` reads an operator from byte 0, two operands from the rest
  (`mooFuzzOperand`, clamped into `[MOO_NUMBER_MIN, MOO_NUMBER_MAX]` by `mooFuzzClamp`), and computes the operation
  twice: through `moo_number.h` and through the oracle (`moo_oracle_*`). `mooFuzzMismatch` prints both values and
  aborts on any difference. `/`, `//` and `%` with a zero divisor are skipped, because the VM raises
  `division by zero` before `moo_number.h` is called (DESIGN.md "Division by zero"). The same input is also fed to
  `moo_numparse` as raw bytes and as a decimal-shaped string; every value that parses must survive
  `moo_numtostr` -> `moo_numparse` bit for bit (`mooFuzzRoundTrip`, `mooFuzzParse`; DESIGN.md "Number formatting").
- `fuzz/number.cpp` (`LLVMFuzzerTestOneInput`, `moo:` marked): the upstream double test used `luai_num2str` and
  `strtod`. It now reads 8 bytes as a raw `fixed_t`, clamps it, formats with `moo_numtostr` and parses back with
  `moo_numparse`, aborting unless the raw value is unchanged.
- `fuzz/format.cpp` (`LLVMFuzzerTestOneInput`, `moo:` marked): the upstream target called the analyzer's double
  format-string linter. It now builds C flags, width, precision and a conversion from the input, calls
  `moo_numformat`, checks the returned length and NUL, checks that one character less than that length is refused,
  and checks that `%.16f` is the exact expansion that `moo_numparse` reads back to the same raw value.
- `fuzz/CMakeLists.txt`: adds `Moo.Fuzz.Arith`, `Moo.Fuzz.Number` and `Moo.Fuzz.Format`, built with
  `-fsanitize=fuzzer,address,undefined`, including `Common/include`, `extern` and `extern/fixed/include`, defining
  `FIX_SATURATE`, and linking `Moo.Fixed`. `Moo.Fuzz.Arith` compiles `tests/moo/moo_oracle_fixed.c` as C.
- `tests/moo/moo-l5-fuzz-differential.test.cpp` (new): suite `moo-l5-fuzz-differential`.
  - `MooFuzzDifferentialOperators`: all 13 operators, edges crossed with themselves plus 20,000 seeded PCG32 pairs,
    `moo_number.h` against the oracle.
  - `MooFuzzDifferentialParseRoundtrip`: `tostring` and `%.16f` both parse back to the same raw value over 20,000
    seeded values and the edges.
  - `MooFuzzDifferentialNegativeControl`: a planted one-raw-unit error in `*` must differ from the oracle on every
    pair, so the comparison the target aborts on is not vacuous.
  - `MooFuzzDifferentialScript`: runs the new conformance script and expects `OK`.
- `tests/conformance/moo/l3/moo-l5-fuzz-differential.luau` (new): pins from inside the VM the same round trips,
  `fixMul`'s half-up tie, saturating add/sub at both domain edges, the sign of `//` and `%`, and the named
  `division by zero` error. It returns `OK`; `MooL3Scripts` runs it with the other layer scripts.
- No corpus is committed: the targets start from an empty corpus and the fuzzer's own mutations; the card allows at
  most 20 seed files and none were needed.

## Sentence changes owed to the closing card (moo-l5-conformance)

`docs/DESIGN.md`, "Layers", item 5, currently ends:

> including adversarial programs (object-keyed tables, heavy GC with
> destructors and weak tables, coroutines); bit-exact agreement with `fixed`'s C; fuzzing; and
> the coroutine TLA+ model.

It should name the targets:

> including adversarial programs (object-keyed tables, heavy GC with
> destructors and weak tables, coroutines); bit-exact agreement with `fixed`'s C; libFuzzer
> targets for the operators and the number formatter and parser (`fuzz/moo_arith.cpp`,
> `fuzz/number.cpp`, `fuzz/format.cpp`); and the coroutine TLA+ model.

`docs/DESIGN.md`, "Layer 3 as built", the bullet "**Formatting and tonumber are exact**", currently ends:

> `tonumber` and string coercion use the literal parser, with spaces
> and one sign around it; out of range is not a number.

It should record that the rule is also fuzzed:

> `tonumber` and string coercion use the literal parser, with spaces
> and one sign around it; out of range is not a number. A libFuzzer target feeds arbitrary strings to
> the parser and arbitrary raw values through the formatter and requires `parse(format(x)) == x`
> (`fuzz/moo_arith.cpp`, `fuzz/number.cpp`, `fuzz/format.cpp`).

`docs/DETERMINISM.md`, "The guarantee", currently says:

> The conformance layer (layer 5 in [DESIGN.md](DESIGN.md)) checks the guarantee: every program
> in the determinism suite runs on each platform and compiler, and the full VM state hash and
> the effect log must match bit for bit.

It should add:

> The conformance layer (layer 5 in [DESIGN.md](DESIGN.md)) checks the guarantee: every program
> in the determinism suite runs on each platform and compiler, and the full VM state hash and
> the effect log must match bit for bit. Its libFuzzer targets compare every number operator with
> `fixed`'s C and require `parse(format(x)) == x` for arbitrary input.

## Not done

- The two upstream protobuf fuzz targets are left as they were; they are not moo's number paths.
- The moo fuzz targets are not run by CI or `Moo.Test`; the 60-second runs on the bench are recorded in the pull
  request body, not in the test suite.

## 60-second runs on the Linux bench

Each target was built with clang 21 and `-fsanitize=fuzzer,address,undefined` and run for 60 seconds with
`-max_total_time=60 -print_final_stats=1`; each exited 0 with no crash. The final stats lines:

`build/fuzz/Moo.Fuzz.Arith`:

    #2954810	DONE   cov: 306 ft: 1150 corp: 319/5749b lim: 4096 exec/s: 48439 rss: 466Mb
    Done 2954810 runs in 61 second(s)

`build/fuzz/Moo.Fuzz.Number`:

    #8008000	DONE   cov: 150 ft: 543 corp: 64/505b lim: 4096 exec/s: 131278 rss: 516Mb
    Done 8008000 runs in 61 second(s)

`build/fuzz/Moo.Fuzz.Format`:

    #4433078	DONE   cov: 373 ft: 1045 corp: 145/1674b lim: 4096 exec/s: 72673 rss: 480Mb
    Done 4433078 runs in 61 second(s)

The corpora stayed in `$TMPDIR`; no seed files are committed.
