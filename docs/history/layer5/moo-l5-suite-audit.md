# Layer 5: the suite audit

This note is for `moo-l5-conformance`, which folds it into `docs/DESIGN.md` and
`docs/DETERMINISM.md`. It records the decision this card makes, what changed, and the exact
sentences the two documents must change. The card's rule is `docs/DESIGN.md` "Layers", layer 5:
the Luau suite is adapted for fixed, not silenced.

## Decision

**A skipped case caused by moo names its reason with `moo:`; a skip that predates moo is
reported, not rewritten here.** The audit (`tools/moo-check-skips.sh`) lists all four kinds of
record: a doctest skip decorator, an early return that carries a `moo:` reason (the codegen
cases), a `#if 0` block that holds a `TEST_CASE`, and a moved conformance script under
`tests/conformance/moo`. A record without a `moo:` reason on its line or the line above (a moved
script: in its comments) fails the run, and so does a reason that names a replacement which does
not exist.

The run at this base finds **61 records**: 29 doctest skips, 2 marked early returns, 11
`#if 0` blocks, and 19 moved scripts. Eleven records fail the reason rule, and all eleven are
upstream `#if 0` blocks that predate moo and carry an upstream ticket comment instead:

- `tests/Frontend.test.cpp:635`
- `tests/NonStrictTypeChecker.test.cpp:882`
- `tests/Normalize.test.cpp:278`, `:334`, `:1546`
- `tests/StringUtils.test.cpp:38`
- `tests/TypeFunction.test.cpp:961`
- `tests/TypeInfer.functions.test.cpp:2790`
- `tests/TypeInfer.test.cpp:2616`
- `tests/TypeInfer.typestates.test.cpp:99`, `:322`

The reason to report them rather than edit them: none of those files is in this card's `NEW`
list, and adding a `moo:` sentence to eleven analyzer/benchmark test files would spread this seam
across upstream files that merge often, for skips moo did not create. They are listed for a
follow-up; `moo-l5-conformance` can fold a bulk annotation or an explicit upstream-skip rule into
the closing card. The audit is still useful now: a new unmarked skip anywhere, or a `moo:` reason
that points at a test which no longer exists, fails the run.

## What changed

- **`tools/moo-check-skips.sh`** (new). One `awk` pass over every `tests/**/*.cpp` classifies a
  doctest skip decorator, an early return whose line or previous lines carry `moo:`, and a `#if 0`
  block that contains `TEST_CASE`. A `find` pass lists every `tests/conformance/moo/**/*.luau`
  as a moved script. The second pass reads each record's reason, ignores lines that say
  `upstream` or `Replaces` (that names the file moo removed, not a replacement), and resolves
  every `tests/...` path and any following CamelCase test name with `grep`. It prints the count by
  file and by kind, then every failure, and exits 1 on a failure. It runs in under a second.
- **`tests/moo/moo-l5-suite-audit.test.cpp`** (new). `MooL5SkippedCasesNameAReason` applies the
  same rule in C++ to the doctest skips and the two marked early returns, resolves their
  replacement references, and pins the eleven upstream `#if 0` blocks as the known gap (none is a
  `tests/moo` file, so moo's own tests cannot gain an unmarked skip).
  `MooL5MovedScriptsNameAReason` requires a `moo:` or `moo-specific` comment in every moved
  script and resolves its references; it expects 19 scripts after this card's own.
- **`tests/conformance/moo/l3/moo-l5-suite-audit.luau`** (new). MooL3Scripts runs it; it pins the
  reason grammar the checker consumes: a non-empty text after `moo:`, `tests/X` for a replacement,
  an optional CamelCase case name, and `upstream`/`Replaces` lines excluded from replacement
  resolution.

## Exact sentences to change

### `docs/DESIGN.md` "Layers", layer 5

Current sentence:

> 5. **Conformance.** The Luau suite adapted for fixed; a cross-platform determinism suite that
>    runs each program on Linux gcc and clang, macOS arm64 and Windows and compares full VM state
>    hashes and effect logs, including adversarial programs (object-keyed tables, heavy GC with
>    destructors and weak tables, coroutines); bit-exact agreement with `fixed`'s C; fuzzing; and
>    the coroutine TLA+ model.

Add after "The Luau suite adapted for fixed":

> Every moved or skipped upstream case states its reason in a `moo:` comment and, where it names
> the moo test that replaces it, that test resolves; `tools/moo-check-skips.sh` lists them and
> fails on a missing reason or a replacement that does not exist.

### `docs/DESIGN.md` "Layer 3 as built"

The anchor bullets are exactly:

> - **Native code generation** is excluded (below).
> - **TEMPORARY bridges**, each marked in the code and each removed by a later layer 3 PR:

Insert between them:

> - **The suite says what it skips.** Every skipped doctest case and every moved conformance
>   script states its reason with `moo:`; `tools/moo-check-skips.sh` lists them and fails when a
>   reason is missing or names a replacement that does not exist.

### `docs/DETERMINISM.md` "Status"

The current sentence ends:

> When literals became exact, `strings.luau` and `attrib.luau` moved too (literals beyond the
> range are compile errors), nine parser, linter, pretty-printer and frontend cases that assert
> double literals or precision lints are skipped with the reason beside each, and four compiler
> cases had their expected decimal constants updated to the Q48.16 values, marked `// moo:`.

and the later sentence is:

> Twenty compiler cases that asserted folding moo turned off are skipped in
> `tests/Compiler.test.cpp`, each pointing at `tests/MooCompiler.test.cpp`, which tests moo's
> folding.

Append after the first sentence:

> `tools/moo-check-skips.sh` lists every doctest skip, marked early return and moved script; it
> fails when one lacks a `moo:` reason or names a replacement that does not exist.

Append after the second sentence:

> The same audit reports eleven upstream `#if 0` blocks that predate moo and still carry their
> upstream ticket comment; they are listed for a follow-up, not counted as moo skips.
