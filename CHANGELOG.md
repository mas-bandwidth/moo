# moo changelog

## Unreleased

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
  `^` operator behaves today.

### Fixed

- `^` now raises the same errors as `math.pow`. `0 ^ n` for any `n < 0`, integer or fractional, is
  `division by zero` (before, `0 ^ -0.5` and `math.pow(0, -0.5)` were 0); a negative base with a fractional exponent is `pow of a negative number to a fractional
  power`; a power that saturates is `number overflow in '^'` in a checked build and saturates in
  release. Before, the operator returned 0 or the saturated value with no error. The operator,
  `math.pow` and its fastcall share one definition (`moo_numpow`) and one error helper
  (`moo_numpowerror`), so a fractional power that saturates now raises in a checked
  `math.pow` too.
- Every build configuration is green: a checked build (`MOO_OVERFLOW_ERRORS=ON`) passes Moo.Test
  and the conformance suite, whose tests now assert the release value in release and the named
  overflow error in a checked build; gcc builds with `-DLUAU_WERROR=ON`; and
  `tools/moo-check-skips.sh` passes, every upstream `#if 0` case naming its reason. Pull requests
  run a checked job and a gcc `-Werror` job, and the nightly gcc build uses `-Werror`.

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
