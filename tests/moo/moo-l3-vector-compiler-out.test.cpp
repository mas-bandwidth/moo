// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: DESIGN.md "vec3, quat and transform" and DETERMINISM.md 17: the compiler no longer recognises vector.*
// (or vectorLib/vectorCtor options) as builtins and never folds a vector constant.
#include "lua.h"
#include "lualib.h"

#include "Luau/BytecodeBuilder.h"
#include "Luau/Compiler.h"

#include "doctest.h"
#include "MooHost.h"

#include <cstring>
#include <string>

TEST_SUITE_BEGIN("moo-l3-vector-compiler-out");

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

TEST_CASE("VectorCompilerOutNoBuiltinOrConstant")
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

TEST_CASE("VectorCompilerOutScriptInSandbox")
{
    lua_State* L = luaL_newstate();
    REQUIRE(L != nullptr);
    luaL_openlibs(L);
    mooOpenTestHelpers(L);
    luaL_sandbox(L);
    luaL_sandboxthread(L);

    CHECK_EQ(mooRunFile(L, "l3/moo-l3-vector-compiler-out.luau"), "OK");

    lua_close(L);
}

TEST_SUITE_END();
