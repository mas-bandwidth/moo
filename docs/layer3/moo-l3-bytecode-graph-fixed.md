# moo-l3-bytecode-graph-fixed: Sccp folds numbers with moo's operations, not doubles

Card `moo-l3-bytecode-graph-fixed`. The bytecode-graph constant propagation (Sccp, used by the moo-bytecode tool)
folded number constants in double arithmetic (`Bytecode/include/Luau/Sccp.h` `evaluateNumberBinaryOp`, called from
`Bytecode/src/Sccp.cpp` `BcVmConstImpl::evaluate`). A folded constant there would therefore not have been the VM's
result: a double fold, rounded by the boundary rule, can differ from the fixed-point operation's own bits, and a
constant that saturates or rounds differently becomes a wrong, baked-in value.

## Decision: Sccp folds on the raw Q48.16 value with moo_number.h

**Choice.** `evaluateNumberBinaryOp` now takes `fixed_t` (raw `int64_t`) operands and calls the same `moo_number.h`
operations the compiler's `mooFold` and the VM call:

- `+`, `-`, `*` use `moo_numadd`/`moo_numsub`/`moo_nummul` (exact or saturating).
- `/`, `//`, `%` use `moo_numdiv`/`moo_numidiv`/`moo_nummod`, and are never folded when the divisor is `0`; the VM
  raises the named error `division by zero` at run time.
- `^` is never folded: it is still on the libm bridge (`docs/DESIGN.md "pow and ^"`).
- comparisons use the raw `int64_t` (the generic `bcCompare` template).

`BcVmConstImpl::asNumber` returns the raw value: for a `Number` `BcVmConst` it is `valueNumber`; for an `Int`
immediate (`LOADN`) it is `moo_numfromint(valueInt)`, the raw Q48.16 value of the integer the VM loads (the VM's
`cast_num` is `moo_numfromint`). The `double` overloads of `bcCompare` and `asNumber` are gone.

**Why.** The VM's number is Q48.16; a folded constant must be the VM's result bit for bit. The compiler already folds
with `moo_number.h`; the bytecode graph is the other fold site. Folding on doubles reintroduces a platform-dependent and
wrong rounding, which violates determinism.

## What changed, file and function

| file | function | change |
|---|---|---|
| `Bytecode/include/Luau/Sccp.h` | `bcCompare(double, double)` | removed (the generic template compares raw `int64_t`) |
| | `evaluateNumberBinaryOp` | now `std::optional<fixed_t>` on raw operands, calls `moo_numadd/sub/mul/div/idiv/mod`; `/ // %` by zero and `^` are never folded |
| | `VmConstOps::asNumber`, `BcVmConstImpl::asNumber` | return `fixed_t` (raw), not `double` |
| | `Sccp::arithToK` | `valueNumber` is `fixed_t`; the number `1.0` test is `MOO_NUMBER_ONE`, not raw `1` |
| `Bytecode/src/Sccp.cpp` | `BcVmConstImpl::evaluate` | stores the raw fold directly (`result.valueNumber = *resultOpt`), no `mooNumberFromDouble` |
| | `BcVmConstImpl::asNumber` | `Number` `VmConst` returns `valueNumber`; `Int` immediate returns `moo_numfromint` |

Inliner/src/TValueVmConstImpl.cpp is not built (`MOO_JIT_INLINER` off) and is not changed.

## Tests

`tests/Sccp.test.cpp` (Luau.UnitTest, suite `Sccp`): `sccp_phi_filters_dead_predecessor` compares `asNumber` on the
raw value (`moo_numfromint(42)`), not `42.0`; the NaN cases were already skipped (a number has no NaN). The suite is
shown separately as `build/Luau.UnitTest -ts=Sccp*`.

`tests/moo/moo-l3-bytecode-graph-fixed.test.cpp` (Moo.Test, suite `moo-l3-bytecode-graph-fixed`):

- `MooSccpFoldOperatorsMatchMooNumber`: on 64 seeded raw operands (splitmix64, seed 1000; includes `0`, `1`, `MOO_NUMBER_ONE`,
  `MOO_NUMBER_MAX`/`MIN` and edges), each folded operator equals the `moo_number.h` result bit for bit; division, floor
  division and remainder by zero are left unfolded.
- `MooSccpPowNeverFolded`: `^` is never folded on any pair.
- `MooSccpGraphFoldsAddOnRaw`: through the Sccp pipeline, `LOADN 3 + LOADN 5` folds to the raw value `moo_numadd(3, 5)`,
  so a folded constant is the VM's result.

`tests/conformance/moo/l3/moo-l3-bytecode-graph-fixed.luau` (MooL3Scripts): checks that `+`, `/`, `//`, `%` give the raw
Q48.16 values, and that `/` and `%` by zero raise `division by zero` at run time (not folded away).

## The sentences of DESIGN.md and DETERMINISM.md that must change (folded in by moo-l3-conformance)

DESIGN.md, "Layer 3 as built", **The compiler**, the bullet

> The compiler folds number arithmetic with the same `moo_number.h` operations (a constant still travels as the double
> that holds it exactly); it does not fold division by zero, `^`, or any builtin call or `math` constant.

must become

> The compiler folds number arithmetic with the same `moo_number.h` operations (the AST still holds the double that
> holds a constant exactly; the builder converts it once to the raw value by the boundary rule); the bytecode-graph
> constant propagation (`Bytecode/include/Luau/Sccp.h` `evaluateNumberBinaryOp`) folds the same way on the raw value.
> Neither folds division by zero, `^`, or any builtin call or `math` constant.

DETERMINISM.md: add a numbered finding (next number after 23):

> ### 24. The bytecode-graph constant propagation folds doubles (Sccp)
>
> `Bytecode/include/Luau/Sccp.h:70` `evaluateNumberBinaryOp` and `Bytecode/src/Sccp.cpp:26` (`BcVmConstImpl::evaluate`)
> folded number constants in double arithmetic, so a folded constant was the boundary-rounded double, not the VM's
> Q48.16 result, and could differ from run-time arithmetic or saturate wrongly.
>
> **Fix:** built in layer 3 (`moo-l3-bytecode-graph-fixed`): Sccp folds on the raw Q48.16 value with the same
> `moo_number.h` operations the compiler and VM use; division and `%` by zero and `^` are never folded.

## Not done

`tests/Sccp.test.cpp` upstream cases that expected double results beyond the one updated are not present: the NaN cases
were already skipped by the bytecode-version card with a `moo:` reason.
