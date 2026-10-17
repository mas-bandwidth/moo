// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include "moo_number.h"

#include <math.h>
#include <cstdint>

// moo: a number is Q48.16 fixed point (moo_number.h); these are the seams every number operation goes
// through. Division, floor division and remainder by zero are refused by the caller with the named error
// "division by zero" before these run.
#define luai_numadd(a, b) moo_numadd(a, b)
#define luai_numsub(a, b) moo_numsub(a, b)
#define luai_nummul(a, b) moo_nummul(a, b)
#define luai_numdiv(a, b) moo_numdiv(a, b)
// TEMPORARY BRIDGE until pow is rebuilt on fixed (docs/DESIGN.md, "pow and ^"): libm pow on doubles
#define luai_numpow(a, b) moo_numbridgefromdouble(pow(moo_numbridgetodouble(a), moo_numbridgetodouble(b)))
#define luai_numunm(a) moo_numunm(a)
#define luai_numisnan(a) (false)
#define luai_numeq(a, b) ((a) == (b))
#define luai_numlt(a, b) ((a) < (b))
#define luai_numle(a, b) ((a) <= (b))
#define luai_inteq(a, b) ((a) == (b))

inline bool luai_veceq(const float* a, const float* b)
{
#if LUA_VECTOR_SIZE == 4
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
#else
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
#endif
}

inline bool luai_veceq(const double* a, const double* b)
{
#if LUA_VECTOR_SIZE == 4
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
#else
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
#endif
}

inline bool luai_vecisnan(const float* a)
{
#if LUA_VECTOR_SIZE == 4
    return a[0] != a[0] || a[1] != a[1] || a[2] != a[2] || a[3] != a[3];
#else
    return a[0] != a[0] || a[1] != a[1] || a[2] != a[2];
#endif
}

inline bool luai_vecisnan(const double* a)
{
#if LUA_VECTOR_SIZE == 4
    return a[0] != a[0] || a[1] != a[1] || a[2] != a[2] || a[3] != a[3];
#else
    return a[0] != a[0] || a[1] != a[1] || a[2] != a[2];
#endif
}

inline float luai_signf(float v)
{
    return v > 0.0f ? 1.0f : v < 0.0f ? -1.0f : 0.0f;
}

inline double luai_signd(double v)
{
    return v > 0.0 ? 1.0 : v < 0.0 ? -1.0 : 0.0;
}

inline float luai_clampf(float v, float min, float max)
{
    float r = v < min ? min : v;
    return r > max ? max : r;
}

inline double luai_clampd(double v, double min, double max)
{
    double r = v < min ? min : v;
    return r > max ? max : r;
}

inline fixed_t luai_nummod(fixed_t a, fixed_t b)
{
    return moo_nummod(a, b);
}

inline fixed_t luai_numidiv(fixed_t a, fixed_t b)
{
    return moo_numidiv(a, b);
}

LUAU_FASTMATH_BEGIN
inline double luai_nummod(double a, double b)
{
    return a - floor(a / b) * b;
}
LUAU_FASTMATH_END

LUAU_FASTMATH_BEGIN
inline double luai_numidiv(double a, double b)
{
    return floor(a / b);
}
LUAU_FASTMATH_END

inline float luai_lerpf(float a, float b, float t)
{
    return (t == 1.0f) ? b : a + (b - a) * t;
}

inline double luai_lerpd(double a, double b, double t)
{
    return (t == 1.0) ? b : a + (b - a) * t;
}

// moo: toward zero, saturating at the int range (no undefined float-to-int cast)
#define luai_num2int(i, d) ((i) = (int)moo_numclampint(moo_numtrunc(d)))

#define luai_num2long(i, d) ((i) = moo_numtrunc(d))

#define luai_fabs condvectordouble(fabs, fabsf)
#define luai_sqrt condvectordouble(sqrt, sqrtf)
#define luai_floor condvectordouble(floor, floorf)
#define luai_ceil condvectordouble(ceil, ceilf)
#define luai_sign condvectordouble(luai_signd, luai_signf)
#define luai_clamp condvectordouble(luai_clampd, luai_clampf)
#define luai_lerp condvectordouble(luai_lerpd, luai_lerpf)

// On MSVC in 32-bit, double to unsigned cast compiles into a call to __dtoui3, so we invoke x87->int64 conversion path manually
#if defined(_MSC_VER) && defined(_M_IX86)
#define luai_num2unsigned(i, n) \
    { \
        __int64 l; \
        __asm { __asm fld n __asm fistp l} \
        ; \
        i = (unsigned int)l; \
    }
#else
// moo: the integer part, toward zero, modulo 2^32: defined for every number
#define luai_num2unsigned(i, n) ((i) = (unsigned)(uint64_t)moo_numtrunc(n))
#endif

#define LUAI_MAXNUM2STR 48
#define LUAI_MAXINT2STR 30

LUAI_FUNC char* luai_num2str(char* buf, double n);
// TEMPORARY BRIDGE until moo's exact formatter (docs/DESIGN.md, "Number formatting"): a number printed
// through the double that holds it exactly (every raw value below 2^53 is exact in a double)
inline char* luai_fixed2str(char* buf, fixed_t n)
{
    return luai_num2str(buf, moo_numbridgetodouble(n));
}
LUAI_FUNC char* luai_int2str(char* buf, int64_t n);

#define luai_str2num(s, p) strtod((s), (p))
#define luai_str2long(s, p, base) strtoll((s), (p), base)
