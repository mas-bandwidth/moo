// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: the test harnesses are hosts, and a host's floats cross into the VM only through the boundary
// (moo.h). mooHostNumber rounds a host double by the boundary rule; out of range saturates and NaN gives 0,
// which is what a test that pushes such a value can expect of moo.
#pragma once

#include "lua.h"
#include "moo.h"

inline lua_Number mooHostNumber(double d)
{
    int64_t n = 0;
    int status = moo_fromdouble(d, &n);
    if (status == MOO_NUMBER_OK)
        return n;
    if (status == MOO_NUMBER_NAN)
        return 0;
    return d > 0 ? INT64_MAX : -INT64_MAX;
}
