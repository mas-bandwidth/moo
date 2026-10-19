# moo-l3b-vec3-object

Layer 3 (`moo-l3-vector-api-out`, `moo-l3-vector-compiler-out`, `moo-l3-vector-constant-refused`, `moo-l3-vector-vm-out`) removed Luau's float32 `vector` from the C API, compiler, bytecode constant loader, and VM execution loop. Layer 3b brings back three fixed-point geometric types—`vec3`, `quat`, and `transform`—with full Q48.16 (`int64_t`) components matching `number` and `fixed3d` (`docs/DESIGN.md`, "vec3, quat and transform"). Because three 64-bit components (24 bytes) do not fit in a 16-byte `TValue` and widening `TValue` taxes general numeric and table code across the VM, `vec3` is represented as an immutable heap-allocated GC object (`moo_Vec3` in `VM/src/moo_vec3.h`).

## 1. The type tag (`LUA_TVEC3`)

**Decision:** Add a distinct `LUA_TVEC3` tag in `enum lua_Type` (`VM/include/lua.h`) after the existing collectable GC types (`LUA_TOBJECT` and the `#if LUA_VECTOR_DOUBLE == 1` `LUA_TVECTOR` arm), rather than reusing `LUA_TVECTOR`'s slot.

**Reason:**
- Under moo's configuration (`LUA_VECTOR_DOUBLE == 0`), `LUA_TVECTOR` sits at index 5, above `LUA_TSTRING` (index 6). Luau's GC predicate `iscollectable(o)` is `ttype(o) >= LUA_TSTRING` (`VM/src/lobject.h`). Reusing `LUA_TVECTOR` at index 5 would make `iscollectable` false for `vec3`, breaking `gcvalue`, `obj2gco`, `checkconsistency`, `checkliveness`, and write barriers unless `LUA_TVECTOR` were moved below `LUA_TSTRING`.
- Leaving `LUA_TVECTOR` untouched in its upstream slot preserves 1:1 alignment with upstream Luau in `lua.h`, `lobject.h`, and `ltm.cpp`, avoiding `#if LUA_VECTOR_DOUBLE` merge conflicts.
- Layer 3b introduces three sibling GC types (`vec3`, `quat`, `transform`). Giving each its own collectable tag (`LUA_TVEC3`, and later `LUA_TQUAT`, `LUA_TTRANSFORM`) after `LUA_TOBJECT` keeps all three uniform.
- **Boundary with `moo-l3b-vec3-object-gc`:** In this header-and-design card, `LUA_T_COUNT` is aliased to `LUA_TVEC3` (14) so that `VM/src/ltm.cpp` (`static_assert` on `luaT_typenames` length `== LUA_T_COUNT`) and `VM/src/moo_layout.cpp` (`static_assert` on `sizeof(global_State) == 19216`, which embeds `mt[LUA_T_COUNT]` and `ttname[LUA_T_COUNT]`) remain untouched outside this card's `PATHS`. `moo-l3b-vec3-object-gc` adds `"vec3"` to `luaT_typenames` in `VM/src/ltm.cpp`, advances `LUA_T_COUNT = LUA_TDEADKEY`, and updates `sizeof(global_State)` in `VM/src/moo_layout.cpp`.
- **Note on `TKey::tt` bit-width:** With `LUA_TVEC3 = 14`, `LUA_TDEADKEY` is `15`, which fits in `TKey::tt : 4` (`0..15`, verified by `VM/src/ltable.cpp:54`). When `LUA_TQUAT` and `LUA_TTRANSFORM` are added later in Layer 3b, `TKey::tt` in `VM/src/lobject.h:510` widens from `unsigned tt : 4; int next : 28;` to `unsigned tt : 5; int next : 27;` without changing `sizeof(TKey)`.

## 2. The `TValue` tag (`ttisvec3`, `vec3value`, `setvec3value`) and `iscollectable`

`VM/src/moo_vec3.h` defines the `TValue` accessors and mutator following `lobject.h`:

- `ttisvec3(o)`: `(ttype(o) == LUA_TVEC3)`. Because `LUA_TVEC3 >= LUA_TSTRING` (verified by `static_assert(LUA_TVEC3 >= LUA_TSTRING)`), `iscollectable(o)` (`ttype(o) >= LUA_TSTRING`) evaluates to `true` for every `vec3` `TValue` with no change to `iscollectable`.
- `gco2vec3(o)`: `check_exp((o)->gch.tt == LUA_TVEC3, cast_to(moo_Vec3*, (o)))` (and `&((o)->v3)` once `union GCObject` in `VM/src/lstate.h` adds `struct moo_Vec3 v3;` in `moo-l3b-vec3-object-gc`).
- `vec3value(o)`: `check_exp(ttisvec3(o), gco2vec3((o)->value.gc))`.
- `setvec3value(L, obj, x)`: stores `cast_to(GCObject*, x)` into `obj->value.gc`, sets `obj->tt = LUA_TVEC3`, and runs `checkliveness(L->global, obj)` (which verifies `ttype(obj) == obj->value.gc->gch.tt` and `!isdead(g, obj->value.gc)`).

## 3. The struct (`moo_Vec3`)

`VM/src/moo_vec3.h` defines `moo_Vec3` with `CommonHeader` followed by three signed 64-bit Q48.16 fields (`int64_t x, y, z`, matching `fixVec3` in `extern/fixed/include/fixed.h`):

