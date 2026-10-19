# Layer 3b: vec3 as a table key (moo-v2-l3b-vec3-object-key) — HOLD

Card `moo-v2-l3b-vec3-object-key` HOLDs because its precondition is not met.

The card's note (`docs/layer3b/moo-v2-l3b-vec3-object.md`, "Card moo-v2-l3b-vec3-object-key")
states: "Precondition: card gc has landed (`luaO_rawequalKey` matches vec3 by value, and
`vec3value` exists)." At this card's base, card `moo-v2-l3b-vec3-object-gc` has not landed:

- the working tree is at `64ee22b1` (the design card's land commit), and `moo-v2-l3b-vec3-object-gc`
  (`96fd8b5b`) is not an ancestor of HEAD and is not on `origin/sprint/moo`;
- no gc machinery is present: `setvec3value`, `ttisvec3`, `vec3value` do not exist anywhere in `VM/`;
  the `GCObject` union in `VM/src/lstate.h` has no `v3` member; `moo_vec3_new` is declared in
  `VM/src/moo_vec3.h` but defined nowhere; `luaO_rawequalKey` and `luaO_rawequalObj`
  (`VM/src/lobject.cpp`) have no `LUA_TVEC3` case; and `tests/moo/moo-v2-l3b-vec3-object-gc.test.cpp`
  does not exist.

Without the gc card, the key card cannot be implemented or tested: `ltable.cpp` routing needs
`vec3value` and `moo_vec3_equal`, and the tests need `moo_vec3_new` (the test `setvec3value`) to build
vec3 objects. The dependency `DEPENDS-ON: moo-v2-l3b-vec3-object-gc` is unmet.

## What was done

- Nothing implemented: the card's code paths cannot be built or run without the gc dependency.

## Not done

- `VM/src/ltable.cpp` `moo_vec3_hash`, `mainposition`/`mainpositiontagged`/`rawequalkeyvalue` routing,
  `luaH_get`/`luaH_newkey` cases: not written, blocked on the missing gc card.
- `VM/include/lua.h` `lua_isvec3`-style macro: not added, blocked on the missing gc card.
- `tests/moo/moo-v2-l3b-vec3-object-key.test.cpp`: not written, needs `moo_vec3_new`.
- `tests/conformance/moo/l3/moo-v2-l3b-vec3-object.luau`: not written, needs the internal constructor.
- Gate commands were not run: the tree does not build the vec3 object without the gc card.
