# moo-l5-errors-tracebacks

Layer 5, card moo-l5-errors-tracebacks. Error messages and tracebacks compared byte for byte.

## Audit of number formatting in error and traceback messages

DETERMINISM.md 20 says error messages and tracebacks are deterministic once number formatting (8)
and `tostring` (6) are, and asks for the numbers in messages to be routed through moo's formatter.
This card audited every `luaO_pushfstring` / `luaO_pushvfstring` / `luaG_runerror` format in the VM
for a number argument formatted by `vsnprintf` (`%f`, `%g`, `%e`, or a number cast to `int` and
printed with `%d` where the value may be fractional).

The only numeric conversion specifier found in the error/traceback formats is `%d` in
`VM/src/ldebug.cpp:349` (`"%s:%d: %s"`, `chunkid`, `line`, `msg`) and the traceback builders at
`VM/src/ldebug.cpp:676` (`":%d"`, `ar.currentline`) and `:692` (`"... (+%d frames)\n"`, a frame
count). All three print integers a line number or a frame count, which are not fractional and are
deterministic. Every other `%s` argument is a type name, chunk name or pre-formatted string (from
`tostring`, which already goes through moo's formatter). No `%f`, `%g` or `%e` appears in any error
or traceback format, and no fractional number is cast to `%d`.

**So no site needs a change.** The reported PATHS-PROPOSED (`VM/src/ldebug.cpp`), `VM/src/lobject.cpp`) are listed for completeness but were not edited; routing numbers through moo's formatter in
the VM is not needed because no error or traceback format prints a numeric value. The determinism
suite now pins the actual error strings and tracebacks byte for byte (`errors.luau` below), which is
the real guarantee DETERMINISM.md 20 asks for.

## What changed

- `NEW tests/determinism/errors.luau`: the adversarial program. Every kind the VM raises is caught
  with `pcall` and logged with `debug.traceback` (the value's type, the error string and the
  traceback): arithmetic on nil and on a string, calling a nil and a number, indexing a nil with a
  string and a number, a nil table key, the named moo errors (division by zero, `'for' step is zero`),
  errors in metamethods (`__index` that errors, `__len` returning a non-number, `__index` /
  `__newindex` chains 200 deep), concatenation and comparison on tables, the coroutine refusals
  (resume dead, close running, yield across the metamethod boundary) and stack overflow at a fixed
  recursion depth. Every string is logged, so the effects hash covers them all, and the state hash
  covers the VM state mid-cycle and after a full collection.
- `tests/determinism/expected.txt`: added the `errors.luau` effects and state hashes for the suite;
  the existing programs' hashes are unchanged (verified, clang and gcc identical).
- `NEW tests/moo/moo-l5-errors-tracebacks.test.cpp`: nine doctest cases, one per error kind,
  asserting the exact error text at the C API boundary.
- `NEW tests/conformance/moo/l3/moo-l5-errors-tracebacks.luau`: the same phrases as a self-checking
  script that returns `"OK"`.

Both platforms (gcc 13.3 and clang 18.1, x86-64 Linux) produce identical hashes for the whole suite,
including `errors.luau`.

## Sentences that must change when the layer 5 closing card folds this note

- DESIGN.md, "The rest of determinism", the errors bullet:
  `**Errors and tracebacks** are deterministic strings: numbers in them go through moo's formatter.`
  should become a statement that the audit found no number format in the VM's error/traceback
  messages, and that `tests/determinism/errors.luau` pins every error string and traceback byte for
  byte in the determinism suite.
- DETERMINISM.md, item 20 ("Error messages and tracebacks"), the last paragraph:
  `Deterministic once number formatting (8) and tostring (6) are. **Fix:** route numbers in messages through moo's formatter; the determinism suite compares error strings and tracebacks byte for byte.`
  should become a statement that the audit found no numeric conversion specifier in the VM's error or
  traceback formats (only integer line and frame counts), so no change was needed, and that
  `tests/determinism/errors.luau` pins the error strings and tracebacks byte for byte.
