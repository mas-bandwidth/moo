# Layer 3b: vec3 as a table key (moo-v2-l3b-vec3-object-key)

Card `moo-v2-l3b-vec3-object-key` builds the key card of the shared vec3 note
(`docs/layer3b/moo-v2-l3b-vec3-object.md`, "Card moo-v2-l3b-vec3-object-key"). Its precondition
is met: card `moo-v2-l3b-vec3-object-gc` has landed (`luaO_rawequalKey` matches vec3 by value and
`vec3value` exists).

## What this card changed

- `VM/src/ltable.cpp`: define `moo_vec3_hash` (and its component helper `moo_vec3_comp_hash`) as
  decision 4 gives it: each raw component through hashint's MurmurHash64B finalizer, including the
  `ALT_HASH_FUNCTION` switch, the halves taken arithmetically, then combined with hashvec's
  spatial-hash primes in 32-bit unsigned arithmetic. Route `LUA_TVEC3` through `mainposition`,
  `mainpositiontagged<LUA_TVEC3>`, `rawequalkeyvalue<LUA_TVEC3>` (value equality), and the split
  `luaH_get`/`luaH_newkey` cases. Each change carries a `moo:` comment.
- `VM/include/lua.h`: add `lua_isvec3(L, n)` (`lua_type(L, n) == LUA_TVEC3`), the C API's view of
  the tag. `moo_pushvec3`/`moo_tovec3` are card `moo-v2-l3b-vec3-ops`'s and are not added here.
- NEW `tests/moo/moo-v2-l3b-vec3-object-key.test.cpp`, doctest suite
  `moo-v2-l3b-vec3-object-key`: `Vec3KeyEqualsAreOneKey` (two distinct equal vec3 are one key;
  `t[a] = 1` gives `t[b] == 1`; `next` visits one entry; a differing component makes a separate
  key), `Vec3HashFrozen` (the component hash is pinned for zero, single raw unit, Q48.16 units,
  the range ends and the widest negative), `Vec3KeyTableOrderPinned` (10^4 vec3 keys, delete every
  7th, insert a further 5x10^3 to force a rehash; the traversal order is pinned by a frozen hash and
  is the same on a second identical run), `Vec3KeySurvivesGc` (a vec3 key survives a full collect
  while its value is referenced), and `Vec3KeyNextSameObjectOnly` (in a weak `"k"` table, after
  `t[k] = nil` and a full collect, `next(t, k)` works only with the same object; an equal different
  object raises "invalid key to 'next'").
- NEW `tests/conformance/moo/l3/moo-v2-l3b-vec3-object.luau`: returns "OK" and pins that the
  `moo_test` helpers are present. It cannot build a vec3 without the C API of card
  `moo-v2-l3b-vec3-ops` (`moo_pushvec3`/`moo_tovec3`), so it leaves the vec3 exercising to the C++
  test above (recorded here per the brief's "write the script as the note says and say so").
- NEW `docs/layer3b/moo-v2-l3b-vec3-object-key.md`: this note. The shared note's "Card key" item 6
  says the `ltable.cpp` row in `docs/PATCHES.md` gains "and a vec3 by its components"; that edit is
  folded by the layer's closing card `moo-v2-l3b-conformance`, which also folds the "Sentences that
  must change" of the shared note. This card did not edit `docs/PATCHES.md`, `docs/DESIGN.md` or
  `docs/DETERMINISM.md` (the brief forbids them).

## Not done

- `moo_pushvec3`/`moo_tovec3` and the script constructor of a vec3: card
  `moo-v2-l3b-vec3-ops`'s. Until it lands the conformance script cannot construct a vec3, so it
  documents and returns "OK".
- `docs/PATCHES.md`'s `ltable.cpp` row wording, and the design note's "Sentences that must change":
  folded by the layer closing card.
