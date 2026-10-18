# moo-l3-overflow-errors-debug

Checked arithmetic: a build with `MOO_OVERFLOW_ERRORS` raises `number overflow in '<op>'` where
release saturates, so a program either gives the release value or stops with the named error.
Release compiles to the same machine code as before.

## Decisions

- **The switch follows assertions.** `MOO_OVERFLOW_ERRORS` (in `Common/include/moo_number.h`) is 1
  when the build has no `NDEBUG` or defines `LUAU_ENABLE_ASSERT`, the same condition that turns on
  `LUAU_ASSERT` (`Common/include/Luau/Common.h`). Otherwise it is 0. A build can force it with CMake
  `-DMOO_OVERFLOW_ERRORS=ON|OFF` (default `AUTO`, put on `Luau.Common` so every target that includes
  `moo_number.h` sees one value) or `make overflow=on|off` (which also picks its own build directory).
  So CMake Debug is checked and RelWithDebInfo/Release is not. Upstream Luau has no
  `LUAU_ENABLE_ASSERT` CMake option, so `-DLUAU_ENABLE_ASSERT=ON` on the command line does nothing by
  itself. The C macro does.
- **The seam is a saturation test, not a second arithmetic path.** Each saturating operation in
  `moo_number.h` gets a `moo_num*saturates(a, b)` that is true exactly when that operation clamps.
  The value is always the release operation's own, so a checked build can never produce a
  different value. The VM's fast path in each arithmetic arm adds `&& luai_numnosat(op, a, b)`
  (`VM/src/lnumutils.h`). In release that is `(true)` and does not evaluate its operands. Under the
  option an operation that saturates leaves the fast path and reaches `luaV_doarithimpl`
  (`VM/src/lvmutils.cpp`), the same way a zero divisor does. There it raises
  `luaG_runerror(L, "number overflow in '%s'")`, right after the `division by zero` check. The error
  therefore carries the operation's line, behaves like every other runtime error, and also covers
  string-coerced operands and the `K` and `RK` opcode forms.
- **Which operators.** `+`, `-`, `*`, `/`, `//` and `^`. Unary `-` and `%` are exact on the symmetric
  domain `[-INT64_MAX, INT64_MAX]`, so they never saturate and get no check. The tests show that
  neither one raises at the edges in a checked build. `^` goes through the temporary pow bridge, and
  its test is that the libm result is `>= 2^47` in magnitude (`moo_numbridgesaturates`). Below that,
  the rounded value is always in range. NaN is not overflow and still gives 0. When moo-l4-pow-integer
  rebuilds `^` on fixed, it replaces this test with that operation's own.
- **An exact edge is not overflow.** `MAX * 1`, `MAX / 1` and `MAX + 0` give `MAX` and do not raise.
  For `*` the test is exact: `fixMul` clamps only to the two edges, so the test re-rounds only when
  the result is an edge, using fixed's own 128-bit seam (`fixInt128MulI64`, `fixInt128Add`,
  `fixInt128Shr`, so it works both natively and emulated). For `/` the test is `|a| >> 47 >= |b|`,
  meaning the truncated quotient `a * 2^16 / b` reaches `2^63`. For `//` it is the floor quotient
  outside `[-(2^47 - 1), 2^47 - 1]`. For `+` and `-` it is the comparison the operation already makes.
- **The compiler does not fold a saturating operation under the option.** `mooFold`
  (`Compiler/src/ConstantFolding.cpp`) returns early when `mooSaturates(op, a, b)` holds. The constant
  is then computed at run time, where it raises. Today `mooResult` already refuses any result of
  magnitude `>= 2^53` raw, so no saturated value was being folded in either build. The guard keeps
  the rule true once the raw-constant cards (moo-l3-compiler-raw-constants) lift that bound. With the
  option off, the guard is compiled out and folding is unchanged.

## What changed

- `Common/include/moo_number.h`: the `MOO_OVERFLOW_ERRORS` default. `moo_numaddsaturates`,
  `moo_numsubsaturates`, `moo_nummulsaturates`, `moo_numdivsaturates`, `moo_numidivsaturates`, and
  `moo_numbridgesaturates`. None of the existing operations changed.
