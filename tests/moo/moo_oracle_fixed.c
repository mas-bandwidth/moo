// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The oracle of the moo-oracle suite (tests/moo/moo-l3-oracle-harness.test.cpp): fixed's own C, compiled as C in its
// own translation unit with FIX_SATURATE as moo builds it (CMakeLists.txt, Moo.Fixed), and called through extern "C".
// `*` and `/` are fixMul and fixDiv themselves. The operations moo defines on top of fixed (docs/DESIGN.md
// "Semantics": saturating + - and unary -, floor division, floor remainder, comparisons) are written again here from
// that table's definitions, in fixed's 128-bit arithmetic (fixed_int128.h), and never call moo_number.h: a result
// is computed exactly in 128 bits and then saturated into the domain, where moo_number.h uses 64-bit range checks.
#include "fixed/fixed.h"

#ifndef FIX_SATURATE
#error "the oracle is fixed built as moo builds it: FIX_SATURATE (docs/DESIGN.md, Overflow)"
#endif

// docs/DESIGN.md "Representation" and "Overflow": the domain is [-INT64_MAX, INT64_MAX] raw; a result outside it
// saturates to the nearest end.
static int64_t mooOracleSaturate(fixInt128 x)
{
    if (fixInt128Gt(x, fixInt128FromI64(INT64_MAX)))
        return INT64_MAX;
    if (fixInt128Lt(x, fixInt128FromI64(-INT64_MAX)))
        return -INT64_MAX;
    return fixInt128ToI64(x);
}

// floor(a / b) of the raw values, exactly, b != 0. Both raws share the 2^16 scale, so this is the floor of the
// exact quotient of the numbers (docs/DESIGN.md "Semantics", `a // b`).
static fixInt128 mooOracleFloorQuotient(int64_t a, int64_t b)
{
    fixInt128 wa = fixInt128FromI64(a);
    fixInt128 wb = fixInt128FromI64(b);
    fixInt128 q = fixInt128Div(wa, wb); // truncates toward zero
    fixInt128 r = fixInt128Sub(wa, fixInt128Mul(q, wb));
    if (!fixInt128Eq(r, fixInt128FromI64(0)) && fixInt128IsNegative(r) != fixInt128IsNegative(wb))
        q = fixInt128Sub(q, fixInt128FromI64(1));
    return q;
}

// docs/DESIGN.md "Semantics": `a + b`, int64 add, saturating (exact)
int64_t moo_oracle_add(int64_t a, int64_t b)
{
    return mooOracleSaturate(fixInt128Add(fixInt128FromI64(a), fixInt128FromI64(b)));
}

// docs/DESIGN.md "Semantics": `a - b`, int64 sub, saturating (exact)
int64_t moo_oracle_sub(int64_t a, int64_t b)
{
    return mooOracleSaturate(fixInt128Sub(fixInt128FromI64(a), fixInt128FromI64(b)));
}

// docs/DESIGN.md "Semantics": `-a`, negate, saturating (exact)
int64_t moo_oracle_unm(int64_t a)
{
    return mooOracleSaturate(fixInt128Neg(fixInt128FromI64(a)));
}

// docs/DESIGN.md "Semantics": `a * b` is fixMul with FIX_SATURATE, round half up
int64_t moo_oracle_mul(int64_t a, int64_t b)
{
    return fixMul(a, b);
}

// docs/DESIGN.md "Semantics": `a / b` is fixDiv, toward zero. b != 0: "Division by zero" makes / by zero the named
// error before fixDiv is reached, so the harness never asks the oracle for it.
int64_t moo_oracle_div(int64_t a, int64_t b)
{
    return fixDiv(a, b);
}

// docs/DESIGN.md "Semantics": `a // b` is the integer floor of the exact quotient, as a number, saturating. b != 0.
int64_t moo_oracle_idiv(int64_t a, int64_t b)
{
    fixInt128 q = mooOracleFloorQuotient(a, b);
    return mooOracleSaturate(fixInt128Mul(q, fixInt128FromI64(FIX_ONE)));
}

// docs/DESIGN.md "Semantics": `a % b` is a - floor(a / b) * b on the raws, exact, with the sign of b. b != 0.
int64_t moo_oracle_mod(int64_t a, int64_t b)
{
    fixInt128 q = mooOracleFloorQuotient(a, b);
    return mooOracleSaturate(fixInt128Sub(fixInt128FromI64(a), fixInt128Mul(q, fixInt128FromI64(b))));
}

// docs/DESIGN.md "Semantics": comparisons are int64 compares of the raw values; 1 for true, 0 for false
int64_t moo_oracle_lt(int64_t a, int64_t b)
{
    return a < b;
}

int64_t moo_oracle_le(int64_t a, int64_t b)
{
    return a <= b;
}

int64_t moo_oracle_gt(int64_t a, int64_t b)
{
    return a > b;
}

int64_t moo_oracle_ge(int64_t a, int64_t b)
{
    return a >= b;
}

int64_t moo_oracle_eq(int64_t a, int64_t b)
{
    return a == b;
}

int64_t moo_oracle_ne(int64_t a, int64_t b)
{
    return a != b;
}
