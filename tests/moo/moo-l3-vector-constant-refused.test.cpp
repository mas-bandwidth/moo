// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// The moo bytecode has no vector constant (docs/layer3/moo-l3-vector-constant-refused.md; docs/DESIGN.md
// "vec3, quat and transform"): the builder never writes LBC_CONSTANT_VECTOR or LBC_CONSTANT_VECTORD, and the VM
// refuses a hand-built chunk holding either tag by name.
#include "lua.h"
#include "lualib.h"

#include "Luau/Bytecode.h"

#include "doctest.h"
#include "MooHost.h"

#include <string>

TEST_SUITE_BEGIN("moo-l3-vector-constant-refused");

namespace
{

// The body of a hand-built chunk in the format of Luau version 9 (LBC_VERSION_TARGET at BASE), types version 3: no
// strings, no userdata types, one proto with no instructions, holding the one constant `constant` (its tag and bytes).
// The constant is read before any instruction runs, so the refusal fires on load.
std::string chunkBody(const std::string& constant)
{
    std::string s;
    s += char(9); // the Luau version
    s += char(3); // types version
    s += char(0); // string count
    s += char(0); // userdata type remapping: end
    s += char(1); // proto count

    s += char(1); // maxstacksize
    s += char(0); // numparams
    s += char(0); // nups
    s += char(0); // is_vararg
    s += char(0); // flags
    s += char(0); // type info size

    s += char(0); // sizecode: no instructions, the constant is still read
    s += char(1); // sizek
    s += constant;

    s += char(0); // sizep
    s += char(0); // linedefined
    s += char(0); // debugname
    s += char(0); // lineinfo
    s += char(0); // debuginfo

    s += char(0); // main proto
    return s;
}

std::string mooBlob(const std::string& body)
{
    return std::string(1, char(LBC_MOO_VERSION_FLAG | LBC_MOO_VERSION_TARGET)) + body;
}

// luau_load: "" on success, else the load error
std::string loadError(lua_State* L, const std::string& blob)
{
    if (luau_load(L, "=moo", blob.data(), blob.size(), 0) != 0)
    {
        std::string err = lua_tostring(L, -1);
        lua_pop(L, 1);
        return err;
    }
    lua_pop(L, 1);
    return "";
}

} // namespace

// DESIGN.md "vec3, quat and transform": the VM refuses a chunk holding a float vector constant by name.
TEST_CASE("MooBytecodeRefusesVectorConstantF")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    std::string constant(1, char(LBC_CONSTANT_VECTOR));
    constant.append(16, '\0'); // four float32 payload bytes; the refusal fires before they are read

    CHECK(loadError(L, mooBlob(chunkBody(constant))) == "moo: moo bytecode has no vector constant");

    lua_close(L);
}

// DESIGN.md "vec3, quat and transform": the VM refuses a chunk holding a double vector constant by name.
TEST_CASE("MooBytecodeRefusesVectorConstantD")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    std::string constant(1, char(LBC_CONSTANT_VECTORD));
    constant.append(32, '\0'); // four float64 payload bytes; the refusal fires before they are read

    CHECK(loadError(L, mooBlob(chunkBody(constant))) == "moo: moo bytecode has no vector constant");

    lua_close(L);
}

// The same hand-built chunk with an ordinary number constant still loads: the refusal is for the vector tags alone.
TEST_CASE("MooBytecodeVectorRefusalIsSpecific")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    std::string constant(1, char(LBC_CONSTANT_NUMBER));
    constant.append(8, '\0'); // raw Q48.16 value 0

    CHECK(loadError(L, mooBlob(chunkBody(constant))) == "");

    lua_close(L);
}

TEST_SUITE_END();