```c
typedef struct moo_Vec3
{
    CommonHeader;

    int64_t x;
    int64_t y;
    int64_t z;
} moo_Vec3;
```

On 64-bit moo, `CommonHeader` is `uint64_t gcseq; uint8_t tt; uint8_t marked; uint8_t memcat;` (11 bytes) padded to 8-byte alignment = **16 bytes** (`sizeof(GCheader) == 16`). Followed by `x` (offset 16), `y` (offset 24), and `z` (offset 32), `sizeof(moo_Vec3)` is **40 bytes** with **8-byte alignment** (`alignof(moo_Vec3) == 8`). `VM/src/moo_vec3.h` enforces `sizeof(GCheader) == 16`, `sizeof(moo_Vec3) == 40`, `alignof(moo_Vec3) == 8`, and `offsetof(moo_Vec3, x/y/z) == 16/24/32` with compile-time `static_assert`s.

## 4. GC integration and deterministic numbering

`moo-l3b-vec3-object-gc` wires `moo_Vec3` into the garbage collector and state tables as a leaf fixed-size collectable object:

- **`GCObject` union (`VM/src/lstate.h`):** Adds `struct moo_Vec3 v3;` to `union GCObject`.
- **Allocation and `gcseq` (`moo_newvec3`):** Calls `luaM_newgcofixed(L, moo_Vec3, sizeof(moo_Vec3), L->activememcat, LUA_TVEC3)` (40-byte size class on `freegcopages`) and `luaC_init(L, v, LUA_TVEC3)`. `luaC_init` assigns `v->gcseq = L->global->gcseq++`, `v->marked = luaC_white(L->global)`, `v->tt = LUA_TVEC3`, and `v->memcat = L->activememcat`. Because `sizeof(moo_Vec3)` is a constant 40 bytes pinned by `static_assert`, `g->totalbytes` advances by a fixed 40 bytes per allocation across every platform (`docs/DETERMINISM.md`, item 4).
- **Marking (`reallymarkobject` / `propagatemark` in `VM/src/lgc.cpp`):** `moo_Vec3` holds three raw `int64_t` values and no `GCObject*` references. Like `LUA_TSTRING`, `LUA_TBUFFER`, and `LUA_TVECTOR`, `reallymarkobject` marks a white `LUA_TVEC3` directly black with `gray2black(o)` and never links it onto `g->gray`; `propagatemark` never visits it.
- **Sweeping and teardown (`freeobj` in `sweeplist` and `luaC_freeall` in `VM/src/lgc.cpp`):** Has no finalizer or destructor. `freeobj` dispatches `case LUA_TVEC3: luaM_freegco(L, gco2vec3(o), sizeof(moo_Vec3), o->gch.memcat, page); break;`.
- **Accounting and heap inspection (`sizeofgny` in `VM/src/lgc.cpp`, `luaC_validate` and `luaC_enumheap` in `VM/src/lgcdebug.cpp`):** `sizeofgny` returns `sizeof(moo_Vec3)` (40); `luaC_validate` accepts `case LUA_TVEC3:` with no outgoing edges; `luaC_enumheap` reports `enumnode(ctx, o, sizeof(moo_Vec3), NULL)`.

## 5. Value equality (`==` and `rawequal`) and table keys

- **Equality:** `vec3` is a value type in Luau semantics. Both `==` (`luaV_equalval` in `VM/src/lvmutils.cpp`) and `rawequal` (`luaO_rawequalObj` and `luaO_rawequalKey` in `VM/src/lobject.cpp`) compare two `LUA_TVEC3` values by their three Q48.16 components via `moo_vec3equal(vec3value(t1), vec3value(t2))` (`a->x == b->x && a->y == b->y && a->z == b->z`), never by `GCObject*` pointer identity or `gcseq`.
- **Table keys:** Because two distinct `moo_Vec3` allocations with the same `(x, y, z)` are `rawequal`, `mainposition` in `VM/src/ltable.cpp` must hash the 24 payload bytes (`&v->x`, three `int64_t` words) rather than `gcseq`, and table lookup/insertion (`getgeneric`, `newkey`) matches `LUA_TVEC3` keys through `luaO_rawequalKey` on those components. Because Q48.16 has no NaN and no `-0.0` (only a single zero `0`), every `vec3` is a valid, deterministic table key.
- **Owning card:** **`moo-l3b-vec3-equality-and-keys`** owns the `luaV_equalval`, `luaO_rawequalObj`, `luaO_rawequalKey`, and `ltable.cpp` `mainposition` / key-lookup changes.

## 6. Immutability

Every live `moo_Vec3` is immutable after `moo_newvec3` initializes `x, y, z` immediately following `luaC_init`. Neither the C API nor any Luau instruction or library function mutates `x`, `y`, or `z` on a live `moo_Vec3`. Immutability guarantees that copying a `vec3` `TValue` across stack registers, upvalues, and tables never aliases mutable state and that a `vec3` used as a table key never changes its hash after insertion.

The sole planned exception is the later compiler expression-temporary reuse optimization (`docs/DESIGN.md`, "Representations costed"), in which the compiler overwrites an intermediate `moo_Vec3` in place only when it has statically proven that the object resides in a freshly allocated expression temporary that has never been stored, passed, or captured.
