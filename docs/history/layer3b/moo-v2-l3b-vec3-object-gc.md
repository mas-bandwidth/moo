# moo-v2-l3b-vec3-object-gc

Layer 3b's `MooVec3` GC object implementation (`docs/DESIGN.md` "vec3, quat and transform (layer 3b)",
`docs/layer3b/moo-v2-l3b-vec3-object.md`, `VM/src/moo_vec3.h`): `TValue` and `GCObject` wiring, allocation via
`luaM_newgcofixed` and `luaC_init`, value equality (`moo_vec3_equal`) in `luaO_rawequalObj` and `luaO_rawequalKey`,
exact formatting (`moo_vec3_tostring` via `moo_numtostr`), leaf GC traversal/freeing and weak-table preservation
in `VM/src/lgc.cpp`, and 40-byte size pins in `VM/src/lmem.cpp` and `VM/src/moo_layout.cpp`.

## Decision

No new design decision is made on this card. It implements the decisions fixed in
`docs/layer3b/moo-v2-l3b-vec3-object.md` (decisions 2, 3, 4, 5, 6 and 7) and declared in `VM/src/moo_vec3.h`,
within the files named by this card's `PATHS` (`VM/src/lobject.h`, `VM/src/lobject.cpp`, `VM/src/lstate.h`,
`VM/src/lgc.cpp`, `VM/src/lmem.cpp`, `VM/src/moo_layout.cpp`) and `NEW`:

- Because `VM/src/moo_vec3.h` and `docs/layer3b/moo-v2-l3b-vec3-object.md` are read-only on this card and the brief
  instructs us to wire the object into `lobject.h`'s `TValue` macros and `lstate.h`'s `GCObject` union,
  `ttisvec3`, `vec3value` and `setvec3value` are defined in `VM/src/lobject.h` beside `ttisobject`, `objectvalue`
  and `setobjectvalue`, and `gco2vec3` is defined in `VM/src/lstate.h` beside `gco2vec`.
- In `VM/src/lgc.cpp`, `isobjcleared` marks a `LUA_TVEC3` object with `stringmark(&o->gch)` (which clears the white
  bits and sets `BLACKBIT`) and returns `0`, matching `LUA_TSTRING`, so a `vec3` is never cleared from a weak table.
  In `cleartable`, a nil-valued node calls `removeentry(n)` so a collectable key in an emptied weak-table slot turns
  into `LUA_TDEADKEY` before sweep frees its object.

## What changed

- `VM/src/lobject.h`:
  - `ttisvec3(o)`: `(ttype(o) == LUA_TVEC3)`.
  - `vec3value(o)`: `check_exp(ttisvec3(o), &(o)->value.gc->v3)`.
  - `setvec3value(L, obj, x)`: sets `value.gc`, `tt = LUA_TVEC3`, and checks `checkliveness((L)->global, i_o)`.
- `VM/src/lstate.h`:
  - Includes `moo_vec3.h`.
  - `union GCObject`: adds `struct MooVec3 v3;`.
  - `gco2vec3(o)`: `check_exp((o)->gch.tt == LUA_TVEC3, &((o)->v3))`.
- `VM/src/lobject.cpp`:
  - Includes `lmem.h`, `moo_format.h` and `moo_vec3.h`.
  - `moo_vec3_new(lua_State* L, fixed_t x, fixed_t y, fixed_t z)`: allocates a 40-byte `MooVec3` via
    `luaM_newgcofixed(L, MooVec3, sizeof(MooVec3), L->activememcat, LUA_TVEC3)`, initializes its GC header and
    creation number via `luaC_init(L, v, LUA_TVEC3)`, and writes `x`, `y`, `z`.
  - `moo_vec3_equal(const MooVec3* a, const MooVec3* b)`: returns `a->x == b->x && a->y == b->y && a->z == b->z`.
  - `moo_vec3_tostring(char* buf, const MooVec3* v)`: writes `vec3(x, y, z)` into `buf` using `moo_numtostr` for
    each component and returns the end pointer (not NUL-terminated).
  - `luaO_rawequalObj` and `luaO_rawequalKey`: add `case LUA_TVEC3:` returning
    `moo_vec3_equal(vec3value(t1), vec3value(t2))`.
- `VM/src/lgc.cpp`:
  - `reallymarkobject`: adds `case LUA_TVEC3:` turning the object black directly (`gray2black(o); return;`).
  - `isobjcleared`: marks `LUA_TVEC3` via `stringmark(&o->gch)` and returns `0`.
  - `cleartable`: calls `removeentry(n)` on nil-valued nodes so emptied entries in weak tables turn collectable keys
    into `LUA_TDEADKEY` before sweep frees their objects.
  - `freeobj`: adds `case LUA_TVEC3:` calling
    `luaM_freegcofixed(L, gco2vec3(o), sizeof(MooVec3), gco2vec3(o)->memcat, page)`.
- `VM/src/lmem.cpp`:
  - Adds `static_assert(sizeof(MooVec3) == 40, "size mismatch for vec3 object");`.
- `VM/src/moo_layout.cpp`:
  - Adds `static_assert(sizeof(MooVec3) == 40, "vec3 object layout changed");`.
