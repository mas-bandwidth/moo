# moo-l3-pack-floats: string.pack/unpack f, d and n by the exact boundary rule

Files changed: `VM/src/lstrlib.cpp`; new `tests/moo/moo-l3-pack-floats.test.cpp`,
`tests/conformance/moo/l3/moo-l3-pack-floats.luau`.

## Decision: format `n` is the raw Q48.16 int64

Upstream Lua's `n` is `lua_Number`, a double. In moo `lua_Number` is the raw Q48.16
`int64_t`, so `string.pack`/`unpack` `n` stores the raw `int64_t` unchanged. It is an
exact round trip of the number, with no float or double arithmetic. This is the decision
`docs/DESIGN.md` "Boundary" must record.

## What changed

- `VM/src/lstrlib.cpp`:
  - `getoption`: formats `f`, `d`, `n` now set `Header.fltype` (0, 1, 2) so the Kfloat
    case can tell them apart (all have `Kfloat`; `d` and `n` are both 8 bytes).
  - `str_pack` Kfloat: `f` packs `moo_numtofloatbits`, `d` packs `moo_numtodoublebits`,
    `n` packs the raw int64; all stored as integer bytes with `copywithendian`.
  - `str_unpack` Kfloat: `f` unpacks with `moo_numfromfloat`, `d` with
    `moo_numfromdouble`, `n` reads the raw int64. A NaN, infinity or out-of-range value
    raises `string.unpack float is NaN` / `string.unpack float is infinite` /
    `string.unpack float out of number range`.
  - The TEMPORARY bridge calls (`moo_numbridgetodouble` / `moo_numbridgefromdouble`) and
    comments are gone.

## DESIGN.md "Layer 3 as built" sentences that must change

The bullet that lists the TEMPORARY bridge for `string.pack` floats must drop the
string.pack/unpack part:

> - `buffer.readf32`/`writef32`, `readf64`/`writef64` and `string.pack` floats round by the bridge (the boundary PR);

becomes (buffer floats are another card's note):

> - `buffer.readf32`/`writef32`, `readf64`/`writef64` round by the bridge (the boundary PR);

## DESIGN.md "Boundary" sentences that must change

`string.pack`/`unpack` is already mentioned as decoding/encoding by the same rule; add the
`n` decision (format `n` is the raw Q48.16 int64 and round-trips exactly).

## DETERMINISM.md 18 sentences that must change

The item says `string.pack`/`unpack` formats `f`, `d`, `n` convert through the bridge and
are "Fix:"-marked. It becomes fixed: the conversions use `moo_numtofloatbits` /
`moo_numfromfloat` / `moo_numtodoublebits` / `moo_numfromdouble`, no FPU, and `n` is the
raw int64.

## Tests that hold it

- `tests/conformance/moo/l3/moo-l3-pack-floats.luau` (run by `MooL3Scripts`): f, d and n
  round trips under both endians, the f tie-to-even byte pin, and the three named unpack errors.
- `tests/moo/moo-l3-pack-floats.test.cpp`: `string.pack('<d', x)` byte-equals
  `moo_numtodoublebits(x)` for 1024 seeded values. The card says this check compares against
  `buffer.writef64`'s bytes once moo-l3-buffer-floats lands; it has not landed, so the
  fallback (against `moo_numtodoublebits` directly) ran. Also pins the f tie and the n
  exact round trip.
