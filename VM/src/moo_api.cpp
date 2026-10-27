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
    return moo_numfromfloat(x, out);
}

double moo_todouble(int64_t n)
{
    return moo_numtodouble(n);
}

float moo_tofloat(int64_t n)
{
    uint32_t bits = moo_numtofloatbits(n);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}
