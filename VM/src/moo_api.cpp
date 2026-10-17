// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
#include "moo.h"

#include "lapi.h"
#include "lobject.h"
#include "lstate.h"

#include <string.h>

uint64_t moo_objectseq(lua_State* L, int idx)
{
    const TValue* o = luaA_toobject(L, idx);
    return o && iscollectable(o) ? gcvalue(o)->gch.gcseq : 0;
}

int moo_fromdouble(double x, int64_t* out)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof(bits));

    bool negative = (bits >> 63) != 0;
    int exponent = int((bits >> 52) & 0x7ff);
    uint64_t mantissa = bits & ((uint64_t(1) << 52) - 1);

    if (exponent == 0x7ff)
        return mantissa ? MOO_NUMBER_NAN : MOO_NUMBER_INFINITE;

    if (exponent == 0)
        exponent = 1; // subnormal: no implicit bit
    else
        mantissa |= uint64_t(1) << 52;

    // x = mantissa * 2^(exponent - 1075), so the raw value is mantissa * 2^(exponent - 1075 + 16)
    int shift = exponent - 1059;
    uint64_t magnitude;

    if (mantissa == 0)
        magnitude = 0;
    else if (shift >= 0)
    {
        if (shift >= 63 || mantissa > (uint64_t(INT64_MAX) >> shift))
            return MOO_NUMBER_OUT_OF_RANGE;
        magnitude = mantissa << shift;
    }
    else if (-shift >= 64)
        magnitude = 0; // below 2^-64 units: far under half a unit
    else
    {
        int drop = -shift;
        magnitude = mantissa >> drop;
        uint64_t rest = mantissa & ((uint64_t(1) << drop) - 1);
        uint64_t half = uint64_t(1) << (drop - 1);
        if (rest > half || (rest == half && (magnitude & 1)))
            magnitude += 1;
    }

    if (magnitude > uint64_t(INT64_MAX))
        return MOO_NUMBER_OUT_OF_RANGE;

    *out = negative ? -int64_t(magnitude) : int64_t(magnitude);
    return MOO_NUMBER_OK;
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
