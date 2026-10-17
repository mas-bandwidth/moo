// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
#include "moo.h"

#include "lapi.h"
#include "lobject.h"
#include "lstate.h"
#include "moo_number.h"

#include <string.h>

uint64_t moo_objectseq(lua_State* L, int idx)
{
    const TValue* o = luaA_toobject(L, idx);
    return o && iscollectable(o) ? gcvalue(o)->gch.gcseq : 0;
}

int moo_fromdouble(double x, int64_t* out)
{
    return moo_numfromdouble(x, out);
}

int moo_fromfloat(float x, int64_t* out)
{
    return moo_fromdouble(double(x), out); // float to double is exact
}

double moo_todouble(int64_t n)
{
    // int64 to double rounds to nearest even; the scale by 2^-16 is exact
    return double(n) * (1.0 / 65536.0);
}
