# moo-v2-l3b-quat-object

Layer 3b's `MooQuat` GC object implementation, the vec3 pattern again (`docs/DESIGN.md` "vec3, quat and transform
(layer 3b)", `docs/layer3b/moo-v2-l3b-vec3-object.md`): tag, `TValue` and `GCObject` wiring, allocation, value
equality, table-key hashing, exact formatting, leaf GC, and the size pins. No operation is added: `quat * quat`,
`quat * vec3`, `conjugate` and `nlerp` are card `moo-v2-l3b-quat`.

## Decision

No new design decision. It copies `docs/layer3b/moo-v2-l3b-vec3-object.md` decisions 1 to 7 for quat, as that note
directs ("The quat and transform cards copy the decisions"), and adds the tag and its one re-pin, which the note
assigns to this card (decision 1, item 3: "the quat and transform cards each add their own tag and re-pin once more
(the `TKey` widening of item 4 goes with quat)"). The payload is the `docs/DESIGN.md` table's `fixQuat { v: fixVec3,
s }`, 32 bytes.

- `LUA_TQUAT` follows `LUA_TVEC3`: obj 13, vec3 14, quat 15. `LUA_TDEADKEY` is 16 and `LUA_T_COUNT` 16.
- `TKey::tt` widens from 4 to 5 bits and `next` narrows from 28 to 27 signed bits, as the vec3 note requires. The
  `static_assert`s in `VM/src/ltable.cpp` hold `MAXSIZE - 1` in 27 signed bits, and `sizeof(TKey)` does not change.
- `MooQuat` is the 16-byte `CommonHeader`, then fixed's `fixQuat`: `v` at offset 16 (x 16, y 24, z 32) and `s` at 40,
  48 bytes. `totalbytes` gains 48 per quat and each quat takes one creation number.
- Equality is the four raw components (`moo_quat_equal`), never address or creation number, for `==`, `rawequal` and
  table keys.
- A table key hashes by the four raw components: each through hashint's MurmurHash64B finalizer, combined with
  hashvec's primes (`73856093`, `19349663`, `83492791`, and `39916801` for the scalar, hashvec's fourth-component
  prime), so equal quat are one key. Deterministic by construction; Q48.16 has no NaN, so no NaN refusal.
- `tostring` prints `quat(x, y, z, s)`, each component through `moo_numtostr`; `MOO_QUAT_TOSTR_MAX` is 148. It never
  prints a creation number or an address.
- The GC treats a quat as a leaf: `reallymarkobject` turns it black directly, `freeobj` frees it with
  `luaM_freegcofixed`, and `isobjcleared` marks it like a string so a weak table never clears it. No new GC state, so
  no TLA+ model is owed.
- Immutable: `moo_quat_new` writes the four components once, right after `luaC_init`; nothing writes them again.

## What changed

- `VM/include/lua.h`, `enum lua_Type`: `LUA_TQUAT` after `LUA_TVEC3` (`moo:` comment).
- `VM/src/ltm.cpp`, `luaT_typenames`: `"quat"` after `"vec3"` (`moo:` comment). This is the one file outside the
  card's `PATHS`; without its entry the `static_assert` that holds the array to `LUA_T_COUNT` fails, so the tag
  cannot be added without it.
- `VM/src/lobject.h`: `ttisquat`, `quatvalue`, `setquatvalue` beside the vec3 macros; `TKey::tt : 5`,
  `TKey::next : 27` (`moo:` comments).
- `VM/src/lstate.h`: includes `moo_quat.h`, adds `struct MooQuat q;` to `union GCObject` and `gco2quat`.
- `VM/src/lobject.cpp`: includes `moo_quat.h`; `moo_quat_new` (via `luaM_newgcofixed` then `luaC_init`),
  `moo_quat_equal`, `moo_quat_tostring`; `case LUA_TQUAT:` in `luaO_rawequalObj` and `luaO_rawequalKey`.
- `VM/src/ltable.cpp`: `moo_quat_comp_hash` and `moo_quat_hash`; `LUA_TQUAT` in `mainposition`,
  `mainpositiontagged<LUA_TQUAT>`, `rawequalkeyvalue<LUA_TQUAT>`, `luaH_get` and `luaH_newkey`.
- `VM/src/lgc.cpp`: `LUA_TQUAT` in `reallymarkobject` (leaf, black), `isobjcleared` (value, never weak) and
  `freeobj` (`luaM_freegcofixed`, `sizeof(MooQuat)`).
- `VM/src/lmem.cpp` and `VM/src/moo_layout.cpp`: `static_assert(sizeof(MooQuat) == 48, ...)` beside the vec3 pins;
  `sizeof(global_State)` moves from 19232 to 19248 (two tag slots more in `mt[]` and `ttname[]`).
- `tests/moo/moo-v2-l3b-quat-object.test.cpp` (new, doctest suite `moo-v2-l3b-quat-object`):
  `QuatCreateAndLayoutInVm` (tag, `type()` is `"quat"`, payload, +1 creation number and +48 bytes per quat),
  `QuatEqualByValue` (all four components, `luaO_rawequalObj`/`Key`, `lua_rawequal`, script `rawequal`),
  `QuatTableKeyByValue` (two distinct equal quat are one key; a different scalar is a different key; iterative
  count; hash equality), `QuatTostringFormat` (`quat(1, -2.5, 0.1, 0)`, zero, raw units, range ends, no `#gcseq`,
  within `MOO_QUAT_TOSTR_MAX`), `QuatGcLeafAndChurn100k` (full-GC collection, a 10^5 destructor-free churn, weak
  `"k"`, `"v"`, `"kv"` preservation).
