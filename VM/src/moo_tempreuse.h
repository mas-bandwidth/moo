// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: expression-temporary reuse, the VM's half (docs/DESIGN.md "Expression-temporary reuse"; tla/TempReuse.tla).
//
// The compiler marks an arithmetic op (LOP_ADDTB .. LOP_MINUST, Bytecode.h) when one operand register holds an
// expression temporary: an arithmetic op of the same expression produced it, and nothing else reads it
// (Compiler/src/MooTempReuse.h). When that op's operation is a vec3, quat or transform one, the VM writes the result
// into the object in that register instead of allocating, provided the object is of the result's type and was
// created at or after the fence (luaV_doarithtemp, lvmutils.cpp).
//
// The fence is what makes "nothing else can reach it" true at run time. A value reaches an expression temporary
// either from the arithmetic fast path, as a new object, or from a metamethod, which may return any object the
// program holds; and while a temporary is live, a host callback may read the running frame's registers. So every
// place where code outside the arithmetic can have run moves the fence to the next creation number when that code
// returns, and no object that existed then is overwritten: a metamethod call (callTMres, luaV_callTM in
// lvmutils.cpp), the interrupt (VM_INTERRUPT in lvmexecute.cpp, GC_INTERRUPT in lgc.cpp), a debug hook
// (luau_callhook), the destructor queue (rundtorqueue in lgc.cpp), and a resume (lua_resume, lua_resumeerror in
// ldo.cpp), which covers whatever the host did to a suspended thread. The fence moves only forward, and it moves
// on nothing the program computes, so where it is follows from the program alone: deterministic.
#pragma once

#include "lstate.h"

inline void moo_tempfence(lua_State* L)
{
    L->global->mootempfence = L->global->gcseq;
}
