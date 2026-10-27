# moo-l3-integer-args

A function that needs an integer now accepts only an integer-valued number: a value whose 16
fraction bits are zero. A fraction is the named error `number has no integer representation`,
as in Lua 5.3, instead of Luau's silent truncation. A number equal to an integer (`2.0`) still
works.

## Decision

The rule lives at the conversion seam, not in each library function: `lua_tointegerx` and
`lua_tounsignedx` (`VM/src/lapi.cpp`) set `*isnum` to 0 when the number has fraction bits, and
`luaL_checkinteger`, `luaL_optinteger` and `luaL_checkunsigned` (`VM/src/laux.cpp`) turn that
into the named argument error. This is Lua 5.3's rule and it extends the existing seam, so the
functions that already used `luaL_check*` needed no change. `string.format`'s integer
conversions and `string.pack`'s integer formats, which had truncated themselves with
`moo_numtrunc`, now validate through the same `luaL_checkinteger` / `luaL_checkunsigned`
first, then keep the full 64-bit value for formatting and packing (the check returns `int`).

## What changed

- `VM/src/lapi.cpp`, `lua_tointegerx` / `lua_tounsignedx`: a number for which
  `moo_numisinteger` is false reports `*isnum = 0` and returns 0, so it is not a conversion.
  Type errors are unchanged: the caller still distinguishes a non-number.
- `VM/src/laux.cpp`, `luaL_checkinteger` / `luaL_checkunsigned`: when the value is a number
  (or a numeric string) but not integer-valued, raise
  `invalid argument #n to 'f' (number has no integer representation)`; a non-number is still
  the type error. `luaL_optinteger` inherits the rule through `luaL_checkinteger`.
- `VM/src/lstrlib.cpp`, `string.format`'s `%c`, `%d`, `%i`, `%o`, `%u`, `%x`, `%X` and
  `string.pack`'s `Kint` / `Kuint`: validate with `luaL_checkinteger` / `luaL_checkunsigned`
  instead of truncating a `luaL_checknumber` result.
- `VM/src/lbuflib.cpp`, `buffer.readbits` / `buffer.writebits`: the bit offset went through
  `luaL_checknumber` and `moo_numtrunc`; it now validates with `luaL_checkinteger` first.
  Every other buffer offset already used `luaL_checkinteger`.
- Tests: `tests/moo/moo-l3-integer-args.test.cpp`
  (`IntegerArgsRefuseFraction`, `IntegerArgsAcceptInteger`,
  `IntegerConversionReportsFraction`) and
  `tests/conformance/moo/l3/moo-l3-integer-args.luau`. Each covers every function DESIGN.md
  "Exact integers" names, once with `1.5` and once with `2`.

## Functions that reach the rule

- `string.byte`, `string.char`, `string.sub`, `string.rep` — `luaL_checkinteger` /
  `luaL_optinteger` (`VM/src/lstrlib.cpp`).
- `table.insert`, `table.remove`, `table.move`, `table.create` positions —
  `luaL_checkinteger` / `luaL_optinteger` (`VM/src/ltablib.cpp`).
- `bit32` arguments — `luaL_checkunsigned` / `luaL_checkinteger` (`VM/src/lbitlib.cpp`).
- `buffer` offsets — `luaL_checkinteger` (`VM/src/lbuflib.cpp`), with `readbits` /
  `writebits` fixed here.
- `string.format("%d")` and the other integer conversions, `string.pack` integer formats —
  fixed here.

Not covered: the bytecode fastcalls in `VM/src/lbuiltins.cpp` (`luauF_byte`, `luauF_char`,
`luauF_sub`, `luauF_tinsert`, `luauF_band`/`bor`/`bxor`/`bnot`/`lshift`/`rshift`/`arshift`/
`lrotate`/`rrotate`/`extract`/`replace`/`btest`, `luauF_countlz`/`countrz`/`byteswap`) still
convert with `luai_num2unsigned` and `moo_numtrunc`; they are card
`moo-l3-integer-args-fastcalls`. The tests here call through local aliases so they exercise the
library function, not the fastcall.

## Existing conformance scripts

`tests/conformance/moo/strings.luau`, `bitwise.luau` and `buffers.luau` were searched for a
call that relied on truncation of a fractional argument to one of these functions; none was
found, so no line of those three scripts changed. Their fractional arguments are to
`string.format`'s `%f` / `%e` conversions and to the float `buffer` / `string.pack` formats,
which are not integer conversions.

## DESIGN.md sentences that must change

- DESIGN.md, "Layer 3 as built", after the "Exact already" bullet: the list has no bullet for
  this rule. Add:

  > - **Functions that need an integer** (`lua_tointegerx`, `lua_tounsignedx`,
  >   `luaL_checkinteger`, `luaL_optinteger`, `luaL_checkunsigned`; `string.byte`, `string.char`,
  >   `string.sub`, `string.rep`, `table.insert`/`remove`/`move`/`create` positions, `bit32`,
  >   `buffer` offsets, `string.format`'s `%c`/`%d`/`%i`/`%o`/`%u`/`%x`/`%X` and `string.pack`'s
  >   integer formats) accept only integer-valued numbers. A fraction raises
  >   `number has no integer representation`, as in Lua 5.3. The fastcalls in `lbuiltins.cpp`
  >   still truncate until the fastcall card.

- DESIGN.md, "Exact integers", bullet "**Functions that need an integer**": its sentence already
  states the rule and needs no change. Its parenthetical could name `string.pack` alongside
  `string.format("%d")`, and add `table.remove`/`move`/`create` beside `table.insert` positions.

## DETERMINISM.md sentences that must change

- DETERMINISM.md, "11. Out-of-range float-to-integer casts", "Fix": the sentence

  > with fixed numbers every conversion is integer arithmetic with a defined result
  > (saturate, or a named error).

  is now fully true; no change is required. The preceding "Until then this is a real
  cross-platform divergence ..." sentence is historical. Add, after the fix sentence:

  > `lua_tointegerx` and `lua_tounsignedx` refuse a number with fraction bits, and
  > `luaL_checkinteger` / `luaL_checkunsigned` raise `number has no integer representation`;
  > the conversion still saturates for an integer-valued number outside the C range.

- DETERMINISM.md, "Status": no sentence changes; this card adds no source of non-determinism.
