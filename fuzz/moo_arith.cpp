// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License;
// see LICENSE-moo.txt for details
//
// moo: the differential fuzz target of layer 5 (docs/DESIGN.md "Layers", layer 5: "bit-exact agreement with fixed's
// C; fuzzing"; docs/DETERMINISM.md "The guarantee"). It reads two operands and an operator from the input, computes
// the operation through moo_number.h and through the layer 3 oracle's fixed C reference (tests/moo/moo_oracle_fixed.c),
// and aborts on any difference. It also feeds strings to moo_numparse and checks that the formatter's output reads
// back to the same raw value (docs/DESIGN.md "Number formatting"). Nothing here runs in Moo.Test: fuzzing is a
// functional activity.
#include "moo_number.h"
#include "moo_parse.h"
#include "moo_format.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C"
{
    int64_t moo_oracle_add(int64_t a, int64_t b);
    int64_t moo_oracle_sub(int64_t a, int64_t b);
    int64_t moo_oracle_unm(int64_t a);
    int64_t moo_oracle_mul(int64_t a, int64_t b);
    int64_t moo_oracle_div(int64_t a, int64_t b);
    int64_t moo_oracle_idiv(int64_t a, int64_t b);
    int64_t moo_oracle_mod(int64_t a, int64_t b);
    int64_t moo_oracle_lt(int64_t a, int64_t b);
    int64_t moo_oracle_le(int64_t a, int64_t b);
    int64_t moo_oracle_gt(int64_t a, int64_t b);
    int64_t moo_oracle_ge(int64_t a, int64_t b);
    int64_t moo_oracle_eq(int64_t a, int64_t b);
    int64_t moo_oracle_ne(int64_t a, int64_t b);
}

namespace
{

// the domain is [MOO_NUMBER_MIN, MOO_NUMBER_MAX] raw (docs/DESIGN.md "Representation"); the oracle's C assumes it
fixed_t mooFuzzClamp(int64_t v)
{
    if (v < MOO_NUMBER_MIN)
        return MOO_NUMBER_MIN;
    if (v > MOO_NUMBER_MAX)
        return MOO_NUMBER_MAX;
    return v;
}

// an operand built from the input, wrapping over a short input; the clamp reaches the edges, so libFuzzer finds them
int64_t mooFuzzOperand(const uint8_t* data, size_t size, size_t offset)
{
    uint64_t bits = 0;
    for (size_t i = 0; i < 8; ++i)
        bits = (bits << 8) | data[(offset + i) % size];
    int64_t v;
    memcpy(&v, &bits, sizeof(v));
    return mooFuzzClamp(v);
}

void mooFuzzMismatch(const char* op, int64_t a, int64_t b, int64_t got, int64_t want)
{
    fprintf(stderr, "moo-fuzz MISMATCH op=%s a=%lld b=%lld moo=%lld oracle=%lld\n", op, (long long)a, (long long)b, (long long)got,
        (long long)want);
    fflush(stderr);
    abort();
}

// moo's tonumber strips one sign before moo_numparse (VM/src/lobject.cpp luaO_str2d); the formatter writes the sign
int mooFuzzParseSigned(const char* text, fixed_t* out)
{
    bool negative = false;
    if (*text == '-' || *text == '+')
        negative = *text++ == '-';
    int status = moo_numparse(text, out);
    if (status == MOO_PARSE_OK && negative)
        *out = -*out;
    return status;
}

// tostring(x) then parse must give x back, bit for bit (docs/DESIGN.md "Number formatting": the shortest decimal
// that reads back to exactly the same raw value)
void mooFuzzRoundTrip(fixed_t value)
{
    char buffer[64];
    char* end = moo_numtostr(buffer, value);
    if (end < buffer || end >= buffer + sizeof(buffer))
    {
        fprintf(stderr, "moo-fuzz MISMATCH tostring overflow for %lld\n", (long long)value);
        fflush(stderr);
        abort();
    }
    *end = 0;

    fixed_t back = 0;
    int status = mooFuzzParseSigned(buffer, &back);
    if (status != MOO_PARSE_OK || back != value)
        mooFuzzMismatch("tostring/tonumber", value, 0, back, value);
}

// the raw input and a decimal-shaped variant, both parsed when they are well formed; a parsed value must round-trip
void mooFuzzParse(const uint8_t* data, size_t size)
{
    size_t n = size < 256 ? size : 256;

    char raw[257];
    memcpy(raw, data, n);
    raw[n] = 0;

    char decimal[257];
    for (size_t i = 0; i < n; ++i)
    {
        if (i % 7 == 5)
            decimal[i] = '.';
        else if (i % 11 == 9)
            decimal[i] = 'e';
        else
            decimal[i] = char('0' + data[i] % 10);
    }
    decimal[n] = 0;

    const char* candidates[] = {raw, decimal};
    for (const char* text : candidates)
    {
        fixed_t parsed = 0;
        if (moo_numparse(text, &parsed) == MOO_PARSE_OK)
            mooFuzzRoundTrip(parsed);
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{
    if (Size < 2)
        return 0;

    // docs/DESIGN.md "Semantics", the operator table: one row per moo operator, against the oracle's own C
    int operation = Data[0] % 13;
    int64_t a = mooFuzzOperand(Data, Size, 1);
    int64_t b = Size >= 10 ? mooFuzzOperand(Data, Size, 9) : mooFuzzOperand(Data, Size, 1);

    int64_t got = 0;
    int64_t want = 0;
    const char* name = "?";

    switch (operation)
    {
    case 0:
        name = "+";
        got = moo_numadd(a, b), want = moo_oracle_add(a, b);
        break;
    case 1:
        name = "-";
        got = moo_numsub(a, b), want = moo_oracle_sub(a, b);
        break;
    case 2:
        name = "*";
        got = moo_nummul(a, b), want = moo_oracle_mul(a, b);
        break;
    case 3:
        name = "/";
        if (b == 0)
            return 0; // division by zero is the VM's named error; moo_number.h does not define it
        got = moo_numdiv(a, b), want = moo_oracle_div(a, b);
        break;
    case 4:
        name = "//";
        if (b == 0)
            return 0;
        got = moo_numidiv(a, b), want = moo_oracle_idiv(a, b);
        break;
    case 5:
        name = "%";
        if (b == 0)
            return 0;
        got = moo_nummod(a, b), want = moo_oracle_mod(a, b);
        break;
    case 6:
        name = "unary -";
        got = moo_numunm(a), want = moo_oracle_unm(a);
        break;
    case 7:
        name = "<";
        got = a < b, want = moo_oracle_lt(a, b);
        break;
    case 8:
        name = "<=";
        got = a <= b, want = moo_oracle_le(a, b);
        break;
    case 9:
        name = ">";
        got = a > b, want = moo_oracle_gt(a, b);
        break;
    case 10:
        name = ">=";
        got = a >= b, want = moo_oracle_ge(a, b);
        break;
    case 11:
        name = "==";
        got = a == b, want = moo_oracle_eq(a, b);
        break;
    default:
        name = "~=";
        got = a != b, want = moo_oracle_ne(a, b);
        break;
    }

    if (got != want)
        mooFuzzMismatch(name, a, b, got, want);

    mooFuzzRoundTrip(a);
    mooFuzzRoundTrip(b);
    mooFuzzParse(Data, Size);
    return 0;
}
