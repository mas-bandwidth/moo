// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// moo: this upstream target tested Luau's double number path (luai_num2str + strtod). moo's number is Q48.16 fixed
// point, so it now reads a raw fixed_t and exercises moo's exact formatter and parser instead
// (docs/DESIGN.md "Number formatting", "Layer 3 as built" "Formatting and tonumber are exact").
#include "moo_number.h"
#include "moo_parse.h"
#include "moo_format.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define LUAI_MAXNUM2STR 48

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{
    if (Size < 8)
        return 0;

    // moo: the 8 bytes are the raw Q48.16 value, clamped into the domain [-INT64_MAX, INT64_MAX]
    int64_t num;
    memcpy(&num, Data, 8);
    if (num < MOO_NUMBER_MIN)
        num = MOO_NUMBER_MIN;
    if (num > MOO_NUMBER_MAX)
        num = MOO_NUMBER_MAX;

    char buf[LUAI_MAXNUM2STR];
    char* end = moo_numtostr(buf, num); // moo: the exact formatter, no libc
    if (end < buf || end >= buf + sizeof(buf))
    {
        fprintf(stderr, "moo-fuzz number: tostring overflow for %lld\n", (long long)num);
        fflush(stderr);
        abort();
    }

    *end = 0;

    // moo: tonumber strips one sign before the parser (VM/src/lobject.cpp luaO_str2d); the formatter writes the sign
    const char* text = buf;
    bool negative = false;
    if (*text == '-' || *text == '+')
        negative = *text++ == '-';

    fixed_t rec = 0;
    int status = moo_numparse(text, &rec); // moo: the exact parser, no strtod and no locale
    if (status == MOO_PARSE_OK && negative)
        rec = -rec;

    if (status != MOO_PARSE_OK || rec != num)
    {
        fprintf(stderr, "moo-fuzz number: round-trip %lld -> '%s' -> %d %lld\n", (long long)num, buf, status, (long long)rec);
        fflush(stderr);
        abort();
    }
    return 0;
}
