# moo-l3-integer-args-fastcalls

Every bytecode fastcall in `VM/src/lbuiltins.cpp` that converts a number argument to an
integer now checks `luai_numisint` first and returns `-1` when the number has fraction bits,
so the VM falls through to the C library function (`luaL_checkinteger`, `luaL_optinteger`,
or `luaL_checkunsigned`), which raises the named error `number has no integer representation`.
Direct calls at `-O0` (no fastcall) and at `-O1` / `-O2` (with `LOP_FASTCALL*` in a `safeenv`
environment) now produce identical results and identical error messages.

## Decisions

1. **`luai_numisint` seam in `VM/src/lnumutils.h`:** defined `#define luai_numisint(d) moo_numisinteger(d)`
   beside `luai_num2int`, so every fastcall in `VM/src/lbuiltins.cpp` queries integer-valuedness
   through the same header that provides `luai_num2int` and `luai_num2unsigned`.
2. **Return `-1` on any fractional integer argument:** fastcalls cannot raise Lua errors directly;
   returning `-1` instructs `LOP_FASTCALL`, `LOP_FASTCALL1`, `LOP_FASTCALL2`, `LOP_FASTCALL2K`,
   and `LOP_FASTCALL3` in `VM/src/lvmexecute.cpp` to run the fallback `LOP_CALL` to the C library
   function, which raises `invalid argument #k to 'f' (number has no integer representation)`.
3. **`math.ldexp` fastcall (`luauF_ldexp`):** previously converted its second argument with
   `int(moo_numbridgetodouble(nvalue(args)))`, silently truncating fractional exponents while
   `math_ldexp` in `VM/src/lmathlib.cpp` called `luaL_checkinteger(L, 2)`. `luauF_ldexp` now
   requires `luai_numisint(nvalue(args))` and converts the exponent with `luai_num2int`.
4. **64-bit integer fastcalls kept consistent (`luauF_integercreate`, `luauF_bufferreadlong`,
   `luauF_bufferwritelong`):** `luauF_integercreate` uses `luai_numisint(a1)` (returning `nil` on
   fractions, matching `integer.create`), and `luauF_bufferreadlong` / `luauF_bufferwritelong`
   check `luai_numisint(nvalue(args))` on the buffer offset.
5. **Follow-up noted outside allowed `PATHS` (`Compiler/src/Compiler.cpp:1448`):** when
   `bit32.extract(n, f, w)` is compiled at `-O1` / `-O2` with compile-time constant numbers for
   `f` and `w`, `Compiler.cpp` truncates `int(fc.valueNumber)` and `int(wc.valueNumber)` at
   compile time to pack `LOP_FASTCALL2K` (`LBF_BIT32_EXTRACTK`). Non-constant `f` or `w` emit
   `LOP_FASTCALL` / `LOP_FASTCALL2` / `LOP_FASTCALL3` (`LBF_BIT32_EXTRACT`) and are refused by
   `luauF_extract`, and fractional `n` with constant `f, w` is refused by `luauF_extractk`, but
   the compile-time constant folding in `Compiler/src/Compiler.cpp` should require integer-valued
   constants before emitting `LBF_BIT32_EXTRACTK`.

## What changed

- `VM/src/lnumutils.h`:
  - Added `#define luai_numisint(d) moo_numisinteger(d)` beside `luai_num2int`.
- `VM/src/lbuiltins.cpp`:
  - `luauF_ldexp`: check `luai_numisint(nvalue(args))` and convert exponent via `luai_num2int`.
  - `luauF_arshift`, `luauF_lrotate`, `luauF_lshift`, `luauF_rrotate`, `luauF_rshift`: check
    `luai_numisint(a1) && luai_numisint(a2)` before `luai_num2unsigned` / `moo_numtrunc`.
  - `luauF_band`, `luauF_bor`, `luauF_bxor`, `luauF_btest`: check `luai_numisint(a1) && luai_numisint(a2)`
    and check `luai_numisint(a)` for each variadic argument `i >= 3`.
  - `luauF_bnot`, `luauF_countlz`, `luauF_countrz`, `luauF_byteswap`: check `luai_numisint(a1)`.
  - `luauF_extract`: check `luai_numisint(a1) && luai_numisint(a2)` and, when `nparams >= 3`,
    `luai_numisint(a3)`.
  - `luauF_extractk`: check `luai_numisint(a1) && luai_numisint(a2)`.
  - `luauF_replace`: check `luai_numisint(a1) && luai_numisint(a2) && luai_numisint(a3)` and,
    when `nparams >= 4`, `luai_numisint(a4)`.
  - `luauF_byte`: check `luai_numisint(nvalue(args))` and, when `nparams >= 3`,
    `ttisnumber(args + 1) && luai_numisint(nvalue(args + 1))` (returning `-1` on non-number or
    non-integer 3rd argument instead of substituting `0`).
  - `luauF_char`: check `luai_numisint(nvalue(arg0))` and `luai_numisint(nvalue(args + (i - 2)))`
    for each subsequent argument.
  - `luauF_sub`: check `luai_numisint(nvalue(args)) && luai_numisint(nvalue(args + 1))`.
  - `luauF_tunpack`: check `luai_numisint(nvalue(args + 1))` when `nparams == 3`.
  - `luauF_select`: check `luai_numisint(nvalue(arg0))` when indexing varargs with a number.
  - `luauF_readinteger`, `luauF_readfp`, `luauF_writefp`, `luauF_bufferreadlong`,
    `luauF_bufferwritelong`: check `luai_numisint(nvalue(args))` on the byte offset.
  - `luauF_writeinteger`: check `luai_numisint(nvalue(args)) && luai_numisint(nvalue(args + 1))`
    on both the byte offset and the integer value.
  - `luauF_integercreate`: use `luai_numisint(a1)` for the exact-integer check.