- `VM/src/lnumutils.h`: `luai_num{add,sub,mul,div,idiv,pow}sat` and `luai_numnosat` (just `(true)` in
  release).
- `VM/src/lvmexecute.cpp`: the fast-path condition of the 14 arms `LOP_ADD`, `SUB`, `MUL`, `DIV`,
  `IDIV`, `POW`, `ADDK`, `SUBK`, `MULK`, `DIVK`, `IDIVK`, `POWK`, `SUBRK`, `DIVRK`.
- `VM/src/lvmutils.cpp` `luaV_doarithimpl`: the raise site.
- `Compiler/src/ConstantFolding.cpp`: `mooSaturates` and the guard in `mooFold`.
- `CMakeLists.txt`: the `MOO_OVERFLOW_ERRORS` cache option (`AUTO`/`ON`/`OFF`). `Makefile`:
  `overflow=on|off`, and a separate `BUILD` directory for each setting.
- Tests: `tests/moo/moo-l3-overflow-errors-debug.test.cpp`:
  - `MooOverflowSaturationTestsAreExact` checks every saturation test against an exact MooWide
    reference over edge values plus 200 seeded operands, and checks the bridge edge.
  - `MooOverflowAdd`, `Sub`, `Mul`, `Div`, `FloorDiv` and `Pow` each cover one runtime and one folded
    case at optimization levels 0 to 2. A checked build gets the exact text
    `moo:1: number overflow in '<op>'`; release gets `MOO_NUMBER_MAX` or `MOO_NUMBER_MIN`. A case one
    step inside the range never raises.
  - `MooOverflowUnaryMinusAndModNeverRaise`.
  - `MooOverflowCoercedOperand` covers the string-coercion path.
- `tests/conformance/moo/l3/moo-l3-overflow-errors-debug.luau` runs in both builds. It uses its
  first edge to tell which build it is in, then checks that every operator, opcode form and folded
  constant agrees with that build.
- `tests/conformance/moo/l3/moo-l3-boundary-tofloat.luau` (outside this card's PATHS, see the
  report) had asserted `max + step == max` and `min - step == min`. Those lines now accept either
  the saturated value or the named error, so the script passes in both builds.

## Release is unchanged

The check was done on the bench with the same clang and the DONE-WHEN RelWithDebInfo configuration
at the base and at this head: `objdump -d --no-show-raw-insn` of every object in `libLuau.VM.a` and
`libLuau.Compiler.a`. The result, and the unchanged frozen determinism hashes
(`tests/determinism/expected.txt`, exercised by `Determinism.test.cpp` in `build/Moo.Test`), are in
the PR body.

## DESIGN.md and DETERMINISM.md sentences that must change

- DESIGN.md, "Overflow": replace "Debug builds (`LUAU_ENABLE_ASSERT` or a moo debug option) raise a
  named error instead: `number overflow in '*'` (and so on per operator)." with: "Checked builds
  raise the named error `number overflow in '<op>'` at the operation, for `+`, `-`, `*`, `/`, `//`
  and `^`. A build is checked when assertions are on (no `NDEBUG`, or `LUAU_ENABLE_ASSERT`), or when
  it sets `MOO_OVERFLOW_ERRORS=ON` (CMake) or `overflow=on` (make). Unary `-` and `%` are exact on
  the domain and never saturate. An exact result at the edge (`MAX * 1`) is not overflow. The
  compiler does not fold an operation that saturates in a checked build." The sentence "Saturation
  is the release behaviour, ..." stays.
- DESIGN.md, "Layer 3 as built", "The number is fixed": after "behind Luau's `luai_num*` seams.",
  add: "A checked build (`MOO_OVERFLOW_ERRORS`, on with assertions) raises
  `number overflow in '<op>'` where release saturates (`moo_num*saturates`, `luai_numnosat`, raised in
  `luaV_doarithimpl`)." In "The compiler", after "it does not fold division by zero, `^`, ..." add
  "or, in a checked build, an operation that saturates".
- DETERMINISM.md: no sentence changes. Release values are unchanged, and a checked build differs
  only by stopping with an error. If DETERMINISM.md's status list names debug overflow errors as not
  built, that entry becomes built.
