# moo-l3-bytecode-version: the moo bytecode version

Card `moo-l3-bytecode-version`. It removes the first TEMPORARY bridge of "Layer 3 as built". Before this card,
bytecode carried a double for each number constant and the loader rounded it to Q48.16. Now a number constant is the
raw Q48.16 value, and the VM loads only moo bytecode. Each refusal is a named error with a test.

## Decision 1: how moo bytecode is told apart from Luau bytecode

**Choice.** The first byte of a moo blob is `LBC_MOO_VERSION_FLAG | moo version` (`0x80 | 1` = `0x81` today). The
next byte is the Luau version whose format the rest of the blob follows, the same byte Luau writes first. After that
come the types version and the rest of Luau's layout, with the moo changes listed under the moo version.

```
Luau blob:  [Luau version 3..14 or 100] [types version] [strings] ...
moo blob:   [0x80 | moo version]  [Luau version 3..14 or 100] [types version] [strings] ...
error blob: [0] [message]                                  (unchanged)
```

**Why.**

- *A Luau blob is always refused by name.* Every Luau version fits in 7 bits. `BytecodeBuilder.cpp` asserts
  `LBC_VERSION_MAX <= 127`, and the experimental `LBC_VERSION_CLASSES` is 100. So a first byte with the top bit clear
  is a Luau version and never a moo one. The loader refuses each such byte (1 to 127) with
  `this is Luau bytecode (version N), not moo bytecode`, whatever version upstream reaches. Byte 0 stays the error blob.
- *A moo format change cannot collide with Luau on a merge.* moo's version has its own counter
  (`LBC_MOO_VERSION_MIN/MAX/TARGET`, in a new `MooBytecodeTag` enum) and its own place in the blob. A later moo change
  bumps that counter. When an upstream merge adds Luau version 15, it changes `LBC_VERSION_*` and the second byte.
  The two counters never share a number or a line, so the merge needs no renumbering.
- *Luau's feature versions keep working.* The compiler still picks the Luau version from its flags
  (`BytecodeBuilder::getVersion`: 9 by default, and 11 to 14 or 100 under flags). The loader still branches on it
  (`version >= 4`, `>= 11`, `>= 12`). Upstream's format code therefore needs no change, and merging it stays
  mechanical.
- *Rejected: overloading the 7-bit byte.* For example, `0x80 | Luau version` in one byte. That leaves no room for a
  moo version, and a moo bump would need numbers that Luau could also use. Another rejected idea was a new constant
  tag with no version: the loader would still accept Luau blobs as moo, which is the failure this card removes.
- *Limit.* Luau reserves the top bit so that it can move to a varint version someday. A Luau varint version of 128 or
  more would start with a byte whose top bit is set. moo would read that byte as moo version 0 or N. Version 0 is
  refused. Version N would then fail the Luau format check on the next byte, which is below 3. So such a blob is
  refused, though not by the "Luau bytecode" name. Luau is at version 14.

An unknown moo version is the error `moo bytecode version mismatch (expected [1..1], got N)`. A moo blob whose second
byte is not a supported Luau format keeps Luau's own `bytecode version mismatch` error.

## Decision 2: the number constant is 8 bytes of raw Q48.16, little-endian

`LBC_CONSTANT_NUMBER` is followed by the raw `int64_t`, least significant byte first. The writer
(`writeRawNumber`, BytecodeBuilder.cpp) uses shifts and integer stores, with no double and no `memcpy` of host
order. Both readers assemble the same bytes with shifts. The domain is `[-INT64_MAX, INT64_MAX]`. A blob that
carries `INT64_MIN` is refused by name, `moo bytecode number constant out of range`, so the VM never holds a value
outside the domain.

## Decision 3: moo bytecode has no integer constant

The loader refuses `LBC_CONSTANT_INTEGER` with `moo bytecode has no integer constant` (DESIGN.md "integer"). The
compiler emits such a constant only for an integer literal (`Compiler.cpp:2082`, `:2329`, `:3134`). Card
`moo-l3-ast-raw-literals` refuses those at the source. Until that card lands, a script with an integer literal
compiles but does not load (see "Not done").

## What changed, file and function

