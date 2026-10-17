// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo's number: Q48.16 fixed point, the raw signed 64-bit value of fixed's fixed_t
// (docs/DESIGN.md, "Representation" to "Division by zero"). Every operator the VM applies to a
// number is here, so the VM's arithmetic has one definition.
//
// The domain is [MOO_NUMBER_MIN, MOO_NUMBER_MAX] = [-INT64_MAX, INT64_MAX] raw, fixed's
// [FIX_MIN, FIX_MAX]: about +-1.4e14, symmetric, so negation is exact and fixed's C, which does
// not take INT64_MIN, takes every value. Every operation saturates into the domain, never wraps
// and never produces INT64_MIN. Division, floor division and remainder by zero are not defined
// here: the VM raises the named error "division by zero" before calling them.
#pragma once

#include "fixed/fixed.h"

#include <stdint.h>

#define MOO_NUMBER_MAX FIX_MAX
#define MOO_NUMBER_MIN FIX_MIN
#define MOO_NUMBER_ONE FIX_ONE
#define MOO_NUMBER_FRACTION_BITS FIX_FRACTION_BITS

// the largest and smallest integer value a number holds (2^47 - 1 and its negation)
#define MOO_NUMBER_INT_MAX (MOO_NUMBER_MAX >> MOO_NUMBER_FRACTION_BITS)

// a + b, exact, saturating (a and b in the domain)
inline fixed_t moo_numadd(fixed_t a, fixed_t b)
{
    if (b > 0 && a > MOO_NUMBER_MAX - b)
        return MOO_NUMBER_MAX;
    if (b < 0 && a < MOO_NUMBER_MIN - b)
        return MOO_NUMBER_MIN;
    return a + b;
}

// a - b, exact, saturating (a and b in the domain)
inline fixed_t moo_numsub(fixed_t a, fixed_t b)
{
    if (b < 0 && a > MOO_NUMBER_MAX + b)
        return MOO_NUMBER_MAX;
    if (b > 0 && a < MOO_NUMBER_MIN + b)
        return MOO_NUMBER_MIN;
    return a - b;
}

// -a, exact (the domain is symmetric)
inline fixed_t moo_numunm(fixed_t a)
{
    return a == INT64_MIN ? MOO_NUMBER_MAX : -a;
}

// a * b: fixed's fixMul, round half up, saturating (FIX_SATURATE)
inline fixed_t moo_nummul(fixed_t a, fixed_t b)
{
    return fixMul(a, b);
}

// a / b, b != 0: fixed's fixDiv, truncates toward zero, saturating
inline fixed_t moo_numdiv(fixed_t a, fixed_t b)
{
    return fixDiv(a, b);
}

// floor(a / b) as a number, b != 0: the exact quotient's floor (both raws share the 2^16 scale,
// so the integer quotient of the raws is the quotient's integer part), saturating
inline fixed_t moo_numidiv(fixed_t a, fixed_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q -= 1;
    if (q > MOO_NUMBER_INT_MAX)
        return MOO_NUMBER_MAX;
    if (q < -MOO_NUMBER_INT_MAX)
        return MOO_NUMBER_MIN;
    return fixShiftLeft(q, MOO_NUMBER_FRACTION_BITS);
}

// a - floor(a / b) * b, b != 0: exact, with the sign of b (Lua's rule)
inline fixed_t moo_nummod(fixed_t a, fixed_t b)
{
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        r += b;
    return r;
}

// true when the value has no fraction bits
inline bool moo_numisinteger(fixed_t a)
{
    return (a & (MOO_NUMBER_ONE - 1)) == 0;
}
