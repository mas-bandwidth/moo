// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "BuiltinFolding.h"

#include "Luau/Bytecode.h"

#include <cstddef>

namespace Luau
{
namespace Compile
{

// moo: builtins do not fold until the math library is rebuilt on fixed (docs/DESIGN.md, "math library"); the build
// machine's libm and the run time's must not disagree, so a builtin call keeps its run-time definition. Number
// constants are the raw Q48.16 value (docs/DESIGN.md "Layer 3 as built", "The compiler"), and there is no double
// here to read. The upstream float/bit32 folding bodies are gone; what this file must keep is the seam the compiler
// calls, and it answers "not a constant".
Constant foldBuiltin(AstNameTable& stringTable, int bfid, const Constant* args, size_t count, bool vectorDoublePrecision)
{
    (void)stringTable, (void)bfid, (void)args, (void)count, (void)vectorDoublePrecision;
    return Constant();
}

Constant foldBuiltinMath(AstName index)
{
    // moo: math.pi and math.huge come from the VM at run time (huge is the largest number, not infinity)
    (void)index;
    return Constant();
}

} // namespace Compile
} // namespace Luau
