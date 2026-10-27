// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// What moo removes from Luau: the os, integer and vector libraries, the float vector in the C API,
// the compiler's vector builtins and constants (docs/DESIGN.md "Analyzer", "vec3, quat and transform").

#include "lua.h"
#include "lualib.h"
#include "Luau/BytecodeBuilder.h"
#include "Luau/Compiler.h"
#include "doctest.h"
#include "MooHost.h"
#include "moo.h"
#include "ScopedFlags.h"

#include <cstring>
#include <string>

TEST_SUITE_BEGIN("removed");

// moo: DESIGN.md "vec3, quat and transform" and DETERMINISM.md 17: the compiler no longer recognises vector.*
// (or vectorLib/vectorCtor options) as builtins and never folds a vector constant.

namespace
{

void vectorLibraryConstantCb(const char* library, const char* member, Luau::CompileConstant* constant)
{
    if (strcmp(library, "vector") == 0 || strcmp(library, "Vector3") == 0)
    {
        if (strcmp(member, "zero") == 0)
            Luau::setCompileConstantVector(constant, 0.0f, 0.0f, 0.0f, 0.0f);
        else if (strcmp(member, "one") == 0)
            Luau::setCompileConstantVector(constant, 1.0f, 1.0f, 1.0f, 0.0f);
        else if (strcmp(member, "oned") == 0)
            Luau::setCompileConstantVectord(constant, 1.0, 2.0, 3.0, 0.0);
    }
}

std::string dumpAtOpt(const char* source, int optLevel, bool withVectorOptions = false)
{
    Luau::BytecodeBuilder bcb;
    bcb.setDumpFlags(Luau::BytecodeBuilder::Dump_Code | Luau::BytecodeBuilder::Dump_Constants);

    Luau::CompileOptions options;
    options.optimizationLevel = optLevel;
    if (withVectorOptions)
    {
        options.vectorLib = "Vector3";
        options.vectorCtor = "new";
        static const char* kLibs[] = {"vector", "Vector3", nullptr};
        options.librariesWithKnownMembers = kLibs;
        options.libraryMemberConstantCb = vectorLibraryConstantCb;
    }

    Luau::compileOrThrow(bcb, source, options);
    return bcb.dumpFunction(0);
}

bool contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("RemovedVectorHasNoCompilerBuiltinOrConstant")
{
    // return vector.create(1, 2, 3) compiles to GETGLOBAL/GETIMPORT + CALL with no FASTCALL and no vector constant.
    for (int opt = 0; opt <= 2; ++opt)
    {
        std::string dump = dumpAtOpt("return vector.create(1, 2, 3)", opt, true);
        CHECK_MESSAGE((contains(dump, "GETIMPORT") || contains(dump, "GETGLOBAL")), "opt=" << opt << ":\n" << dump);
        CHECK_MESSAGE(contains(dump, "CALL"), "opt=" << opt << ":\n" << dump);
        CHECK_MESSAGE(!contains(dump, "FASTCALL"), "opt=" << opt << ":\n" << dump);
        CHECK_MESSAGE(!contains(dump, "1, 2, 3"), "opt=" << opt << ":\n" << dump);
    }

    // Every vector.* library entry compiles as an ordinary call without FASTCALL.
    const char* vectorCalls[] = {
        "return vector.create(1, 2, 3)",
        "local a = ... return vector.create(a, a, a)",
        "local a = ... return vector.magnitude(a)",
        "local a = ... return vector.normalize(a)",
        "local a, b = ... return vector.cross(a, b)",
        "local a, b = ... return vector.dot(a, b)",
        "local a = ... return vector.floor(a)",
        "local a = ... return vector.ceil(a)",
        "local a = ... return vector.abs(a)",
        "local a = ... return vector.sign(a)",
        "local a, b, c = ... return vector.clamp(a, b, c)",
        "local a, b = ... return vector.min(a, b)",
        "local a, b = ... return vector.max(a, b)",
        "local a, b, t = ... return vector.lerp(a, b, t)",
    };

    for (const char* call : vectorCalls)
    {
        for (int opt = 0; opt <= 2; ++opt)
        {
            std::string dump = dumpAtOpt(call, opt, true);
            CHECK_MESSAGE((contains(dump, "GETIMPORT") || contains(dump, "GETGLOBAL")), call << " opt=" << opt << ":\n" << dump);
            CHECK_MESSAGE(!contains(dump, "FASTCALL"), call << " opt=" << opt << ":\n" << dump);
        }
    }

    //CompileOptions vectorLib / vectorCtor and setCompileConstantVector / setCompileConstantVectord produce no builtin and no vector constant.
    for (int opt = 0; opt <= 2; ++opt)
    {
        std::string ctorConst = dumpAtOpt("return Vector3.new(1, 2, 3)", opt, true);
        CHECK_MESSAGE((contains(ctorConst, "GETIMPORT") || contains(ctorConst, "GETGLOBAL")), "opt=" << opt << ":\n" << ctorConst);
        CHECK_MESSAGE(!contains(ctorConst, "FASTCALL"), "opt=" << opt << ":\n" << ctorConst);
        CHECK_MESSAGE(!contains(ctorConst, "1, 2, 3"), "opt=" << opt << ":\n" << ctorConst);

        std::string ctorVar = dumpAtOpt("local a, b, c = ... return Vector3.new(a, b, c)", opt, true);
        CHECK_MESSAGE((contains(ctorVar, "GETIMPORT") || contains(ctorVar, "GETGLOBAL")), "opt=" << opt << ":\n" << ctorVar);
        CHECK_MESSAGE(!contains(ctorVar, "FASTCALL"), "opt=" << opt << ":\n" << ctorVar);

        std::string fields = dumpAtOpt("return vector.zero, vector.one, Vector3.one, Vector3.oned", opt, true);
        CHECK_MESSAGE((contains(fields, "GETIMPORT") || contains(fields, "GETGLOBAL")), "opt=" << opt << ":\n" << fields);
        CHECK_MESSAGE(!contains(fields, "LOADK"), "opt=" << opt << ":\n" << fields);

        std::string arith = dumpAtOpt("local a, b = vector.one, Vector3.one return a + b, a - b, a * 2, a / 2, a // 2, -a, a.x, a == b", opt, true);
        CHECK_MESSAGE(contains(arith, "ADD"), "opt=" << opt << ":\n" << arith);
        CHECK_MESSAGE(contains(arith, "SUB"), "opt=" << opt << ":\n" << arith);
        CHECK_MESSAGE(contains(arith, "MUL"), "opt=" << opt << ":\n" << arith);
        CHECK_MESSAGE(contains(arith, "DIV"), "opt=" << opt << ":\n" << arith);
        CHECK_MESSAGE(contains(arith, "IDIV"), "opt=" << opt << ":\n" << arith);
        CHECK_MESSAGE(contains(arith, "MINUS"), "opt=" << opt << ":\n" << arith);
        CHECK_MESSAGE(contains(arith, "GETTABLEKS"), "opt=" << opt << ":\n" << arith);
    }
}

TEST_CASE("RemovedScriptInSandbox")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    luaL_sandbox(L);
    luaL_sandboxthread(L);

