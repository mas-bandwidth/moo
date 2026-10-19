# Layer 3b: the vec3 object (moo-v2-l3b-vec3-object)

The shared note of the vec3 split. Card `moo-v2-l3b-vec3-object-design` writes the decisions and pins the layout;
cards `moo-v2-l3b-vec3-object-gc` and `moo-v2-l3b-vec3-object-key` build them and append their own sections below.
The quat and transform cards copy the decisions. The layer's closing card (`moo-v2-l3b-conformance`) folds this
note into `docs/DESIGN.md` and `docs/DETERMINISM.md`, using the sentences in "Sentences that must change".

The starting point is `docs/DESIGN.md`, "vec3, quat and transform (layer 3b)": option (a), immutable GC objects, the
representation Luau itself uses when `LUA_VECTOR_DOUBLE == 1` (`LuauVector`, `VM/src/lvector.cpp`). It costs nothing
to code that does not use the types. A vec3 is one GC object: the common header, with the object's creation number
(`docs/DETERMINISM.md`, 1), then three `fixed_t` components.

## Decisions

### 1. The type tag: a new `LUA_TVEC3` right after `LUA_TOBJECT`

`enum lua_Type` (`VM/include/lua.h`) gets `LUA_TVEC3` right after `LUA_TOBJECT`. It goes after the
`#if LUA_VECTOR_DOUBLE == 1` `LUA_TVECTOR` arm and before the comment "values below this line are used in GCObject
tags", so before `LUA_TDEADKEY`. In moo's build (`LUA_VECTOR_DOUBLE == 0`) it is **14**, so `LUA_T_COUNT`
(`= LUA_TDEADKEY`) becomes 15. `LUA_TVECTOR` keeps its slot (5) and stays unused.

Why:

- **It has to be a `TValue` tag and collectable.** A vec3 sits in registers, tables and upvalues, so its tag must be
  below `LUA_T_COUNT`: `lua_typename`'s `api_check` (`VM/src/lapi.cpp:351`), the per-type metatables `g->mt[]`, the type
  names `g->ttname[]` (`VM/src/lstate.h:217-218`) and `luaT_typenames` all index by it. It must also be at or above
  `LUA_TSTRING`, because `iscollectable(o)` is `ttype(o) >= LUA_TSTRING`. The only range that meets both is between
  `LUA_TOBJECT` and `LUA_TDEADKEY`, which is where upstream put its own last GC types (`LUA_TCLASS`, `LUA_TOBJECT`,
  and `LUA_TVECTOR` under `LUA_VECTOR_DOUBLE == 1`).
- **Not `LUA_TVECTOR`'s slot.** The slot is 5, below `LUA_TSTRING`, so a GC object tagged 5 is not collectable. Using
  it would mean moving `LUA_TVECTOR` into the GC range, which is the `LUA_VECTOR_DOUBLE == 1` layout. That renumbers
  every tag from `LUA_TSTRING` to `LUA_TOBJECT` (each moves down by one), and so it changes far more than a new tag
  does. It also changes what `LUA_TVECTOR` means in the C API. In Luau, a host that sees `lua_type(L, i) ==
  LUA_TVECTOR` reads float components with `lua_tovector`. Layer 3 refuses that API by name (`moo-l3-vector-api-out`).
  Giving the old name to a Q48.16 object would let ported Luau host code compile and then misread a vec3. With a new
  tag, ported code that tests for `LUA_TVECTOR` simply never matches.
- **Upstream merges.** The change is one inserted enumerator, in the shape upstream already uses to add types. Tags
  0 to 13, which are every existing `TValue` tag, keep their values. Only the GCObject-only tags move (`LUA_TDEADKEY`
  goes from 14 to 15, `LUA_TPROTO` from 15 to 16, `LUA_TUPVAL` from 16 to 17), and nothing outside the VM sees
  those.