| file | function | change |
|---|---|---|
| `Common/include/Luau/Bytecode.h` | version history, `MooBytecodeTag` | new `LBC_MOO_VERSION_FLAG/MIN/MAX/TARGET`; the "moo bytecode versions" history |
| `Bytecode/include/Luau/BytecodeBuilder.h` | `Constant`, `addConstantNumberRaw`, `mooNumberFromDouble` | `Constant::valueNumber` is `int64_t` (raw); the `ConstantKey` holds the raw bits |
| `Bytecode/src/BytecodeBuilder.cpp` | `addConstantNumber(double)` | converts once by the boundary rule (`moo_numfromdouble`, ties to even); out of range or infinite saturates, NaN gives 0. This is exactly the old load-time bridge, so the compiler's folding and its output values are unchanged |
| | `addConstantNumberRaw(int64_t)` | new; asserts the domain |
| | `mooNumberFromDouble` | new, the one double-to-raw conversion for the builder, the graph and Sccp |
| | `writeRawNumber`, `writeConstants` | the raw 8 bytes, little-endian |
| | `finalize` (header) | writes `0x80 | LBC_MOO_VERSION_TARGET`, then the Luau version |
| | `dumpConstant` | prints `moo_numtostr` (the shortest decimal that reads back), not `%.17g` of a double |
| `Bytecode/src/BytecodeDump.cpp` | `toString(BcVmConst)` | the same `moo_numtostr` |
| `Bytecode/include/Luau/BytecodeGraph.h` | `BcVmConst`, `addVmConstNumberRaw`, `addVmConstNumber` | `valueNumber` is the raw `int64_t`; `addVmConstNumber(double)` converts once with `mooNumberFromDouble` |
| `Bytecode/src/BytecodeGraph.cpp` | `fromFunctionBytecode`, `toFunctionBytecode` | read and write the raw value (`addConstantNumberRaw`), so a graph round trip is bit exact |
| `Bytecode/src/Sccp.cpp` (outside PATHS, see below) | `BcVmConstImpl::evaluate`, `asNumber` | convert at the two calls only: Sccp still folds doubles until `moo-l3-bytecode-graph-fixed` |
| `VM/src/lvmload.cpp` | `loadsafe` | refuses Luau bytecode and unknown moo versions by name, then reads the Luau version byte; reads the raw number with `setnvalue` (the TEMPORARY comment and `moo_numbridgefromdouble` are gone); refuses `INT64_MIN` and integer constants by name |
| `tests/Compiler.test.cpp` | 10 golden dumps | numbers print shortest (`0.3`, not `0.3000030517578125`); `PreserveNegZero` prints `0`, since a number has no negative zero. Each is marked `// moo:` |

## Tests

`tests/moo/moo-l3-bytecode-version.test.cpp` (Moo.Test):

- `MooBytecodeRefusesLuauByName`: a hand-built chunk in the Luau version 9 format (BASE's `LBC_VERSION_TARGET`) loads
  and returns 1.5 when the moo byte comes first. With first byte 1 to 127 it is refused with the exact named message,
  including 9, 14 and 100. The error blob (byte 0) still carries the compiler's message.
- `MooBytecodeRefusesUnknownMooVersion`: moo versions 0 and 2 to 127 are refused with the exact message. A moo blob
  whose Luau format byte is 2 keeps Luau's mismatch error.
- `MooBytecodeRefusesIntegerConstant`: a hand-built chunk with `LBC_CONSTANT_INTEGER` is refused by name.
- `MooBytecodeRefusesNumberOutsideTheDomain`: `INT64_MIN` is refused by name; `-INT64_MAX` loads.
- `MooBytecodeNumberConstantsAreRaw`: 4096 seeded raw values (splitmix64, seed 1000, every magnitude scale). The set
  includes `MOO_NUMBER_MAX`, `-MOO_NUMBER_MAX`, 1, -1, 2^37 and 2^37 + 1. Each value goes through
  `addConstantNumberRaw`, then `finalize`, then `luau_load`, and must come back bit exact. The blob must hold the 8
  little-endian bytes.
- `MooBytecodeGraphRoundTripIsRaw`: the same 4096 values go through `fromFunctionBytecode` and `toFunctionBytecode`;
  the value and the function bytes come back identical.
- `MooBytecodeDoubleConstantsConvertByTheBoundaryRule`: `addConstantNumber(double)` uses the boundary rule. This
  covers ties to even, saturation, infinity, NaN and -0, and 4096 seeded doubles checked against `moo_fromdouble`.

`tests/conformance/moo/l3/moo-l3-bytecode-version.luau` (MooL3Scripts) checks that, at optimization levels 0, 1 and
2, the compiler's blob starts with `0x81` and a 7-bit Luau version. It checks that `0.1`, `-1.5` and `2^37 + 0.5` load
as raw 6554, -98304 and 9007199254773760. It also checks that the same blob without the moo byte, and the blob with
moo version 2, are each refused by name.

## The sentences of DESIGN.md and DETERMINISM.md that must change (folded in by moo-l3-conformance)

DESIGN.md, "Layer 3 as built", **TEMPORARY bridges**: delete the item

> bytecode carries a double per number constant, rounded by the boundary rule at load, saturating (the bytecode
> version PR);

and add a bullet:

> - **The moo bytecode version** (`Common/include/Luau/Bytecode.h`, "moo bytecode versions"): a moo blob starts with
>   `0x80 | moo version` (1), then the Luau version its format follows. A number constant is the raw Q48.16 value, 8
>   bytes little-endian. The VM refuses Luau bytecode (`this is Luau bytecode (version N), not moo bytecode`), an
>   unknown moo version, an integer constant (`moo bytecode has no integer constant`) and `INT64_MIN`, each by name.
>   The bytecode dump prints numbers through `moo_format.h`.

DESIGN.md, "Layer 3 as built", **The compiler**: "(a constant still travels as the double that holds it exactly)"
becomes "(the AST still holds the double that holds a constant exactly; the builder converts it once to the raw
value by the boundary rule)".

