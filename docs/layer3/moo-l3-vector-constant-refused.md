# moo-l3-vector-constant-refused: the bytecode has no vector constant

Card `moo-l3-vector-constant-refused`. It builds the "bytecode constant" refusal of DESIGN.md "vec3, quat and
transform" (the library and the type are separate cards, `moo-l3-vector-library-out` and the analyzer/compiler work).
Before this card, the builder could write `LBC_CONSTANT_VECTOR` or `LBC_CONSTANT_VECTORD` and the loader accepted them.
Now the builder never writes them, and the VM refuses a chunk holding either tag by name.

## Decision: the loader refuses the two vector constant tags by name

`LBC_CONSTANT_VECTOR` (7) and `LBC_CONSTANT_VECTORD` (11) are refused with `moo bytecode has no vector constant`,
before the payload is read. The refusal is in the same place as the integer constant refusal (`lvmload.cpp`), with the
same message shape. `Bytecode.h` keeps both enum values and their tag numbers, because upstream still uses them (a merge
is mechanical, and the numbers are pinned by upstream code).

## Decision: the builder never writes a vector constant

In `BytecodeBuilder.cpp`, the `Constant::Type_Vectorf` and `Constant::Type_Vectord` serialization cases are removed, so
such a constant falls through to the default (`LUAU_ASSERT(!"Unsupported constant type")`). The tags `LBC_CONSTANT_VECTOR`
and `LBC_CONSTANT_VECTORD` no longer appear in the builder. The `addConstantVectorf`/`addConstantVectord` entry points
stay, because the compiler and the bytecode graph still call them until `moo-l3-vector-compiler-out` lands; the two now
dead `writeFloat`/`writeDouble` helpers are removed.

## What changed, file and function

| file | function | change |
|---|---|---|
| `Bytecode/src/BytecodeBuilder.cpp` | `writeConstants` | removes the `Type_Vectorf` and `Type_Vectord` cases, so no vector tag is ever written; removes the now-unused `writeFloat`/`writeDouble` |
| `VM/src/lvmload.cpp` | `loadsafe` | `LBC_CONSTANT_VECTOR` and `LBC_CONSTANT_VECTORD` are refused by name, `moo bytecode has no vector constant`, before the payload is read |

## Tests

`tests/moo/moo-l3-vector-constant-refused.test.cpp` (Moo.Test):

- `MooBytecodeRefusesVectorConstantF`: a hand-built moo chunk with `LBC_CONSTANT_VECTOR` is refused by name.
- `MooBytecodeRefusesVectorConstantD`: a hand-built moo chunk with `LBC_CONSTANT_VECTORD` is refused by name.
- `MooBytecodeVectorRefusalIsSpecific`: the same chunk with an ordinary `LBC_CONSTANT_NUMBER` constant loads, so the
  refusal is for the vector tags alone.

`tests/conformance/moo/l3/moo-l3-vector-constant-refused.luau` (MooL3Scripts) hand-builds the same two chunks with
`string.pack`-style bytes and checks `moo_test.load` returns nil with the named error for each.

## The sentences of DESIGN.md and DETERMINISM.md that must change (folded in by moo-l3-conformance)

DESIGN.md, "vec3, quat and transform" (first paragraph): "moo removes it in layer 3 (the library, the type and the
bytecode constant, each refused by name)" stays and gains "(built: the bytecode constant here in `BytecodeBuilder.cpp`
and `lvmload.cpp`; the library and the type in `moo-l3-vector-library-out` and the compiler card)".

DESIGN.md, "Layer 3 as built", **TEMPORARY bridges**: the item that reads "the libm functions of `math`, `^`,
`math.noise`, and the float `vector` type convert through doubles (`moo_numbridgetodouble`,
`moo_numbridgefromdouble`), until the math library is rebuilt on fixed and the vector type is removed" stays until the
vector type is removed; the bytecode-constant part of the vector removal is already built by this card.

DETERMINISM.md 17, **Fix**: "in layer 3 the float vector type is removed" stays and gains "(built in layer 3: the
bytecode constant is refused by name, `moo bytecode has no vector constant`, and the builder never writes it)".
