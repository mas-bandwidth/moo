// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: expression-temporary reuse, the compiler's half (MooTempReuse.h; docs/DESIGN.md "Expression-temporary reuse").
#include "MooTempReuse.h"

namespace Luau
{
namespace Compile
{

static bool isConstantExpr(AstExpr* node, const DenseHashMap<AstExpr*, Constant>& constants)
{
    const Constant* cv = constants.find(node);
    return cv && cv->type != Constant::Type_Unknown;
}

bool isTempProducer(AstExpr* node, const DenseHashMap<AstExpr*, Constant>& constants)
{
    for (;;)
    {
        // compileExpr emits a constant before it looks at the node's kind
        if (isConstantExpr(node, constants))
            return false;

        // compileExpr compiles these as the inner expression, into the same target
        if (AstExprGroup* group = node->as<AstExprGroup>())
            node = group->expr;
        else if (AstExprTypeAssertion* assertion = node->as<AstExprTypeAssertion>())
            node = assertion->expr;
        else
            break;
    }

    if (AstExprBinary* binary = node->as<AstExprBinary>())
        return binary->op == AstExprBinary::Add || binary->op == AstExprBinary::Sub || binary->op == AstExprBinary::Mul;

    if (AstExprUnary* unary = node->as<AstExprUnary>())
        return unary->op == AstExprUnary::Op::Minus;

    return false;
}

LuauOpcode tempOpcode(LuauOpcode op, bool tempB, bool tempC)
{
    switch (op)
    {
    case LOP_ADD:
        return tempB ? LOP_ADDTB : tempC ? LOP_ADDTC : op;
    case LOP_SUB:
        return tempB ? LOP_SUBTB : tempC ? LOP_SUBTC : op;
    case LOP_MUL:
        return tempB ? LOP_MULTB : tempC ? LOP_MULTC : op;
    case LOP_MULK:
        return tempB ? LOP_MULKTB : op;
    case LOP_MINUS:
        return tempB ? LOP_MINUST : op;
    default:
        return op;
    }
}

} // namespace Compile
} // namespace Luau