- `tests/conformance/moo/l3/moo-v2-l3b-quat-object.luau` (new): returns `"OK"` and pins that the `moo_test` helpers
  are present; a script cannot build a quat until card `moo-v2-l3b-quat`, so the exercising is in the C++ case.
- `tests/Moo.test.cpp`: five frozen values re-pinned (below).
- `tests/determinism/expected.txt`: seven programs re-pinned (below).
- `tests/moo/moo-v2-l3b-vec3-object-design.test.cpp`: the tag asserts follow the new tag (`LUA_TQUAT` 15,
  `LUA_TDEADKEY` 16, `LUA_T_COUNT` 16, `TKey::tt` 5 bits). The vec3 tag itself did not move.

## Re-pinned

Measured on amd-3 (Linux, clang, `Moo.Test`) at this tree. Exactly the 7 determinism cases and the five `Moo.test.cpp`
cases moved, from the one cause the vec3 note names: interning `"quat"` shifts every creation number by one, and
`sizeof(global_State)` grows by 16, moving GC pacing. Every new value is a function of raw integers and the program.

`tests/determinism/expected.txt` (effects, state):

| program | old | new |
|---|---|---|
| coroutines | `87415de4bd8d788a` `5c5b83003f192d3f` | `a2351a1850f47a3e` `82260fc9ccf89eea` |
| coroutines2 | `46858120d5307229` `f66cdf65627402aa` | `46858120d5307229` `04227eacd09eec18` |
| errors | `f7037948e0d4bea2` `a8553ceb065dad36` | `05180962e67f7b9c` `a054d87b62d19241` |
| gcchurn | `457e563d872baf1e` `2abd24e2bddab39e` | `4db2bab0d3b7857b` `aa44a22fb73d4818` |
| library | `4d053eec9658fe53` `1e2a00317a2595ce` | `acf48eff1179e3ff` `db3ea85e7aa881d0` |
| math | `2f1dd357cba511b4` `dda4446980e68248` | `875acf9dfe3e0d34` `d6184ba0ca32b3f3` |
| objectkeys | `5e3e1deb0bac6e7b` `507a9f51a8f19397` | `acd46a3c0b5a77a5` `3f134ee24fc598cb` |

`tests/Moo.test.cpp`:

| case | old | new |
|---|---|---|
| `ObjectKeyOrderIgnoresAddresses` | `42,27,41,56,8,9,33,23,48,21,60,16,47,7,45,58,35,57,12,51,55,54,36,53,13,52,6,34,49,25,50,59,24,46,44,22,43,38,5,32,3,18,11,4,28,20,2` | `41,26,40,55,52,30,8,28,22,47,43,16,15,9,60,34,51,59,36,50,57,56,35,54,39,5,42,53,13,24,49,58,48,46,45,21,44,37,4,31,2,17,19,3,27,25,6` |
| `TostringPrintsCreationNumber` | `table: #376 table: #377` | `table: #377 table: #378` |
| `GcPacingIsAFunctionOfTheProgram` | `1918608859321230699` | `9392283810654395904` |
| `DestructorsRunInCreationOrder` | `4673712535580941492` | `7098317604663815751` |
| `WeakTableOrderIsAFunctionOfTheProgram` | `11785329807427589751` | `1196433714162079160` |

