# moo-l3-vector-vm-out

Decision: remove vector-specific VM execution and arithmetic paths now that the C API, library and bytecode constant cannot create a vector value. Keep the vector type-name slots in `ltm.cpp` so Luau's type table layout remains unchanged; they do not make a vector reachable or observable through `typeof`.

The VM's named vector branches were deleted from `luaV_execute` in `VM/src/lvmexecute.cpp` and from `luaV_doarithimpl` and `luaV_equalval` in `VM/src/lvmutils.cpp`. Raw set no longer checks a vector NaN key in `VM/src/lbuiltins.cpp`; raw equality no longer compares vector values in `VM/src/lobject.cpp`; the vector-to-double selector was removed from the scalar math macros in `VM/src/lnumutils.h`. The opcode regression script covers arithmetic, indexing, calls and comparisons on fixed numbers, tables and strings.

The `moo-l3-conformance` closing card should make these exact documentation edits:

- In DESIGN.md, “Layer 3 as built” → “TEMPORARY bridges”: remove “and the float `vector` type convert through doubles (`moo_numbridgetodouble`, `moo_numbridgefromdouble`), until the math library is rebuilt on fixed and the vector type is removed;” while retaining the math library, `^` and `math.noise` bridge text.
- In DETERMINISM.md, item 17 “The `vector` type is float32”: replace “**Fix:** in layer 3 the float vector type is removed. Layer 3b brings `vec3`, `quat` and `transform` back with full Q48.16 components (DESIGN.md, "vec3, quat and transform").” with “**Fixed in layer 3:** the float vector type is removed. Layer 3b brings `vec3`, `quat` and `transform` back with full Q48.16 components (DESIGN.md, "vec3, quat and transform").”

No vector type-name table entry is removed, and this note does not edit DESIGN.md or DETERMINISM.md directly.