- **Not after `LUA_TUPVAL`.** The carried attempt 2 head (`a1c6c089`) put the tag there, at 17. That is at or above
  `LUA_T_COUNT`, so it is not a `TValue` tag: `g->mt[17]` would read past the array, `lua_typename` would trip its
  `api_check`, and `TKey::tt` (4 bits) cannot hold 17, so a vec3 could never be a table key. This attempt removes
  it.
- **Not one shared tag for vec3, quat and transform** (with a kind byte in the header's padding). `type()`, the
  per-type metatables and `tostring` all dispatch on the tag, so a shared tag would add a second dispatch at each of
  those sites. Three tags cost three enumerators.

What adding the tag changes. This card made all of it (the second attempt, after the coordinator widened PATHS to
these files and told it to re-pin once, 2026-10-19):

1. `VM/src/ltm.cpp`, `luaT_typenames`: add `"vec3"` after the `#if LUA_VECTOR_DOUBLE == 1` `"vector"` arm. The
   `static_assert` at `ltm.cpp:76` holds the array to `LUA_T_COUNT`. `ltm.cpp` was not in `docs/PATCHES.md`, so it
   got a new row there (and the `lua.h` row names the tag), or `tools/moo-check-patches.sh` fails.
2. `VM/src/moo_layout.cpp`: `sizeof(global_State)` goes from 19216 to 19232, because `mt[LUA_T_COUNT]` and
   `ttname[LUA_T_COUNT]` each gain a pointer. The initial `totalbytes` includes it.
3. Every creation number moves up by one, and the pinned hashes move with them. `luaT_init` interns one type-name
   string per tag at startup, so `"vec3"` takes a creation number before the program runs, and every later object's
   number is one higher. Object keys hash by that number, so table order moves, and with it every hash pinned on it.
   `global_State` also grows (item 2), and that moves GC pacing. The state hash covers each object's `tt` too, and
   every proto and upvalue changes tag. So both hashes of every program in `tests/determinism/expected.txt` change,
   and so do five frozen values in `tests/Moo.test.cpp`. "Re-pinned" below has every old and new value. Each of the
   quat and transform tags does the same again: the coordinator chose to add only the vec3 tag here, so the quat and
   transform cards each add their own tag and re-pin once more (the `TKey` widening of item 4 goes with quat).
4. `TKey::tt` (`VM/src/lobject.h:510`) is 4 bits. With vec3, `LUA_TDEADKEY` is 15, the largest value it holds, so
   vec3 still fits (`static_assert`, `VM/src/ltable.cpp:54`). The quat and transform tags push `LUA_TDEADKEY` to 17,
   so the quat card widens `TKey` to `unsigned tt : 5; int next : 27;`. `next` needs ±(2^26 - 1)
   (`MAXBITS` 26, `static_assert`s at `ltable.cpp:55-56`), and 27 signed bits hold that. `sizeof(TKey)` does not
   change.

### 2. The layout: `MooVec3`, 40 bytes, 24 of them payload

`VM/src/moo_vec3.h`:

```c
typedef struct MooVec3
{
    CommonHeader;      // uint64_t gcseq; uint8_t tt; uint8_t marked; uint8_t memcat  (16 bytes with padding)
    fixed_t x;         // offset 16
    fixed_t y;         // offset 24
    fixed_t z;         // offset 32
} MooVec3;             // sizeof 40, alignof 8
```

The name follows upstream's later GC types (`LuauVector`, `LuauBuffer`, `LuauClass`, `LuauObject`), and the prefix
keeps it clear of `fixVec3` and of any host's `Vec3`. The payload is fixed's `fixVec3`, component for component. An
operation copies `x`, `y`, `z` into a `fixVec3` by value and calls fixed's function on that copy. It never casts the
object's address. Each vec3 adds 40 to `totalbytes` (`docs/DETERMINISM.md`, 5) and takes one creation number. The
`static_assert`s in `moo_vec3.h` pin the payload (24), the component offsets and the size (40).

### 3. Equality: by value

`==`, `~=`, `rawequal` and table-key matching compare the three raw components (`moo_vec3_equal`). They never
compare the address or the creation number. A vec3 is a value like a number, so `==` never calls `__eq`. Q48.16 has
no NaN and no negative zero, so value equality is plain integer equality, and `a == a` always holds.

### 4. Table keys: hashed by the three raw components

`moo_vec3_hash(v)` runs each raw component `c` through hashint's MurmurHash64B finalizer. Call that `f(c)`: with
`h1 = uint32_t(c)` and `h2 = uint32_t(uint64_t(c) >> 32)`, the four steps of `VM/src/ltable.cpp` `hashint`, including
its `ALT_HASH_FUNCTION` switch, run on `h1` and `h2`, and the result is `h2`. The halves are taken arithmetically. The
`memcpy` that `hashint` uses would swap them on a big-endian target. The three results are then combined with hashvec's spatial-hash primes, in 32-bit
unsigned arithmetic: `f(x) * 73856093 ^ f(y) * 19349663 ^ f(z) * 83492791`. `mainposition` passes that to
`hashpow2`. Two equal vec3 have the same hash and match by value, so they are one key. The hash depends only on the
raw integers, so it is deterministic by construction (no address and no creation number). Q48.16 has no NaN, so
unlike `LUA_TVECTOR` (`ltable.cpp:1376`) there is no "table index contains NaN" refusal. Every vec3 is a valid key.

- **Dead keys stay identity-only.** `findindex` (`ltable.cpp:380`) matches a dead key by pointer identity
  (`gcvalue(gkey(n)) == gcvalue(key)`). It must never read a dead key's object, which may already be freed. Until a
  GC turns the emptied entry into a dead key, `luaO_rawequalKey` still matches it by value. After that, `next(t, k)`
  works only with the same object, which is what `pairs` passes. A different vec3 with equal components raises
  "invalid key to 'next'", as upstream's double vectors do.
- **An emptied entry's key must die before its object is freed.** Value equality reads the key's object, which
  identity equality never does. Today an entry with a nil value can keep a live collectable tag on a key whose
  object the sweep has freed. `traversetable` returns early for `"kv"` tables (`lgc.cpp:369`), so it never calls
  `removeentry` on them. `cleartable` skips nil-valued nodes (`lgc.cpp:740`). So a `"k"` or `"kv"` table that sets
  `t[k] = nil` after it was traversed leaves `k` unmarked and still tagged vec3. A later lookup through
  `luaO_rawequalKey` or `rawequalkeyvalue<LUA_TVEC3>` would then compare against freed memory. Strings never hit
  this because they compare by pointer. Card gc closes it: `cleartable` calls `removeentry(n)` on every
  nil-valued node with a collectable key, so such a key is `LUA_TDEADKEY` before the sweep frees its object.
- **Never weak.** A vec3 is a value, so like a string it is never cleared from a weak table: `isobjcleared`
  (`VM/src/lgc.cpp:665`) marks it and returns 0. Otherwise a key `vec3(1, 2, 3)` in a weak-keyed table would vanish
  while an equal vec3 is still live elsewhere.

### 5. tostring: `vec3(x, y, z)`

`moo_vec3_tostring` writes `vec3(` and then the three components, separated by `, `, each through `moo_numtostr`
(the shortest decimal that reads back exactly, `docs/DESIGN.md` "Number formatting"), then `)`. For example
`vec3(1, -2.5, 0.1)`. It writes into a caller's buffer of `MOO_VEC3_TOSTR_MAX` (112) bytes and returns the end, as
`moo_numtostr` does. It does not allocate. It never prints a creation number: a vec3 is a value, not a reference
(`docs/DETERMINISM.md`, 6).

### 6. The GC: a leaf, like a string

A vec3 holds no reference to another object. `reallymarkobject` turns it black directly and never puts it on the gray
list (as for `LUA_TVECTOR`, `lgc.cpp:306`). `propagatemark` never sees it. It has no destructor, and `freeobj` frees
it with `luaM_freegcofixed(L, v, sizeof(MooVec3), v->memcat, page)`, as `luaVec_freevector` does. **The GC's object
lifecycle does not change.** A vec3 goes white, black, swept, freed, which are the states a string or buffer already
has. There is no new state and no new transition, so no TLA+ model is owed (house rule, `docs/DESIGN.md` "State
machines and TLA+"). The destructor queue (`tla/DestructorQueue.tla`) never sees a vec3.

### 7. Immutability

`moo_vec3_new` writes `x`, `y` and `z` once, right after `luaC_init`, and nothing writes them again: no C API, no
instruction and no library function. That is what makes it safe to copy a vec3 `TValue` freely and to use it as a
key: its hash never changes after insertion. The planned compiler reuse of expression temporaries
(`docs/DESIGN.md`, "Representations costed") may overwrite a temporary that has never been stored, passed or
captured. When it is built, that card has to show the overwrite cannot be observed. It also changes how many objects
are allocated, and with that `totalbytes` and the creation numbers. That is still deterministic, because the compiler
makes the decision.

## What this card changed

- `VM/include/lua.h`, `enum lua_Type`: `LUA_TVEC3` after the `LUA_VECTOR_DOUBLE == 1` arm, before `LUA_TDEADKEY`
  (`moo:` comment). It is 14; `LUA_TDEADKEY` and `LUA_T_COUNT` are 15, `LUA_TPROTO` 16, `LUA_TUPVAL` 17, `LUA_T_ALL` 18.
- `VM/src/ltm.cpp`, `luaT_typenames`: `"vec3"` last (`moo:` comment), so `type()` and `lua_typename` will say
  `vec3` once card gc makes one.
- `VM/src/moo_layout.cpp`: `sizeof(global_State)` 19216 -> 19232 (`mt[]` and `ttname[]` each gain a pointer).
- `docs/PATCHES.md`: a new `VM/src/ltm.cpp` row, and the `VM/include/lua.h` row names the tag.
  `tools/moo-check-patches.sh` passes.
- `tests/determinism/expected.txt` and five frozen values in `tests/Moo.test.cpp`: re-pinned once, after clang and
  gcc gave identical new values (below).
- `VM/src/moo_vec3.h` (new): `MooVec3`, its `static_assert`s, `MOO_VEC3_TOSTR_MAX`, and the declarations of
  `moo_vec3_new`, `moo_vec3_equal`, `moo_vec3_tostring` and `moo_vec3_hash`, each citing this note. Nothing defines
  or calls them yet, so no object is created.
- `tests/moo/moo-v2-l3b-vec3-object-design.test.cpp` (new), doctest suite `moo-v2-l3b-vec3-object-design`:
  `Vec3ObjectLayout` (header field offsets equal `GCheader`'s, x/y/z at 16/24/32, size 40, align 8, payload equals
  `sizeof(fixVec3)`), `Vec3TagSlotIsAfterObject` (`LUA_TOBJECT` 13, `LUA_TVEC3` 14, `LUA_TDEADKEY` and `LUA_T_COUNT`
  15, which still fits `TKey::tt`, and `luaT_typenames[LUA_TVEC3]` is `"vec3"`), and `Vec3TostringBound` (the widest
  `moo_numtostr` output is at most 34 characters, which is what `MOO_VEC3_TOSTR_MAX` is built from).

## Re-pinned

Measured on hetzner2 (Linux; clang and gcc; Moo.Test) at the merged tree with the tag. Before the re-pin the same 6
cases failed on both compilers and nothing else did: `DeterminismSuite`, and in `tests/Moo.test.cpp`
`ObjectKeyOrderIgnoresAddresses`, `TostringPrintsCreationNumber`, `GcPacingIsAFunctionOfTheProgram`,
`DestructorsRunInCreationOrder` and `WeakTableOrderIsAFunctionOfTheProgram`. Every new value was identical on clang
and gcc, and identical to the first attempt's experiment on amd-3. All are from the one cause in decision 1, item 3.

`tests/determinism/expected.txt` (effects, state):

| program | old | new |
|---|---|---|
| coroutines | `a2351a1850f47a3e` `eae7f9734f410e3a` | `87415de4bd8d788a` `5c5b83003f192d3f` |
| coroutines2 | `2ca405a9600143fa` `dc579a5432f7898e` | `46858120d5307229` `f66cdf65627402aa` |
| errors | `ee7b3d6f2b3bfb9e` `4d4a67e1614c5a55` | `f7037948e0d4bea2` `a8553ceb065dad36` |
| gcchurn | `6d789a68240b8976` `5ee2eb0d2642bda5` | `457e563d872baf1e` `2abd24e2bddab39e` |
| library | `9ce6b3fc5ed7d5fe` `039fbc53f46d1d5a` | `4d053eec9658fe53` `1e2a00317a2595ce` |
| math | `a005f1cb6d366b18` `bb3e5e4465819cce` | `2f1dd357cba511b4` `dda4446980e68248` |
| objectkeys | `c026a789bb37c024` `38780e98f3910393` | `5e3e1deb0bac6e7b` `507a9f51a8f19397` |

`tests/Moo.test.cpp`:

| case | old | new |
|---|---|---|
| `ObjectKeyOrderIgnoresAddresses` | `43,28,42,57,9,10,34,24,49,22,47,17,59,8,46,58,36,56,13,52,55,54,37,53,1,41,35,50,15,51,60,16,45,2,44,48,40,39,6,32,4,19,12,5,29,21,3` | `42,27,41,56,8,9,33,23,48,21,60,16,47,7,45,58,35,57,12,51,55,54,36,53,13,52,6,34,49,25,50,59,24,46,44,22,43,38,5,32,3,18,11,4,28,20,2` |
| `TostringPrintsCreationNumber` | `table: #375 table: #376` | `table: #376 table: #377` |
| `GcPacingIsAFunctionOfTheProgram` | `8555275115460150344` | `1918608859321230699` |
| `DestructorsRunInCreationOrder` | `10826925009745639169` | `4673712535580941492` |
| `WeakTableOrderIsAFunctionOfTheProgram` | `4589104574784354874` | `11785329807427589751` |

## Card moo-v2-l3b-vec3-object-gc

Precondition: met by card `moo-v2-l3b-vec3-object-design`. `LUA_TVEC3` (14) is in `lua.h`, with the follow-on
changes from decision 1: `ltm.cpp` and its `PATCHES.md` row, `moo_layout.cpp`, and the re-pinned `expected.txt` and
`tests/Moo.test.cpp` values. This card adds no tag and re-pins nothing for the tag.

1. `VM/src/moo_vec3.h`: add the accessors, modelled on `lobject.h`'s `LUA_VECTOR_DOUBLE == 1` `setvvalue`
   (`lobject.h:134-142`): `ttisvec3(o)`, `vec3value(o)` (`check_exp(ttisvec3(o), gco2vec3((o)->value.gc))`),
   `setvec3value(L, obj, v)` (sets `value.gc` and `tt = LUA_TVEC3`, then `checkliveness`), and
   `gco2vec3(o)` (`check_exp((o)->gch.tt == LUA_TVEC3, &((o)->v3))`). They stay in the moo file, not in
   `lobject.h`.
2. `VM/src/lstate.h`, `union GCObject`: add `struct MooVec3 v3;` (`moo:` comment), and include `moo_vec3.h`.
3. `VM/src/lobject.cpp`: define `moo_vec3_new` (`luaM_newgcofixed(L, MooVec3, sizeof(MooVec3), L->activememcat,
   LUA_TVEC3)`, `luaC_init(L, v, LUA_TVEC3)`, then x, y, z), `moo_vec3_equal`, and `moo_vec3_tostring` (decision 5).
   In `luaO_rawequalObj` and `luaO_rawequalKey`, add `case LUA_TVEC3:` returning `moo_vec3_equal` on the two objects.
4. `VM/src/lvmutils.cpp`, `luaV_equalval`: `case LUA_TVEC3:` returns `moo_vec3_equal`, with no `__eq` lookup.
   **Required:** `VM/src/lvmexecute.cpp`, `LOP_JUMPIFEQ` and `LOP_JUMPIFNOTEQ`. Their type switches end in
   `default: LUAU_ASSERT(!"Unknown value type"); LUAU_UNREACHABLE();` (`:1428`, and the same in `LOP_JUMPIFNOTEQ`),
   so a vec3 has to get its own case. Use `case LUA_TVEC3:` that compares with `moo_vec3_equal` and jumps, or one
   that `break`s to the `luaV_equalval` slow path. Without the case, `==` on two vec3 is undefined behaviour in a
   release build.
5. `VM/src/lgc.cpp`: in `reallymarkobject`, `case LUA_TVEC3:` does `gray2black` and returns. In `freeobj`,
   `case LUA_TVEC3:` calls `luaM_freegcofixed(L, gco2vec3(o), sizeof(MooVec3), gco2vec3(o)->memcat, page)`. In
   `isobjcleared`, a vec3 is marked and returns 0, like a string (decision 4). In `cleartable`, call `removeentry(n)`
   on every nil-valued node with a collectable key (decision 4, "An emptied entry's key must die"). Test it with a
   weak `"k"` table and a weak `"kv"` table: set `t[k] = nil`, drop `k`, run a full collect, set
   `t[vec3 equal to k] = 1`, and check that `luaC_validate` passes.
6. `VM/src/lgcdebug.cpp`: `validateobj` gets `case LUA_TVEC3: break;` (no edges), `dumpobj` gets `return;`, and
   `enumobj` gets `enumnode(ctx, o, sizeof(MooVec3), NULL)`, beside each `LUA_TVECTOR` case (`:205`, `:675`,
   `:1103`).
7. `VM/src/laux.cpp`, `luaL_tolstring`: `case LUA_TVEC3:` fills a `char[MOO_VEC3_TOSTR_MAX]` with
   `moo_vec3_tostring(vec3value(luaA_toobject(L, idx)))` and pushes it with `lua_pushlstring`. Without this case,
   `default` would print `vec3: #N`.
8. `VM/src/moo_layout.cpp`: include `moo_vec3.h` and add `static_assert(sizeof(MooVec3) == 40, ...)` beside
   `LuauVector`'s, so that one file lists every pinned size.
9. `VM/src/lapi.cpp`, `lua_topointer`: its `default` returns the object's address, so two equal vec3 give two
   different pointers. Add `case LUA_TVEC3: return NULL;`, which is what a value gets, like a number.
10. `tests/Determinism.test.cpp`, `hashObject`: `case LUA_TVEC3:` hashes x, y and z. Today's programs make no vec3,
   so the pinned hashes do not move.
11. NEW `tests/moo/moo-v2-l3b-vec3-object-gc.test.cpp`. The test makes vec3s with `moo_vec3_new` and pushes them with
    `setvec3value` into a `TValue` and `luaA_pushvalue`, because there is no script constructor yet. It checks: `type` is
    `"vec3"`; `tostring` gives `vec3(1, -2.5, 0.1)`; `==` and `rawequal` are true for distinct equal objects and
    false when one component differs; the creation number goes up by one per vec3; `totalbytes` goes up by 40 per
    vec3; a full collect frees an unreachable vec3 (heap enumeration count); `luaC_validate` passes; and a vec3 key
    or value is never cleared from a weak table.
12. Append a "Card gc" section to this note. List the PATCHES.md rows the change needs: `lstate.h`, `lobject.cpp`,
    `lvmutils.cpp`, `lvmexecute.cpp`, `lapi.cpp`, `lgc.cpp`, `lgcdebug.cpp` and `laux.cpp` already have rows, and each row's reason gains
    "vec3 (layer 3b)".

## Card moo-v2-l3b-vec3-object-key

Precondition: card gc has landed (`luaO_rawequalKey` matches vec3 by value, and `vec3value` exists).

1. `VM/src/ltable.cpp`: define `moo_vec3_hash` as decision 4 gives it. It applies hashint's finalizer, the same four
   steps with the same `ALT_HASH_FUNCTION` switch, to each component. Copy the steps; `hashint` itself does not
   change.
2. `mainposition` (`ltable.cpp:226`): `case LUA_TVEC3: return hashpow2(t, moo_vec3_hash(vec3value(key)));`.
3. Add `mainpositiontagged<LUA_TVEC3>` and `rawequalkeyvalue<LUA_TVEC3>` specialisations taking
   `const MooVec3*`, the second using `moo_vec3_equal`. Then in `luaH_get` (`:1271`) add
   `case LUA_TVEC3: return chainsearchtagged<LUA_TVEC3>(...)`, and in `luaH_newkey` (`:1347`) add
   `case LUA_TVEC3: return newkeytagged<LUA_TVEC3, false>(...)`. Without them, `default` sends a vec3 down the
   `LUA_TGCO` path, which matches by pointer.
4. `findindex` (`:380`): leave the dead-key fallback as pointer identity (decision 4). Add nothing for NaN.
5. NEW `tests/moo/moo-v2-l3b-vec3-object-key.test.cpp`. It checks: two distinct equal vec3 are one key (`t[a] = 1`
   gives `t[b] == 1`, and `next` visits one entry); `moo_vec3_hash` is frozen for a few vectors, including
   zero, the range ends and a single raw unit; the `next` order of a vec3-keyed table through inserts, deletes and
   a rehash is pinned by a hash; and `next` after `t[k] = nil` and a full collect works with the same object and raises
   with an equal different one.
6. Append a "Card key" section to this note. The `ltable.cpp` row in `docs/PATCHES.md` gains "and a vec3 by its
   components".

## Sentences that must change

`docs/DESIGN.md`:

1. "Representations costed", (a): "and 48 bytes per stored vec3 (16-byte slot plus a 32-byte object). Sizes are 32
   bytes for vec3, 40 for quat and 64 for transform with the GC header." becomes "and 56 bytes per stored vec3
   (16-byte slot plus a 40-byte object). Sizes are 40 bytes for vec3, 48 for quat and 72 for transform with moo's
   16-byte GC header (the creation number, DETERMINISM.md 1)."
2. "Layer 3 as built" gains a bullet (or the layer 3b equivalent): "**vec3 is a GC object.** `LUA_TVEC3` (14) follows
   `LUA_TOBJECT`; a vec3 is `MooVec3` (`VM/src/moo_vec3.h`), the 16-byte header and three `fixed_t`, 40 bytes,
   immutable. `==`, `rawequal` and table keys compare and hash the three raw components; `tostring` prints
   `vec3(x, y, z)` with the exact formatter; the GC treats it as a leaf and never clears it from a weak table.
   `LUA_TVECTOR` stays unused."
3. "The rest of determinism", **Object keys**: after "never by address." add "A vec3 is a value: it hashes by its
   three raw components."
4. "The rest of determinism", **Output**: after "never an address." add "A vec3 prints its components,
   `vec3(x, y, z)`."

`docs/DETERMINISM.md`:

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
7. "The determinism suite" table: every row's effects and state hash change to the new values under "Re-pinned"
   (for example the `coroutines` row's `` `a2351a1850f47a3e` | `eae7f9734f410e3a` `` becomes
   `` `87415de4bd8d788a` | `5c5b83003f192d3f` ``), and the paragraph that explains past re-pins gains "Re-pinned
   again when layer 3b added the vec3 tag: its type-name string shifts every creation number by one."

## Not done

- No vec3 is created and no behaviour of a program changes beyond the creation-number shift: the four functions are
  declared, not defined (cards gc and key).
- `docs/DESIGN.md` and `docs/DETERMINISM.md` are not edited (the brief forbids it); "Sentences that must change"
  lists what `moo-v2-l3b-conformance` folds in, including the suite table's new hashes.
- quat and transform tags, and the `TKey::tt` widening, are their own cards' (decision 1, items 3 and 4).
