// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include "moo_number.h"
#include "moo_format.h"
#include "moo_parse.h"
#include "moo_pow.h"

#include "fixed/fixed_math.h"

#include <math.h>
#include <cstdint>

// moo: a number is Q48.16 (moo_number.h). Division, floor division and remainder by zero raise "division by
// zero" in the caller before these run.
#define luai_numadd(a, b) moo_numadd(a, b)
#define luai_numsub(a, b) moo_numsub(a, b)
#define luai_nummul(a, b) moo_nummul(a, b)
#define luai_numdiv(a, b) moo_numdiv(a, b)
#define luai_numunm(a) moo_numunm(a)

// moo: pow, one definition for the ^ operator, math.pow and its fastcall (docs/DESIGN.md "pow and ^"). An integer
// exponent is moo_pow_int, exact, rounded once. A fractional exponent is fixPow = fixExp2(exponent * fixLog2(base));
// 0 ^ negative is division by zero as for an integer, a negative base is an error, and fixPow's MOO_NUMBER_MAX is
// saturation. Returns a MOO_POW_ status, *out the release value.
inline int moo_numpow(fixed_t a, fixed_t b, fixed_t* out)
{
    if (moo_numisinteger(b))
        return moo_pow_int(a, b >> MOO_NUMBER_FRACTION_BITS, out);

    if (a == 0 && b < 0)
    {
        *out = 0;
        return MOO_POW_DIVISION_BY_ZERO;
    }

    if (a < 0)
    {
        *out = 0;
        return MOO_POW_NEGATIVE_BASE;
    }

    *out = fixPow(a, b);
    return *out == MOO_NUMBER_MAX ? MOO_POW_SATURATED : MOO_POW_OK;
}

// moo: the error a MOO_POW_ status raises in this build, or nullptr. Saturation raises only when checked.
inline const char* moo_numpowerror(int status)
{
    switch (status)
    {
    case MOO_POW_DIVISION_BY_ZERO:
        return "division by zero";
    case MOO_POW_NEGATIVE_BASE:
        return "pow of a negative number to a fractional power";
#if MOO_OVERFLOW_ERRORS
    case MOO_POW_SATURATED:
        return "number overflow in '^'";
#endif
    default:
        return nullptr;
    }
}

// moo: with MOO_OVERFLOW_ERRORS an operation that would saturate leaves the fast path (luai_numnosat is false)
// and luaV_doarithimpl raises "number overflow in '<op>'". In release luai_numnosat is true and evaluates
// nothing. ^ leaves the fast path on any moo_numpowerror.
#if MOO_OVERFLOW_ERRORS
#define luai_numaddsat(a, b) moo_numaddsaturates(a, b)
#define luai_numsubsat(a, b) moo_numsubsaturates(a, b)
#define luai_nummulsat(a, b) moo_nummulsaturates(a, b)
#define luai_numdivsat(a, b) moo_numdivsaturates(a, b)
#define luai_numidivsat(a, b) moo_numidivsaturates(a, b)
#define luai_numnosat(op, a, b) (!luai_num##op##sat(a, b))
#else
#define luai_numnosat(op, a, b) (true)
#endif
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

// moo: integer-valued check, and toward zero clamped to the int range (docs/DESIGN.md "Exact integers")
#define luai_numisint(d) moo_numisinteger(d)
#define luai_num2int(i, d) ((i) = (int)moo_numclampint(moo_numtrunc(d)))

#define luai_num2long(i, d) ((i) = moo_numtrunc(d))

// moo: only lveclib.cpp, which is not built into the VM, uses the double helpers
#define luai_fabs fabs
#define luai_sqrt sqrt
#define luai_floor floor
#define luai_ceil ceil
#define luai_sign luai_signd
#define luai_clamp luai_clampd
#define luai_lerp luai_lerpd

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
// moo: the shortest decimal that reads back to exactly the number (moo_format.h)
inline char* luai_fixed2str(char* buf, fixed_t n)
{
    return moo_numtostr(buf, n);
}
LUAI_FUNC char* luai_int2str(char* buf, int64_t n);

#define luai_str2num(s, p) strtod((s), (p))
#define luai_str2long(s, p, base) strtoll((s), (p), base)