DESIGN.md, "Layer 3 as built", **Literals are exact**: "Until the bytecode version PR, the AST and the bytecode carry
the double that holds the value, which is exact below 2^37." becomes "Until the AST raw-literals PR, the AST carries
the double that holds the value, which is exact below 2^37; the bytecode carries the raw value."

DESIGN.md, "Literals", last bullet: "The compiler stores number constants as the raw int64 in a moo bytecode version;
the VM refuses Luau bytecode by version, by name." stays and gains "(built: Bytecode.h, "moo bytecode versions")".

DETERMINISM.md 19, **Fix**: append "Built in layer 3 (`moo-l3-bytecode-version`): moo bytecode version 1;
`VM/src/lvmload.cpp` reads the raw value and refuses Luau bytecode by name."

## Comments outside PATHS that now describe the old state

`Ast/src/Parser.cpp:4005`, `Compiler/src/ConstantFolding.cpp:26` and `tests/MooHost.h` (the `moo_test.load` comment:
"bytecode number constants are rounded at load") still describe the bridge. Their own cards, or moo-l3-conformance,
should update them.

## Not done: test cases outside PATHS that break (PATHS-PROPOSED)

Each was green at BASE `1fcc79e5`. Each breaks only because of this card's required change, and each needs an edit
outside PATHS:

- `tests/BytecodeCallInliner.test.cpp:176` and `tests/BytecodeCompiler.test.cpp:99` skip the header with
  `size_t offset = 2; // skip versions`. A moo header is 3 bytes, so this must be `offset = 3` (moo). Without that
  change the string table is misread, and `graph_builds_loop_exit_phi_for_downstream_use` crashes Luau.UnitTest with
  SIGSEGV. With it (probed on hulk, not committed), every other failure is one of those listed below.
- `tests/BytecodeCallInliner.test.cpp:1092` `fold_constants_div_by_zero`: Sccp folds `10 / 0` to the double infinity,
  which now saturates to `140737488355327.99998`. Owner: `moo-l3-bytecode-graph-fixed`. Folding by zero should be
  refused, per DESIGN.md "Division by zero".
- `tests/MooCompiler.test.cpp:44`, `:48`, `:76`: the dump strings `0.666656494140625`, `0.3000030517578125` and
  `0.100006103515625` now print as `0.66666`, `0.3` and `0.1`.
- `tests/Sccp.test.cpp` `sccp_nan_compare_check1/2/3`: they build a NaN constant, but a number has no NaN. Owner:
  `moo-l3-bytecode-graph-fixed`.
- `tests/Conformance.test.cpp:4659` `NativeIntegerSpills`: `native_integer_spills.luau` compiles integer literals to
  `LBC_CONSTANT_INTEGER`, which the loader now refuses by name. Owner: `moo-l3-ast-raw-literals`. The other option is
  a moo skip of that case.

`Bytecode/src/Sccp.cpp` is outside PATHS too. The change converts at the two calls only (`evaluate` stores through
`mooNumberFromDouble`; `asNumber` returns `moo_numbridgetodouble`), and both are marked TEMPORARY until
`moo-l3-bytecode-graph-fixed`. Without it, the implicit int64/double conversions would compile with no warning and
silently fold on the wrong scale.
