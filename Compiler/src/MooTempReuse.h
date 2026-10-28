// This file is part of moo, a fork of the Luau programming language; moo's changes are licensed under MIT License; see LICENSE-moo.txt for details
//
// moo: expression-temporary reuse, the compiler's half (docs/DESIGN.md "Expression-temporary reuse"; tla/TempReuse.tla).
//
// The rule, over Compiler.cpp's expression compilation: in compileExprBinary (Add, Sub, Mul, every operand form),
// compileExprUnary (Minus) and compileStatCompoundAssign (+=, -=, *=), an operand is an expression temporary when
//   1. its expression, under any parentheses and type assertions, is an Add, Sub or Mul AstExprBinary or a Minus
//      AstExprUnary that is not a compile-time constant (isTempProducer), so the code that fills it is one
//      arithmetic op writing its result into the operand's register, and
//   2. compileExprAuto returned the register it allocated for that operand: the register is regTop at the call, never
//      a local's register (locals are always below regTop) and never a register the move elision substituted.
// Such a register is written by its producer and read by this op only: the compiler names no temporary, so no
// local, upvalue, closure, table field, call argument or return value refers to it. The op that reads it is then
// emitted as its temporary variant (tempOpcode); at -O0 nothing is marked.
#pragma once

#include "Luau/Ast.h"
#include "Luau/Bytecode.h"
#include "Luau/DenseHash.h"

#include "ConstantFolding.h"

namespace Luau
{
namespace Compile
{

// condition 1 above: node is an arithmetic expression whose code is one arithmetic op into its target register
bool isTempProducer(AstExpr* node, const DenseHashMap<AstExpr*, Constant>& constants);

// the temporary variant of op when its B (tempB) or C (tempC) operand is an expression temporary, or op itself:
// ADD, SUB, MUL -> ADDTB/ADDTC, SUBTB/SUBTC, MULTB/MULTC (B first when both are); MULK -> MULKTB; MINUS -> MINUST
LuauOpcode tempOpcode(LuauOpcode op, bool tempB, bool tempC);

} // namespace Compile
} // namespace Luau
