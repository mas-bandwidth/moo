// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Ast.h"
#include "Luau/DenseHash.h"

#include "moo_number.h"

namespace Luau
{
namespace Compile
{

struct Constant;

// cost model: 8 bytes, where first byte is the baseline cost, and the next 7 bytes are discounts for when variable #i is constant
uint64_t modelCost(
    AstNode* root,
    AstLocal* const* vars,
    size_t varCount,
    const DenseHashMap<AstExprCall*, int>& builtins,
    const DenseHashMap<AstExpr*, Constant>& constants
);
// when additional data is not available, used to test the cost model
uint64_t modelCost(AstNode* root, AstLocal* const* vars, size_t varCount);

// cost is computed as B - sum(Di * Ci), where B is baseline cost, Di is the discount for each variable and Ci is 1 when variable #i is constant
int computeCost(uint64_t model, const bool* varsConst, size_t varCount);

// get loop trip count or -1 if we can't compute it precisely
// moo: the bounds are raw Q48.16 values (moo_number.h), with no double in the path
int getTripCount(fixed_t from, fixed_t to, fixed_t step);

} // namespace Compile
} // namespace Luau
