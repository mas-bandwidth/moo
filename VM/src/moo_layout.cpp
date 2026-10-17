// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The GC paces itself by g->totalbytes, the sum of the sizes the VM requests from the allocator
// (lmem.cpp adds nsize, never the size class or the host allocator's own overhead). Each request is a
// size from the table below plus a payload length the program chose (a string's bytes, an array's
// element count, a userdata's length). So the allocation counter is a fixed formula per object kind and
// payload length, and with it when each GC step runs, which weak entries clear and when destructors
// run (docs/DETERMINISM.md, 4 and 5).
//
// Nothing in C guarantees these sizes on every compiler. These asserts do: a target whose layout
// differs in any of them does not build. A change to a struct changes the numbers here, on purpose,
// and with them the determinism suite's pinned hashes.
#include "lobject.h"
#include "lstate.h"

#include <stddef.h>

static_assert(sizeof(void*) == 8, "moo supports 64-bit targets only");
static_assert(sizeof(int) == 4 && sizeof(size_t) == 8, "moo pins the C integer sizes it allocates by");

static_assert(sizeof(GCheader) == 16, "GC header layout changed");
static_assert(sizeof(TValue) == 16, "value layout changed");
static_assert(sizeof(LuaNode) == 32, "table node layout changed");
static_assert(sizeof(LuaTable) == 56, "table layout changed");
static_assert(offsetof(TString, data) == 32, "string header layout changed");
static_assert(offsetof(Udata, data) == 32, "userdata header layout changed");
static_assert(offsetof(Buffer, data) == 16, "buffer header layout changed");
static_assert(sizeof(LuauVector) == 24, "vector object layout changed");
static_assert(sizeof(Proto) == 224, "function prototype layout changed");
static_assert(sizeof(LocVar) == 24, "local variable record layout changed");
static_assert(sizeof(FeedbackVectorSlot) == 16, "feedback vector slot layout changed");
static_assert(sizeof(Instruction) == 4, "instruction size changed");
static_assert(sizeof(UpVal) == 48, "upvalue layout changed");
static_assert(offsetof(Closure, c.upvals) == 56, "C closure layout changed");
static_assert(offsetof(Closure, l.uprefs) == 40, "Luau closure layout changed");
static_assert(sizeof(LuauClass) == 96, "class layout changed");
static_assert(sizeof(LuauObject) == 48, "class object layout changed");
static_assert(sizeof(lua_State) == 144, "thread layout changed");
static_assert(sizeof(CallInfo) == 48, "call frame layout changed");
static_assert(sizeof(global_State) == 19216, "global state layout changed (the initial totalbytes includes it)");
