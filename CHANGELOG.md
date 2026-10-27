# moo changelog

## Unreleased

### Fixed

- The compiler's loop unroller computes the trip count and each unrolled index with `moo_number.h` on
  the raw Q48.16 value; no double reaches a moo number in the compiler or the bytecode passes. The
  unrolled code is unchanged: the bounds it admits are integers within 32767, where the double path
  was exact. SCCP's unreachable `asNumber` fallback returns raw 0, not `0.0` (MSVC C4244).
- The determinism suite hashes a number as its raw Q48.16 bits, not that integer converted to a
  double (MSVC C4244); the seven state hashes are re-pinned and every effects hash held.

### CI

- Each static library is on a macOS link line once (CMake policy CMP0156; the test targets link
  `Luau.Bytecode` through `Luau.Compiler`), and the macOS job fails on any linker warning
  (`-Wl,-fatal_warnings`).
- A compiler cache outage no longer fails a job: `tools/moo-ci-sccache.sh` builds without sccache
  when its server cannot start (as when the GitHub Actions cache answered HTTP 503), and
  `SCCACHE_IGNORE_SERVER_IO_ERROR=1` covers a server lost mid-build. Compile and test failures
  still fail the job.

## v1.0.2 — 2026-10-27

Cut from c562d2e574fe4de1a647a8e4a719ab319f969749. 3 pull requests since v1.0.1.

Dogfood gate waived: moo is a language runtime, not a nova tool: it has no nova CLI reference or dogfood receipts, and no sprint spend store or provider spend

Spend gate waived: moo is a language runtime, not a nova tool: it has no nova CLI reference or dogfood receipts, and no sprint spend store or provider spend (window 2026-10-27T00:00:00Z..2026-10-27T18:58:28Z)
- unread: --spend-store <addr> names no sprint store, so the recorded spend cannot be read

- #51 changelog: the v1.0.1 cut record
- #52 conformance asserts the overflow edge per build; CI on macOS and Windows
- #53 release readiness for v1.0.2: version and CHANGELOG

## v1.0.2

A point release: the conformance suite asserts the overflow edge in both builds again, and pull
requests build and test on macOS and Windows as well as Linux. No change to the VM, compiler or
libraries. `moo --version` prints `moo 1.0.2 (Luau 0.741)`. Pull requests #51 and #52.

### Fixed

- Four upstream conformance scripts assert the overflow edge again instead of avoiding it: the
  `10^33` table key (`attrib.luau`), `math.huge * 2` (`math.luau`), the `math.pow(2, i)` loop to
  50 (`tables.luau`) and the depth-2000 sums (`pcall.luau`). Release asserts the saturated
  `math.huge`; a checked build asserts the named `number overflow in '<op>'`. The conformance
  runner gives every script the test-only global `moocheckedbuild`; there is no new API.

### CI

- Pull requests run Moo.Test and Luau.Conformance on macOS (`macos-latest`, clang,
  `-DLUAU_WERROR=ON`) and Windows (`windows-latest`, MSVC), built with sccache like the Linux
  jobs. The nightly is unchanged.

## v1.0.1 — 2026-10-27

Cut from f34ee8a0093a40292379a00af0f4347056909b06. 8 pull requests since v1.0.0.

Dogfood gate waived: moo is a language runtime, not a nova tool: it has no nova CLI reference or dogfood receipts, and no sprint spend store or provider spend

Spend gate waived: moo is a language runtime, not a nova tool: it has no nova CLI reference or dogfood receipts, and no sprint spend store or provider spend (window 2026-10-25T00:00:00Z..2026-10-27T17:50:56Z)
- unread: --spend-store <addr> names no sprint store, so the recorded spend cannot be read

- #43 changelog: the v1.0.0 cut record
- #44 docs: replace the logo with the terminal cow
- #45 Clean up moo's own source: one definition per rule, terse current-state comments
- #46 tests, docs: organise moo's tests and design records by subject
- #47 ^ raises the same errors as math.pow
- #48 Every build configuration green; CI runs checked and gcc -Werror
- #49 Sccp: refuse a saturating fold in a checked build, as mooFold does
- #50 release readiness for v1.0.1: version and CHANGELOG

## v1.0.1

A point release of fixes after v1.0.0: `^` raises the same errors as `math.pow`, every build
configuration is green, and the bytecode-graph constant propagation never folds a value a checked
build raises for. moo's own source and tests are reorganised with no change in behaviour.
`moo --version` prints `moo 1.0.1 (Luau 0.741)`. Pull requests #44 to #49.

