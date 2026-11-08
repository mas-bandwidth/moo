// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The GC paces itself by g->totalbytes, the sum of the sizes the VM asks the allocator for: a size from the
// canonical table in moo_layout.h plus a length the program chose (a string's bytes, an array's count). So when
// each GC step runs, which weak entries clear and when destructors run are a function of the program
// (docs/DETERMINISM.md, 4 and 5).
//
// C does not guarantee these sizes. These asserts do: on a 64-bit target every struct is exactly its canonical
// size, and a compiler that lays one out differently does not build. On wasm32 the structs are smaller and the VM
// requests and charges the canonical size anyway (moo_layout.h), so each struct must fit its canonical block, with
// an alignment the block offers. Any other target does not build until it has been checked (the plan's M6).
// Changing a struct changes the numbers here on purpose, and with them the determinism suite's pinned hashes.
#include "moo_layout.h"

#include "lobject.h"
#include "lstate.h"
#include "lmem.h"

#include <stddef.h>

static_assert(sizeof(void*) == 8 || MOO_TARGET_WASM32, "moo supports 64-bit targets and wasm32 only");
static_assert(sizeof(int) == 4 && (sizeof(size_t) == 8 || MOO_TARGET_WASM32), "moo pins the C integer sizes it allocates by");
static_assert(!MOO_TARGET_WASM32 || (sizeof(void*) == 4 && sizeof(size_t) == 4), "wasm32 has 4-byte pointers");

// the header and the value: the same on every target, and the hashing of table nodes depends on it
static_assert(sizeof(GCheader) == 16, "GC header layout changed");
static_assert(sizeof(TValue) == moo_sizeof(TValue), "value layout changed");
static_assert(sizeof(LuaNode) == moo_sizeof(LuaNode), "table node layout changed");
static_assert(sizeof(Instruction) == moo_sizeof(Instruction), "instruction size changed");
static_assert(sizeof(LuauVector) == moo_sizeof(LuauVector), "vector object layout changed");
static_assert(sizeof(FeedbackVectorSlot) == moo_sizeof(FeedbackVectorSlot), "feedback vector slot layout changed");
// docs/history/layer3b/moo-v2-l3b-vec3-object.md, decision 2
static_assert(sizeof(MooVec3) == moo_sizeof(MooVec3), "vec3 object layout changed");
// docs/history/layer3b/moo-v2-l3b-quat-object.md, decision 2
static_assert(sizeof(MooQuat) == moo_sizeof(MooQuat), "quat object layout changed");
// docs/history/layer3b/moo-v2-l3b-transform.md
static_assert(sizeof(MooTransform) == moo_sizeof(MooTransform), "transform object layout changed");
static_assert(offsetof(Buffer, data) == MOO_OFFSET_BUFFER_DATA, "buffer header layout changed");

// the structs with pointers: exact on 64-bit, within the canonical block on wasm32
MOO_LAYOUT_PIN(sizeof(LuaTable), moo_sizeof(LuaTable), "table layout changed");
MOO_LAYOUT_PIN(offsetof(TString, data), MOO_OFFSET_TSTRING_DATA, "string header layout changed");
MOO_LAYOUT_PIN(offsetof(Udata, data), MOO_OFFSET_UDATA_DATA, "userdata header layout changed");
MOO_LAYOUT_PIN(sizeof(Proto), moo_sizeof(Proto), "function prototype layout changed");
MOO_LAYOUT_PIN(sizeof(LocVar), moo_sizeof(LocVar), "local variable record layout changed");
MOO_LAYOUT_PIN(sizeof(UpVal), moo_sizeof(UpVal), "upvalue layout changed");
MOO_LAYOUT_PIN(offsetof(Closure, c.upvals), MOO_OFFSET_CCLOSURE_UPVALS, "C closure layout changed");
MOO_LAYOUT_PIN(offsetof(Closure, l.uprefs), MOO_OFFSET_LCLOSURE_UPREFS, "Luau closure layout changed");
MOO_LAYOUT_PIN(sizeof(LuauClass), moo_sizeof(LuauClass), "class layout changed");
MOO_LAYOUT_PIN(sizeof(LuauObject), moo_sizeof(LuauObject), "class object layout changed");
MOO_LAYOUT_PIN(sizeof(lua_State), moo_sizeof(lua_State), "thread layout changed");
MOO_LAYOUT_PIN(sizeof(CallInfo), moo_sizeof(CallInfo), "call frame layout changed");
MOO_LAYOUT_PIN(sizeof(global_State), moo_sizeof(global_State), "global state layout changed (the initial totalbytes includes it)");

// every canonical block is a multiple of 8 and a struct needs no more than that, so a block laid out for 64-bit
// holds the wasm32 struct aligned; the userdata payload's 16-byte alignment is the page allocator's (lmem.cpp)
static_assert(alignof(TValue) <= 8 && alignof(LuaNode) <= 8 && alignof(LuaTable) <= 8 && alignof(TString) <= 8, "object alignment");
static_assert(alignof(Udata) <= 8 && alignof(Buffer) <= 8 && alignof(Proto) <= 8 && alignof(LocVar) <= 8, "object alignment");
static_assert(alignof(UpVal) <= 8 && alignof(Closure) <= 8 && alignof(LuauClass) <= 8 && alignof(LuauObject) <= 8, "object alignment");
static_assert(alignof(lua_State) <= 8 && alignof(CallInfo) <= 8 && alignof(FeedbackVectorSlot) <= 8, "object alignment");
static_assert(alignof(MooVec3) <= 8 && alignof(MooQuat) <= 8 && alignof(MooTransform) <= 8 && alignof(LuauVector) <= 8, "object alignment");
// global_State carries alignas(16) storage; it is the host allocator's one block of that alignment (lstate.cpp LG)
static_assert(alignof(global_State) <= 16, "global state alignment");

// the pacing arithmetic is 64 bits wide on every target (lgc.cpp luaC_step and luaC_fullgc): a 32-bit size_t
// wrapped past about 14.3 MB of heap at the default goal and step multiplier
static_assert(sizeof(global_State::totalbytes) == 8 && sizeof(global_State::GCthreshold) == 8, "GC pacing width");
