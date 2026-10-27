// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "BuiltinFolding.h"

#include "Luau/Bytecode.h"

#include <cstddef>

namespace Luau
{
namespace Compile
{

// moo: builtin calls do not fold: each keeps its run-time definition, so a folded value can never differ from the
// VM's. The upstream float and bit32 folding is gone; the seam the compiler calls answers "not a constant".
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