### Fixed

- `^` now raises the same errors as `math.pow`. `0 ^ n` for any `n < 0`, integer or fractional, is
  `division by zero` (before, `0 ^ -0.5` and `math.pow(0, -0.5)` were 0); a negative base with a fractional exponent is `pow of a negative number to a fractional
  power`; a power that saturates is `number overflow in '^'` in a checked build and saturates in
  release. Before, the operator returned 0 or the saturated value with no error. The operator,
  `math.pow` and its fastcall share one definition (`moo_numpow`) and one error helper
  (`moo_numpowerror`), so a fractional power that saturates now raises in a checked
  `math.pow` too (#47).
- Every build configuration is green: a checked build (`MOO_OVERFLOW_ERRORS=ON`) passes Moo.Test
  and the conformance suite, whose tests now assert the release value in release and the named
  overflow error in a checked build; gcc builds with `-DLUAU_WERROR=ON`; and
  `tools/moo-check-skips.sh` passes, every upstream `#if 0` case naming its reason. Pull requests
  run a checked job and a gcc `-Werror` job, and the nightly gcc build uses `-Werror` (#48).
- The bytecode-graph constant propagation (`Sccp.h` `evaluateNumberBinaryOp`) no longer folds
  `+ - * / //` to a saturated value in a checked build; it refuses the fold, as the compiler's
  constant folding does, so the VM raises `number overflow`. Only the JIT inliner calls it, and
  moo compiles that out, so no released program was affected (#49).

### Source cleanup

- moo's own source has one definition per rule and terse comments that state the current
  behaviour; behaviour is bit-identical. `moo_numfloorquot` is the one floor quotient,
  `moo_nummulwide` the one rounded 128-bit product, and `moo_numfromfloat` the one float
  conversion; `vec3`, `quat` and `transform` share their check, push and payload helpers and call
  fixed's transform functions directly (#45).

### Tests and design records by subject

- moo's own tests are organised by subject instead of by the work card that added them:
  `tests/moo/<subject>.test.cpp` (doctest suite `<subject>`) and
  `tests/conformance/moo/spec/<subject>.luau`, for `number`, `literals`, `boundary`, `buffer`,
  `bytecode`, `compiler`, `removed`, `format`, `math`, `math_oracle`, `pow`, `oracle`,
  `differential`, `vec3`, `quat`, `transform`, `gc`, `geometry_oracle`, `coroutines`, `errors`
  and `upstream`. 50 test files become 21 and 42 scripts become 20; test case names say the
  behaviour (`PowIntCertainEverywhere`, `BytecodeRefusesLuauByName`), never the card. Every check
  still runs; three cases that repeated another case are gone (docs/DESIGN.md "Tests" lists the subjects and how to
  run the wider checks).
- The per-card design notes of layers 3 to 5 move to `docs/history/`. `docs/DESIGN.md` now
  states the current behaviour they specified: the bytecode refusals, the compiler's folding
  limits, the exact-integer and boundary error texts, the math library's definitions, constants
  and asserted error bounds, `moo_pow_int`'s proof, the vec3/quat/transform key hash, and how the
  `^` operator behaves today (#46).

### Docs

- The README's logo is the terminal cow (#44).

## v1.0.0 — 2026-10-25

Cut from 93bf57c8fee1e213df6c28ac2df29a9985178119. 0 pull requests since this repository's first commit.

Dogfood gate waived: moo is a language runtime, not a nova tool: it has no nova CLI reference or dogfood receipts, and no sprint spend store or provider spend

Spend gate waived: moo is a language runtime, not a nova tool: it has no nova CLI reference or dogfood receipts, and no sprint spend store or provider spend (window 2026-10-17T00:00:00Z..2026-10-25T16:05:17Z)
- unread: --spend-store <addr> names no sprint store, so the recorded spend cannot be read

- no pull request merged since the previous tag

## v1.0.0

The first release of moo: Luau 0.741 made deterministic, with `number` as Q48.16 fixed point.
`moo --version` prints `moo 1.0.0 (Luau 0.741)`; the C API has `MOO_VERSION` in
`VM/include/moo.h`. The release tool's record of this cut lists pull requests between tags, so
for this first tag it lists none; the pull requests are named below.

### Determinism without numbers (layers 1 and 2)

- Objects hash by a per-VM creation number, not their address; `tostring` of a table, function,
  userdata or thread prints its type and that number, such as `table: #123` (#2).
- The GC is paced by allocated bytes and reads no clock; `tools/moo-check-no-clock.sh` refuses a
  clock, `getenv` or the C random generator in the VM archive (#3).
- Userdata destructors run in creation order at the end of the GC step that found them dead,
  modelled in `tla/DestructorQueue.tla` (#4).
- The `os` library is removed; `math.random` starts from a constant seed unless the host calls
  `moo_setrandomseed`; native code generation and the JIT bytecode inliner are excluded from the
  build (#1, #9, #15).
- The determinism suite (`tests/Determinism.test.cpp`) runs adversarial programs and compares
  pinned state and effect hashes (#5).

### `number` is Q48.16 fixed point (layer 3)

- `number` is the raw `fixed_t` of [fixed](https://github.com/mas-bandwidth/fixed), vendored
  under `extern/fixed` at `4a70c64` and checked against its pin in CI (#7, #11, #40).
- Overflow saturates; checked builds raise `number overflow in '<op>'` (#8, #21). Division by
  zero is an error; there is no NaN or infinity.
- Decimal literals round once to the nearest value, ties to even, and a literal out of range is
  a compile error; the compiler and the bytecode carry raw Q48.16 constants in a moo bytecode
  version, and Luau bytecode is refused (#12, #19).
- Numeric `for` trip counts are exact (#13); `tostring`, `tonumber` and `string.format` are
  exact and use no libc (#14).
- Integer arguments must be integer-valued; floats exist only at the C API boundary and in
  `buffer` and `string.pack` float formats, converted by one exact rule (#8, #31).
- Luau's float `vector` type is removed (#31). The experimental `integer` library is not
  opened and the analyzer has no `integer`; the type tag and its C API functions stay in
  `lua.h` to keep upstream merges small.
- The layer 3 oracle checks every number operator bit for bit against fixed's C (#17).

### The math library on fixed point (layer 4)

- Every `math` function and `^` are computed in fixed point with no libm (#23, #25, #31). The
  compiler never folds a `math` call; `^` with an integer exponent folds through the same
  `moo_pow_int` the VM runs.
- The CORE-MATH oracle measures each function's error against the correctly rounded result
  (#24, #31).

### Conformance (layer 5)

- Luau's own suites (`Luau.Conformance`, `Luau.UnitTest`, `Luau.CLI.Test`) run with every case
  moo skips or moves giving its `moo:` reason (`tools/moo-check-skips.sh`); the determinism suite's hashes match on Linux gcc and clang,
  macOS arm64 and Windows MSVC and clang-cl in the nightly (#10, #31).
- Error messages and tracebacks are pinned byte for byte; adversarial coroutine programs, and the
  coroutine TLA+ model `tla/Coroutines.tla` (#31, #39).

### vec3, quat and transform (layer 3b)

- `vec3`, `quat` and `transform` are immutable fixed-point value types with operators,
  libraries (`vec3.dot`, `cross`, `length`, `normalize`; `quat.conjugate`, `nlerp`;
  `transform.inverse`, `point`, `direction`, `invpoint`, `invdirection`), C API functions
  (`moo_pushvec3`/`moo_tovec3` and the quat and transform pairs) and analyzer types
  (#33, #34, #35, #36).
- The 3b oracle checks all 22 operations bit for bit against fixed's C, with a reversed witness
  that plants wrong operations and must go red (#37, #41); `tools/moo-fixed3d-compare.sh`
  compares them with fixed3d's own functions built with `BOX3D_FIXED_SATURATE`, 0 differences,
  in CI (#41).

### Known limitations / next

- **Every `vec3`, `quat` and `transform` operation allocates a new object.** Expression-temporary
  reuse in the compiler is proposed in `docs/DESIGN.md` ("Representations costed") but its design
  and build are open (roadmap row "Expression-temporary reuse for vec3, quat and transform").
- **`quat.slerp`** raises `quat.slerp waits for fixed's slerp` until `fixed` has one.
- The representation benchmarks in `docs/DESIGN.md` have not been rerun on the built value types.
- Layers 3 and 3b are built and tested and await their lock review; layer 6 (an existing Luau
  consumer switching to moo) has not started. Virtual Go runs its determinism checks against
  moo's `main` nightly in its own repository.
- The sanitizer configuration of the determinism suite is not in the nightly matrix.
- Windows is checked in the nightly only, not on every pull request.
- moo runs the interpreter only: no native code generation.
