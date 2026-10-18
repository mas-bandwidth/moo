// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// moo: this upstream target called the analyzer's double format-string linter. moo's number is Q48.16 fixed point, so
// it now builds a C format spec from the input and exercises moo's exact formatter (moo_numformat), then checks the
// full %.16f expansion parses back to the same raw value (docs/DESIGN.md "string.format", "Number formatting").
#include "moo_number.h"
#include "moo_parse.h"
#include "moo_format.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size)
{
    if (Size < 9)
        return 0;

    // moo: the first 8 bytes are the raw Q48.16 value, clamped into the domain
    int64_t raw;
    memcpy(&raw, Data, 8);
    if (raw < MOO_NUMBER_MIN)
        raw = MOO_NUMBER_MIN;
    if (raw > MOO_NUMBER_MAX)
        raw = MOO_NUMBER_MAX;

    static const char kFlags[] = "-+ #0";
    char spec[32];
    int length = 0;
    for (int bit = 0; bit < 5 && Size > size_t(8 + bit); ++bit)
        if (Data[8 + bit] & 1)
            spec[length++] = kFlags[bit];

    // width 0 to 39, precision 0 to 24, both from the input
    int width = Size > 13 ? Data[13] % 40 : 0;
    int precision = Size > 14 ? Data[14] % 25 : 6;
    length += snprintf(spec + length, sizeof(spec) - size_t(length), "%d.%d", width, precision);
    if (length < 0 || size_t(length) >= sizeof(spec))
        return 0;

    static const char kConvs[] = "feEgG";
    char conv = kConvs[Data[8] % 5];

    char buf[512];
    int written = moo_numformat(buf, sizeof(buf), spec, conv, raw);
    if (written >= 0)
    {
        if (size_t(written) >= sizeof(buf) || buf[written] != 0)
        {
            fprintf(stderr, "moo-fuzz format: bad length %d for spec '%s' conv '%c'\n", written, spec, conv);
            fflush(stderr);
            abort();
        }

        // moo_numformat must refuse exactly one character less than it wrote
        if (moo_numformat(buf, size_t(written), spec, conv, raw) != -1)
        {
            fprintf(stderr, "moo-fuzz format: accepted cap %d for spec '%s' conv '%c'\n", written, spec, conv);
            fflush(stderr);
            abort();
        }
    }

    // docs/DESIGN.md "Number formatting": every Q48.16 value's exact expansion is at most 16 fraction digits;
    // string.format("%.16f", x) prints it and the literal parser reads it back exactly.
    if (moo_numformat(buf, sizeof(buf), ".16", 'f', raw) < 0)
    {
        fprintf(stderr, "moo-fuzz format: %%.16f did not fit for %lld\n", (long long)raw);
        fflush(stderr);
        abort();
    }

    // moo: tonumber strips one sign before the parser (VM/src/lobject.cpp luaO_str2d); the formatter writes the sign
    const char* text = buf;
    bool negative = false;
    if (*text == '-' || *text == '+')
        negative = *text++ == '-';

    fixed_t back = 0;
    int status = moo_numparse(text, &back);
    if (status == MOO_PARSE_OK && negative)
        back = -back;
    if (status != MOO_PARSE_OK || back != raw)
    {
        fprintf(stderr, "moo-fuzz format: %lld printed as '%s', parsed back as %lld\n", (long long)raw, buf, (long long)back);
        fflush(stderr);
        abort();
    }

    return 0;
}