- `tests/moo/moo-l3-integer-args-fastcalls.test.cpp`:
  - `FastcallIntegerArgsMatchAcrossOptimizationLevels`: compiles and runs 67 direct builtin calls
    at `-O0`, `-O1`, and `-O2` in a sandboxed (`safeenv`) VM, verifying that `1.5` raises the
    exact same `number has no integer representation` error at every optimization level and `2`
    produces the exact same result.
  - `FastcallIntegerArgsScriptInSandbox`: runs `l3/moo-l3-integer-args-fastcalls.luau` in a
    sandboxed (`safeenv`) VM so `LOP_FASTCALL*` instructions execute directly.
- `tests/conformance/moo/l3/moo-l3-integer-args-fastcalls.luau`:
  - Conformance script exercising direct fastcall-eligible calls to `bit32.*`, `string.byte`,
    `string.char`, `string.sub`, `buffer.*`, `math.ldexp`, `table.unpack`, `unpack`, and `select`
    with `1.5` and `2`.

## DESIGN.md sentences that must change

- `docs/DESIGN.md`, section **"Layer 3 as built"**, after the "Exact already" bullet, add (or
  update the `moo-l3-integer-args` bullet so the fastcall caveat is removed):

  > - **Functions that need an integer** (`lua_tointegerx`, `lua_tounsignedx`,
  >   `luaL_checkinteger`, `luaL_optinteger`, `luaL_checkunsigned`, and every integer-taking
  >   bytecode fastcall in `VM/src/lbuiltins.cpp` via `luai_numisint`) accept only
  >   integer-valued numbers. A fraction raises `number has no integer representation`, as in
  >   Lua 5.3, identically at `-O0`, `-O1`, and `-O2`.

- `docs/DESIGN.md`, section **"Exact integers"**, bullet **"- Functions that need an integer"**:
  replace:

  > - **Functions that need an integer** (`string.byte`, `string.char`, `string.sub`,
  >   `string.rep`, `table.insert` positions, `bit32`, `buffer` offsets, `string.format("%d")`)
  >   accept only integer-valued numbers. A non-integer argument is the named error
  >   `number has no integer representation`, as in Lua 5.3, instead of Luau's silent truncation.

  with:

  > - **Functions that need an integer** (`string.byte`, `string.char`, `string.sub`,
  >   `string.rep`, `table.insert`/`remove`/`move`/`create` positions, `table.unpack`/`unpack`
  >   bounds, `select` index, `math.ldexp` exponent, `bit32`, `buffer` offsets and integer
  >   values, `string.format("%d")` and `string.pack` integer formats — both in the C library
  >   and in every `VM/src/lbuiltins.cpp` fastcall) accept only integer-valued numbers. A
  >   non-integer argument is the named error `number has no integer representation`, as in
  >   Lua 5.3, instead of Luau's silent truncation.

## DETERMINISM.md sentences that must change

- `docs/DETERMINISM.md`, section **"Status"**, row `11`:
  replace:

  > | 11 | out-of-range float-to-int casts | **fixed**: every number-to-integer conversion is toward zero and saturating (`moo_number.h`) |

  with:

  > | 11 | out-of-range float-to-int casts | **fixed**: every number-to-integer conversion requires an integer-valued number (`moo_numisinteger` / `luai_numisint`, raising `number has no integer representation` in library calls and falling back in fastcalls) and saturates at the C integer range (`moo_number.h`) |

- `docs/DETERMINISM.md`, section **"11. Out-of-range float-to-integer casts"**, paragraph
  **"Fix:"**:
  replace:

  > **Fix:** with fixed numbers every conversion is integer arithmetic with a defined result
  > (saturate, or a named error). Until then this is a real cross-platform divergence for
  > `bit32` and `string.format` on large values.

  with:

  > **Fix:** with fixed numbers every conversion is integer arithmetic with a defined result:
  > non-integer numbers are refused by `lua_tointegerx` / `lua_tounsignedx` and by `luai_numisint`
  > in every `VM/src/lbuiltins.cpp` fastcall (raising `number has no integer representation`),
  > and integer-valued numbers outside the 32-bit C range saturate (`luai_num2int`) or reduce
  > modulo 2^32 (`luai_num2unsigned`).
