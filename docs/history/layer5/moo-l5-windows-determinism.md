# Layer 5: the determinism suite on Windows MSVC and clang-cl

This note is for `moo-l5-conformance`, which folds it into `docs/DESIGN.md` and
`docs/DETERMINISM.md`. It records the decision this card makes, what changed, the exact
sentences the two documents must change, and the nightly run time.

## Decision

**The Windows determinism run stays nightly and gates nothing; it covers both supported Windows
compilers.** The one-minute CI rule (`docs/DESIGN.md` "Layers" is checked on every push by
`.github/workflows/ci.yml`, whose `push-gate` job builds only `Moo.Test` and the VM library) is
why Windows is not a required check. The nightly `windows` job keeps the existing
`schedule` / `workflow_dispatch` condition and runs the same programs under both Windows
compilers: `windows-msvc` (the default Visual Studio x64 toolset) and `windows-clang-cl`
(`-T ClangCL`), with `fail-fast: false` so a failure in one names that compiler without hiding a
failure in the other.

No production code changes. `tests/Determinism.test.cpp` already reads
`tests/determinism/expected.txt`, and its `DeterminismSuite` case fails with a non-zero exit when
either the effects hash or the state hash differs, so running `Moo.Test.exe` is the hash check:
no second comparator, and no change to any frozen hash. `Moo.Test` also contains the math oracle
(`tests/moo/moo-l3-oracle-harness.test.cpp`) and the rest of moo's tests, so the Windows job
runs the oracle on the same compilers.

## What changed

- **`.github/workflows/nightly.yml`**, job `windows-msvc` becomes job `windows` with a matrix:
  - `name: ${{ matrix.name }}` so the two checks read `windows-msvc` and `windows-clang-cl`.
  - matrix entries `windows-msvc` with `toolset: ""` and `windows-clang-cl` with
    `toolset: "-T ClangCL"`; `fail-fast: false`.
  - the `configure` step gains `${{ matrix.toolset }}`:
    `cmake -S . -B build -A x64 ${{ matrix.toolset }} -DLUAU_WERROR=OFF`.
  - the build and run steps are unchanged:
    `cmake --build build --config RelWithDebInfo --parallel 4 --target Moo.Test Luau.Conformance Luau.UnitTest`,
    then `build/RelWithDebInfo/Moo.Test.exe` (the determinism suite and the oracle), then
    `Luau.Conformance.exe` and `Luau.UnitTest.exe`.

There is no new C++ function or script. The rule each step implements is `docs/DESIGN.md`
"Layers", layer 5: "a cross-platform determinism suite that runs each program on Linux gcc and
clang, macOS arm64 and Windows and compares full VM state hashes and effect logs"; the Windows
half is now both compilers named by `docs/DETERMINISM.md` "The guarantee".

## Nightly run time

The checkout is offline and carries no GitHub Actions run history, so the last nightly's run
time could not be read here. This note does not guess it. What is known from the file is the
budget: the Linux/macOS matrix job has `timeout-minutes: 60` and the Windows job keeps
`timeout-minutes: 90`; the Windows job builds three targets and runs three binaries per
compiler. `moo-l5-conformance`, which can read the scheduled run, should replace this paragraph
with the measured `windows` job wall time (both matrix entries) from the last nightly.

## Exact sentences to change

### `docs/DESIGN.md` "Layers", layer 5

Current sentence:

> 5. **Conformance.** The Luau suite adapted for fixed; a cross-platform determinism suite that
>    runs each program on Linux gcc and clang, macOS arm64 and Windows and compares full VM state
>    hashes and effect logs, including adversarial programs (object-keyed tables, heavy GC with
>    destructors and weak tables, coroutines); bit-exact agreement with `fixed`'s C; fuzzing; and
>    the coroutine TLA+ model.

Replace "Linux gcc and clang, macOS arm64 and Windows" with "Linux gcc and clang, macOS arm64,
and Windows with MSVC and clang-cl", and append:

> The Windows pair runs in the nightly, which gates nothing: it builds and runs `Moo.Test` (the
> determinism suite and the math oracle) under both compilers and fails when either hash differs
> from `tests/determinism/expected.txt`.

### `docs/DESIGN.md` "Layer 3 as built"

No existing sentence changes. The anchor bullets are exactly:

> - **Native code generation** is excluded (below).
> - **TEMPORARY bridges**, each marked in the code and each removed by a later layer 3 PR:

Insert between them:

> - **The determinism suite runs on Windows.** The nightly builds `Moo.Test` (the determinism
>   suite and the math oracle) on `windows-latest` with MSVC and with clang-cl and runs it; the
>   job fails when either the effects hash or the state hash differs from
>   `tests/determinism/expected.txt`. No frozen hash changes.

### `docs/DETERMINISM.md` "Status"

The current sentence is:

> `MOO_DETERMINISM_PRINT=1` prints the table's lines. The nightly runs it on Linux gcc, Linux clang
> and macOS arm64; CI runs it on Linux clang on every push.

It must become:

> `MOO_DETERMINISM_PRINT=1` prints the table's lines. The nightly runs it on Linux gcc, Linux
> clang, macOS arm64 and Windows MSVC and clang-cl; CI runs it on Linux clang on every push.

### `docs/DETERMINISM.md` "The guarantee"

No change is needed: the platform sentence already names both Windows compilers. It is exactly:

> **Supported platforms** are Linux (gcc and clang), macOS arm64 and Windows (MSVC and
> clang-cl), 64-bit only.

The following paragraph also already states the rule this card enforces:

> The conformance layer (layer 5 in [DESIGN.md](DESIGN.md)) checks the guarantee: every program
> in the determinism suite runs on each platform and compiler, and the full VM state hash and
> the effect log must match bit for bit.

## Validation performed here

- `python3 -c 'import yaml;yaml.safe_load(open(".github/workflows/nightly.yml"))'` exits 0.
- `git diff --name-only` names only `.github/workflows/nightly.yml` and this new note.
- The worker cannot run Windows. The configure/build/run command sequence was run on the Linux
  bench (clang 21.1.8, Ninja, `LUAU_WERROR=ON`) in its portable form (no `-A`/`-T`, no `.exe`):
  `Moo.Test` built and its `DeterminismSuite` passed, so the hash check the Windows job runs
  matched `tests/determinism/expected.txt` on Linux; the Windows result itself is the
  coordinator's to record.
- The whole `Moo.Test` binary is **not** green at this base, for a reason this card did not
  introduce: `tests/moo/moo-l5-suite-audit.test.cpp` `MooL5SkippedCasesNameAReason` expects 29
  doctest skip decorators and finds 42 (`CHECK( 42 == 29 )`). That is red with this card's
  changes and with them reverted, so the Windows job's `Moo.Test.exe` step will report it
  independently of the determinism hashes. It is outside this card's `PATHS`/`NEW` list and is
  left for the card that owns the audit.
