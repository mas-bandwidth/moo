// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// Layer 4: the temporary double bridge is deleted (docs/DESIGN.md "Layer 3 as built", "TEMPORARY bridges";
// docs/DETERMINISM.md item 7). The two helpers moo_numbridgefromdouble / moo_numbridgetodouble no longer
// exist; the remaining conversions are the permanent boundary rule (moo_number.h moo_numfromdouble,
// moo_numtodoublebits) and these cases pin the values MOO.Test can reach without the bridge.
#include "moo_number.h"
#include "lgc.h"

#include "doctest.h"

#include <limits>
#include <stdint.h>
#include <string.h>
#include <string>

TEST_SUITE_BEGIN("moo-l4-bridge-removed");

namespace
{

constexpr int64_t kOne = 65536LL;
constexpr int64_t kMax = INT64_MAX;
constexpr int64_t kMin = -INT64_MAX;

} // namespace

TEST_CASE("BridgeBoundaryOutValues")
{
    // moo_numtodouble is the exact value raw / 2^16; 2^-16 is a power of two, so these are exact
    CHECK(moo_numtodouble(0) == 0.0);
    CHECK(moo_numtodouble(1) == 1.0 / 65536.0);
    CHECK(moo_numtodouble(-1) == -1.0 / 65536.0);
    CHECK(moo_numtodouble(kOne) == 1.0);
    CHECK(moo_numtodouble(-98304) == -1.5);
    CHECK(moo_numtodouble(kMax) == double(kMax) / 65536.0);
    CHECK(moo_numtodouble(kMin) == double(kMin) / 65536.0);
}

TEST_CASE("BridgeBoundaryInValues")
{
    // the boundary rule into the VM: nearest Q48.16, ties to even
    CHECK(moo_numfromdoublevalue(0.0) == 0);
    CHECK(moo_numfromdoublevalue(1.0) == kOne);
    CHECK(moo_numfromdoublevalue(-1.5) == -98304);
    CHECK(moo_numfromdoublevalue(0.1) == 6554); // 0.1 * 65536 = 6553.6, nearest is 6554
    CHECK(moo_numfromdoublevalue(-0.1) == -6554);
    CHECK(moo_numfromdoublevalue(1.5 / 65536.0) == 2);  // a tie, even
    CHECK(moo_numfromdoublevalue(2.5 / 65536.0) == 2);  // a tie, even
    CHECK(moo_numfromdoublevalue(3.5 / 65536.0) == 4);  // a tie, even
    CHECK(moo_numfromdoublevalue(140737488355328.0) == kMax);  // 2^47 is out of range
    CHECK(moo_numfromdoublevalue(-140737488355328.0) == kMin); // -2^47 is out of range
}

TEST_CASE("BridgeBoundaryNonFinite")
{
    // NaN gives 0, an infinity saturates (the boundary contract in moo.h)
    CHECK(moo_numfromdoublevalue(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(moo_numfromdoublevalue(std::numeric_limits<double>::infinity()) == kMax);
    CHECK(moo_numfromdoublevalue(-std::numeric_limits<double>::infinity()) == kMin);
}

TEST_CASE("BridgeHeapDumpUsesExactFormatter")
{
    // moo: enumerate a live table's collectable values through lgcdebug.cpp's enumtable path.
    // The fractional edge catches a regression to %.14g: that would print 0.10000610351563.
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, -98304);
    lua_pushstring(L, "negative");
    lua_settable(L, -3);
    lua_pushnumber(L, 6554);
    lua_pushstring(L, "fractional");
    lua_settable(L, -3);

    struct EdgeNames
    {
        bool negative = false;
        bool fractional = false;
    } names;
    luaC_enumheap(
        L, &names,
        [](void*, void*, uint8_t, uint8_t, size_t, const char*) {},
        [](void* context, void*, void*, const char* name)
        {
            EdgeNames& seen = *static_cast<EdgeNames*>(context);
            if (name && strcmp(name, "-1.5") == 0)
                seen.negative = true;
            if (name && strcmp(name, "0.1") == 0)
                seen.fractional = true;
        });
    CHECK(names.negative);
    CHECK(names.fractional);
    lua_close(L);
}