- `tests/moo/moo-v2-l3b-vec3-object-gc.test.cpp` (new, doctest suite `moo-v2-l3b-vec3-object-gc`):
  - `Vec3CreateAndLayoutInVm`: pins `moo_vec3_new`, `setvec3value`, `ttisvec3`, `vec3value`, `gco2vec3`,
    `lua_type(L, -1) == LUA_TVEC3`, `type(v) == "vec3"`, creation number increment (+1 per vec3), and `totalbytes`
    increment (+40 per vec3).
  - `Vec3EqualByValue`: pins `moo_vec3_equal`, `luaO_rawequalObj`, `luaO_rawequalKey`, `lua_rawequal`, and script
    `rawequal` on distinct equal vec3 objects and single-raw-unit component differences.
  - `Vec3TostringFormat`: pins `moo_vec3_tostring` on `vec3(1, -2.5, 0.1)`, `vec3(0, 0, 0)`, single raw units, and
    range extremes within `MOO_VEC3_TOSTR_MAX` with no `#gcseq` or address.
  - `Vec3GcLeafAndChurn100k`: pins full-GC collection of unreachable vec3s via `luaM_visitgco` and `totalbytes`,
    a destructor-free churn of $10^5$ (`100000`) `MooVec3` objects under incremental GC, weak-table preservation of
    `vec3` keys and values across `"k"`, `"v"` and `"kv"` tables, and `LUA_TDEADKEY` clearing of emptied weak-table
    entries in `cleartable`.

## DESIGN.md sentences that must change

1. "Representations costed", (a): "and 48 bytes per stored vec3 (16-byte slot plus a 32-byte object). Sizes are 32
   bytes for vec3, 40 for quat and 64 for transform with the GC header." becomes "and 56 bytes per stored vec3
   (16-byte slot plus a 40-byte object). Sizes are 40 bytes for vec3, 48 for quat and 72 for transform with moo's
   16-byte GC header (the creation number, DETERMINISM.md 1)."
2. "Layer 3 as built" gains a bullet: "**vec3 is a GC object.** `LUA_TVEC3` (14) follows `LUA_TOBJECT`; a vec3 is
   `MooVec3` (`VM/src/moo_vec3.h`), the 16-byte header and three `fixed_t`, 40 bytes, immutable. `==`, `rawequal`
   and table keys compare and hash the three raw components; `tostring` prints `vec3(x, y, z)` with the exact
   formatter; the GC treats it as a leaf and never clears it from a weak table. `LUA_TVECTOR` stays unused."
3. "The rest of determinism", **Object keys**: after "never by address." add "A vec3 is a value: it hashes by its
   three raw components."
4. "The rest of determinism", **Output**: after "never an address." add "A vec3 prints its components,
   `vec3(x, y, z)`."

## DETERMINISM.md sentences that must change

1. Status table, row 1: "object keys hash by it" becomes "object keys hash by it, except a vec3, which is a value
   and hashes by its three raw components (layer 3b)".
2. "The creation number costs 8 bytes in every object (...): a table header is 56 bytes (was 48), ..., a buffer
   header 16 (8)." gains ", a vec3 40 (32)".
3. Inventory 1, **Fix**: after "Object keys hash by that number (with the existing MurmurHash3 finalizer), never by
   address." add "A vec3 is the exception: it is a value, equal vec3 are one key, and it hashes by its three raw
   components (`moo_vec3_hash`)."
4. Inventory 6, **Fix**: after "Light userdata prints `userdata: light` with no value." add "A vec3 prints
   `vec3(x, y, z)` through the exact formatter."
5. Inventory 13: after "Weak tables and destructors are kept" paragraph's last sentence add "A vec3, like a string,
   is a value and is never cleared from a weak table."
6. Inventory 17: "`LUA_TVECTOR` stays unused in the tag enum for upstream merges." gains "; vec3, quat and transform
   get new tags after `LUA_TOBJECT` (`docs/layer3b/moo-v2-l3b-vec3-object.md`)".

## Not done

- Table-key hashing (`moo_vec3_hash`, `mainposition`, `mainpositiontagged<LUA_TVEC3>` and
  `rawequalkeyvalue<LUA_TVEC3>` in `VM/src/ltable.cpp`) belongs to card `moo-v2-l3b-vec3-object-key`.
- `typeof`, C API entry points, script constructors and arithmetic belong to card `moo-v2-l3b-vec3-ops`.
- Seams in files outside this card's `PATHS` (`VM/src/lvmutils.cpp` `luaV_equalval`, `VM/src/lvmexecute.cpp`
  `LOP_JUMPIFEQ`/`LOP_JUMPIFNOTEQ`, `VM/src/lgcdebug.cpp` `validateobj`/`dumpobj`/`enumobj`, `VM/src/laux.cpp`
  `luaL_tolstring`, `VM/src/lapi.cpp` `lua_topointer`, `tests/Determinism.test.cpp` `hashObject`, and row-description
  updates in `docs/PATCHES.md`) are left untouched here and fold in on their respective cards or the closing card
  (`moo-v2-l3b-conformance`).
