# moo-l5-coroutine-programs

Layer 5, card moo-l5-coroutine-programs. Adversarial coroutine programs for the determinism
suite, and the layer 5 conformance and C++ tests that pin the same transitions at the C API
boundary.

## Decision

No production code changes. The coroutine transition rules in the VM are already implemented and
deterministic (DETERMINISM.md 21); this card exercises every transition and refusal and pins it,
rather than changing behaviour. The coroutine TLA+ model (`tla/Coroutine.tla`) is drafted
separately per the card's SOURCE line and is not part of this card; when it lands, the tests'
comments can cite its actions.

## What changed

- `NEW tests/determinism/coroutines2.luau`: the adversarial program. One section per coroutine
  transition and one per refused transition:
  - generator (resume -> running -> yield -> suspended -> return -> dead), yield across `pcall`,
    errors thrown from nested resumes caught by the parent, and a deep nesting chain of 12
    coroutines that unwinds to the deepest yield;
  - close inside `pcall` on a suspended and on an errored coroutine, and close of a finished dead
    coroutine;
  - refusals with their exact error text: `"cannot close running coroutine"`, `"cannot resume dead coroutine"`, `"cannot resume running coroutine"`, `"cannot resume dead coroutine"` from a
    dead wrap, and `"attempt to yield across metamethod/C-call boundary"` from a yield inside a
    `__index` metamethod and inside a `table.sort` comparator.
- `tests/determinism/expected.txt`: added the `coroutines2.luau` effects and state hashes for the
  suite; the existing four programs' hashes are unchanged (verified, clang and gcc identical).
- `NEW tests/moo/moo-l5-coroutine-programs.test.cpp`: twelve doctest cases, one per transition or
  refusal, asserting the exact error text of each refusal at the C API boundary.
- `NEW tests/conformance/moo/l3/moo-l5-coroutine-programs.luau`: the same behaviours as a
  self-checking script that returns `"OK"`.

Both platforms (gcc 13.3 and clang 18.1, x86-64 Linux) produce identical hashes for the whole
suite, including `coroutines2.luau`.

## Sentences that must change when the layer 5 closing card folds this note

- DESIGN.md, "The rest of determinism", the coroutines bullet:
  `**Coroutines** are cooperative and already deterministic; layer 5 models them in TLA+ and tests them adversarially.`
  should gain a pointer to `tests/determinism/coroutines2.luau` and the layer 5 conformance script.
- DETERMINISM.md, item 21 ("Coroutines"), the final sentence:
  `So coroutines are deterministic once everything they compute is. **Proof owed:** a TLA+ model of resume/yield/close/error states (layer 5), and adversarial coroutine programs in the determinism suite.`
  should become a statement that the adversarial programs (`coroutines` and `coroutines2`) are in
  the suite and that `tla/Coroutine.tla` is the model, if it has landed.

## Remaining dependency

Card `moo-l3-test-layout` (DEPENDS-ON) is not merged into this checkout: `tests/moo/` and
`tests/conformance/moo/l3/` do not exist here, and `tests/MooHost.h` predates `mooRun` /
`mooOpenTestHelpers` / `mooRunFile`. `tests/moo/moo-l5-coroutine-programs.test.cpp` and
`tests/conformance/moo/l3/moo-l5-coroutine-programs.luau` are written against that card's
documented API and will be picked up by its CMake glob once it lands; this card could not build or
run them in this checkout. The determinism suite (`coroutines2.luau` + `expected.txt`) was built
and run to green under clang and gcc here.