    CHECK_EQ(mooRunFile(L, "spec/removed.luau"), "OK");

    lua_close(L);
}

// The analyzer's builtin definitions drop os, vector and integer (docs/DESIGN.md "Analyzer");
// DONE-WHEN runs build/moo-analyze on vector.create and expects an unknown global. Moo.Test links
// the VM and the compiler, not the analyzer, so these cases pin the VM side of that seam: the
// globals the analyzer must not declare are absent from the VM in the same build.

// DESIGN.md "Analyzer": the builtin definitions drop os and the integer library; DETERMINISM.md
// "Status" 9 says the os library is deleted. Neither name resolves in the VM.
TEST_CASE("RemovedOsAndIntegerAreNotGlobals")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    CHECK(mooRun(L, "return rawget(_G, 'os') == nil") == "true");
    CHECK(mooRun(L, "return rawget(_G, 'integer') == nil") == "true");

    lua_close(L);
}

// moo: DESIGN.md "Analyzer": both analyzer and VM omit the vector global after moo-l3-libs-out (the library is out in layer 3).
TEST_CASE("RemovedVectorIsNotAGlobal")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);

    CHECK(mooRun(L, "return rawget(_G, 'vector') == nil") == "true");

    lua_close(L);
}

// moo-l3-libs-out: luaL_openlibs opens no vector and no integer library, whatever the integer flag says
// (docs/DESIGN.md, "Layer 3 as built").
LUAU_FASTFLAG(LuauIntegerLibrary)
LUAU_FASTFLAG(LuauIntegerType2)

TEST_CASE("RemovedOpenlibsOpensNoVectorOrInteger")
{
    // Even with the integer flag and type on, the integer library stays out; vector is always out.
    ScopedFastFlag integerLibrary{FFlag::LuauIntegerLibrary, true};
    ScopedFastFlag integerType{FFlag::LuauIntegerType2, true};

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    lua_getglobal(L, "vector");
    CHECK(lua_isnil(L, -1));
    lua_pop(L, 1);

    lua_getglobal(L, "integer");
    CHECK(lua_isnil(L, -1));
    lua_pop(L, 1);

    lua_close(L);
}

// moo: layer 3 removes the float vector from the C API (docs/DESIGN.md "vec3, quat and transform");
// luaL_openlibs and luaL_sandbox leave no vector-valued field in the VM's globals.

namespace
{

// Counts vector values in the table at index and, one level down, in its table fields.
// The depth limit keeps a self-reference such as _G._G from cycling.
int countVectorFields(lua_State* L, int index, int depth)
{
    int count = 0;

    lua_pushvalue(L, index);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0)
    {
        if (lua_type(L, -1) == LUA_TVECTOR)
            count++;
        else if (depth > 0 && lua_istable(L, -1))
            count += countVectorFields(L, -1, depth - 1);

        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    return count;
}

void checkNoVectorInGlobals(lua_State* L)
{
    CHECK_EQ(countVectorFields(L, LUA_GLOBALSINDEX, 1), 0);

    lua_getglobal(L, "vector");
    CHECK(lua_isnil(L, -1));
    lua_pop(L, 1);
}

} // namespace

TEST_CASE("RemovedOpenlibsLeavesNoVectorValue")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    luaL_openlibs(L);
    checkNoVectorInGlobals(L);

    lua_close(L);
}

TEST_CASE("RemovedSandboxLeavesNoVectorValue")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    luaL_openlibs(L);
    luaL_sandbox(L);
    checkNoVectorInGlobals(L);

    lua_close(L);
}

// moo: layer 3 has no vector VM value; ordinary opcode families continue to operate on fixed numbers and reference values.

TEST_CASE("RemovedScript")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);

    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    CHECK(mooRunFile(L, "spec/removed.luau") == "OK");

    lua_close(L);
}

TEST_SUITE_END();