## DESIGN.md sentences that must change

1. "Representations costed", (a): "and 48 bytes per stored vec3 (16-byte slot plus a 32-byte object). Sizes are 32
   bytes for vec3, 40 for quat and 64 for transform with the GC header." becomes "and 64 bytes per stored quat
   (16-byte slot plus a 48-byte object). Sizes are 40 bytes for vec3, 48 for quat and 72 for transform with moo's
   16-byte GC header (the creation number, DETERMINISM.md 1)."
2. "Layer 3 as built" gains a bullet: "**quat is a GC object.** `LUA_TQUAT` (15) follows `LUA_TVEC3`; a quat is
   `MooQuat` (`VM/src/moo_quat.h`), the 16-byte header and a `fixQuat` (a vec3 and a scalar), 48 bytes, immutable.
   `==`, `rawequal` and table keys compare and hash the four raw components; `tostring` prints `quat(x, y, z, s)`
   with the exact formatter; the GC treats it as a leaf and never clears it from a weak table. `TKey::tt` is now 5
   bits, so `LUA_TDEADKEY` (16) fits."
3. "The rest of determinism", **Object keys**: after the vec3 sentence add "A quat is a value too: it hashes by its
   four raw components."
4. "The rest of determinism", **Output**: after the vec3 sentence add "A quat prints its components,
   `quat(x, y, z, s)`."

## DETERMINISM.md sentences that must change

1. Status table, row 1: the vec3 exception becomes "except a vec3 or a quat, which are values and hash by their raw
   components (layer 3b)".
2. "The creation number costs 8 bytes in every object (...): a table header is 56 bytes (was 48), ..., a buffer
   header 16 (8)." gains ", a vec3 40 (32), a quat 48 (32)".
3. Inventory 1, **Fix**: after the vec3 sentence add "A quat is the exception too: equal quat are one key, and it
   hashes by its four raw components (`moo_quat_hash`)."
4. Inventory 6, **Fix**: after the vec3 sentence add "A quat prints `quat(x, y, z, s)` through the exact formatter."
5. Inventory 13: after the vec3 sentence add "A quat, like a string, is a value and is never cleared from a weak
   table."
6. Inventory 17: the vec3 parenthetical gains "; `LUA_TQUAT` follows it at 15, and `TKey::tt` is 5 bits so
   `LUA_TDEADKEY` 16 fits."
7. "The determinism suite" table: every row's effects and state hash change to the new values under "Re-pinned", and
   the paragraph that explains past re-pins gains "Re-pinned again when layer 3b added the quat tag: its type-name
   string shifts every creation number by one and `global_State` grows by two tag slots."

## Not done

- No operation (`quat * quat`, `quat * vec3`, `conjugate`, `nlerp`), no script constructor and no C API entry point:
  card `moo-v2-l3b-quat`. `slerp` waits for `fixed`.
- Seams in files outside this card's `PATHS` (`VM/src/lvmutils.cpp` `luaV_equalval`, `VM/src/lvmexecute.cpp`
  `LOP_JUMPIFEQ`/`LOP_JUMPIFNOTEQ`, `VM/src/lgcdebug.cpp`, `VM/src/laux.cpp` `luaL_tolstring`, `VM/src/lapi.cpp`
  `lua_topointer`, `tests/Determinism.test.cpp` `hashObject`) are left untouched here; **they must be added before a
  quat reaches a script `==`, `tostring` or the GC debug walk**, exactly as the vec3 note lists, and the transform
  card repeats them.
- `docs/DESIGN.md` and `docs/DETERMINISM.md` are not edited (the brief forbids it); "sentences that must change"
  lists what `moo-v2-l3b-conformance` folds in.
- `docs/PATCHES.md` rows were not reworded; `VM/src/ltm.cpp` and `VM/include/lua.h` already have rows, and
  `tools/moo-check-patches.sh` passes. The closing card should add "and quat" to the `ltm.cpp` row.
- The vec3 table-key path is not present at this checkout (card `moo-v2-l3b-vec3-object-key` has not landed); the
  quat path is implemented independently here.
