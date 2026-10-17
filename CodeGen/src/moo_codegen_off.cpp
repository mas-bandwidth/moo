// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: native code generation is excluded from the build (MOO_CODEGEN, docs/DESIGN.md "Native
// codegen"): its code computes on doubles and moo's number is fixed point, and code that computes
// wrong answers is worse than none. This file is the whole of Luau.CodeGen in moo. Every entry point
// says that code generation is not supported, so callers take the interpreter path they already have.
#include "Luau/CodeGen.h"
#include "luacodegen.h"
#include "Luau/BytecodeSummary.h"
#include "Luau/Common.h"

// flags the tests and the CLI set by name; they change nothing while code generation is excluded
LUAU_FASTINTVARIABLE(CodegenHeuristicsInstructionLimit, 1'048'576)
LUAU_FASTFLAGVARIABLE(LuauCodegenBufferInteger)
LUAU_FASTFLAGVARIABLE(DebugLuauAbortingChecks)

int luau_codegen_supported(void)
{
    return 0;
}

void luau_codegen_create(lua_State* L)
{
    (void)L;
}

void luau_codegen_compile(lua_State* L, int idx)
{
    (void)L;
    (void)idx;
}

namespace Luau
{
namespace CodeGen
{

std::string toString(const CodeGenCompilationResult& result)
{
    (void)result;
    return "native code generation is excluded from moo";
}

bool isSupported()
{
    return false;
}

void create(lua_State* L)
{
    (void)L;
}

void create(lua_State* L, AllocationCallback* allocationCallback, void* allocationCallbackContext)
{
    (void)L;
    (void)allocationCallback;
    (void)allocationCallbackContext;
}

void create(lua_State* L, size_t blockSize, size_t maxTotalSize, AllocationCallback* allocationCallback, void* allocationCallbackContext)
{
    (void)L;
    (void)blockSize;
    (void)maxTotalSize;
    (void)allocationCallback;
    (void)allocationCallbackContext;
}

void setNativeExecutionEnabled(lua_State* L, bool enabled)
{
    (void)L;
    (void)enabled;
}

void setUserdataRemapper(lua_State* L, void* context, UserdataRemapperCallback cb)
{
    (void)L;
    (void)context;
    (void)cb;
}

static CompilationResult notInitialized()
{
    CompilationResult r;
    r.result = CodeGenCompilationResult::CodeGenNotInitialized;
    return r;
}

CompilationResult compile(lua_State* L, int idx, unsigned int flags, CompilationStats* stats)
{
    (void)L, (void)idx, (void)flags, (void)stats;
    return notInitialized();
}

CompilationResult compile(const ModuleId& moduleId, lua_State* L, int idx, unsigned int flags, CompilationStats* stats)
{
    (void)moduleId, (void)L, (void)idx, (void)flags, (void)stats;
    return notInitialized();
}

CompilationResult compile(lua_State* L, int idx, const CompilationOptions& options, CompilationStats* stats)
{
    (void)L, (void)idx, (void)options, (void)stats;
    return notInitialized();
}

CompilationResult compile(const ModuleId& moduleId, lua_State* L, int idx, const CompilationOptions& options, CompilationStats* stats)
{
    (void)moduleId, (void)L, (void)idx, (void)options, (void)stats;
    return notInitialized();
}

std::string getAssembly(lua_State* L, int idx, AssemblyOptions options, LoweringStats* stats)
{
    (void)L, (void)idx, (void)options, (void)stats;
    return "";
}

void setPerfLog(void* context, PerfLogFn logFn)
{
    (void)context;
    (void)logFn;
}

std::vector<FunctionBytecodeSummary> summarizeBytecode(lua_State* L, int idx, unsigned nestingLimit)
{
    (void)L, (void)idx, (void)nestingLimit;
    return {};
}

} // namespace CodeGen
} // namespace Luau
