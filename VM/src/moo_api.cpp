// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
#include "moo.h"

#include "lapi.h"
#include "lobject.h"
#include "lstate.h"

uint64_t moo_objectseq(lua_State* L, int idx)
{
    const TValue* o = luaA_toobject(L, idx);
    return o && iscollectable(o) ? gcvalue(o)->gch.gcseq : 0;
}
