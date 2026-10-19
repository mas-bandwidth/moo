// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "ConstantFolding.h"

#include "BuiltinFolding.h"
#include "Utils.h"
#include "Luau/Bytecode.h"
#include "Luau/Lexer.h"

#include "moo_number.h"
#include "moo_pow.h"

#include <vector>
#include <math.h>

LUAU_FASTFLAG(LuauIntegerType2)
LUAU_FASTFLAG(LuauExperimentalIfLocalSyntax)
LUAU_FASTFLAGVARIABLE(LuauCompileNoFoldVectorEqW)

namespace Luau
{
namespace Compile
{

constexpr size_t kConstantFoldStringLimit = 4096;

// moo: number constants fold with the VM's own operations (moo_number.h), so a folded constant and the same
// computation at run time are the same bits. A number constant is the raw Q48.16 value: the operands are read
// raw, and every in-range result folds, with no double in the path (docs/DESIGN.md "Layer 3 as built",
// "The compiler"). Division by zero is not folded: the VM raises the named error at run time.
static bool mooOperands(const Constant& la, const Constant& ra, fixed_t* a, fixed_t* b)
{
    *a = la.valueNumber;
    *b = ra.valueNumber;
    return true;
}

static bool mooResult(Constant& result, fixed_t r)
{
    result.type = Constant::Type_Number;
    result.valueNumber = r;
    return true;
}

#if MOO_OVERFLOW_ERRORS
// moo: a checked build keeps an operation that saturates for run time, where it raises "number overflow
// in '<op>'" instead of carrying a folded saturated constant (docs/DESIGN.md, "Overflow")
static bool mooSaturates(AstExprBinary::Op op, fixed_t a, fixed_t b)
{
    switch (op)
    {
    case AstExprBinary::Add:
        return moo_numaddsaturates(a, b);
    case AstExprBinary::Sub:
        return moo_numsubsaturates(a, b);
    case AstExprBinary::Mul:
        return moo_nummulsaturates(a, b);
    case AstExprBinary::Div:
        return moo_numdivsaturates(a, b);
    case AstExprBinary::FloorDiv:
        return moo_numidivsaturates(a, b);
    default:
        return false;
    }
}
#endif

static void mooFold(Constant& result, AstExprBinary::Op op, const Constant& la, const Constant& ra)
{
    fixed_t a, b;
    if (!mooOperands(la, ra, &a, &b))
        return;

#if MOO_OVERFLOW_ERRORS
    // moo: docs/DESIGN.md, "Overflow"
    if (mooSaturates(op, a, b))
        return;
#endif

    switch (op)
    {
    case AstExprBinary::Add:
        mooResult(result, moo_numadd(a, b));
        break;
    case AstExprBinary::Sub:
        mooResult(result, moo_numsub(a, b));
        break;
    case AstExprBinary::Mul:
        mooResult(result, moo_nummul(a, b));
        break;
    case AstExprBinary::Div:
        if (b != 0)
            mooResult(result, moo_numdiv(a, b));
        break;
    case AstExprBinary::FloorDiv:
        if (b != 0)
            mooResult(result, moo_numidiv(a, b));
        break;
    case AstExprBinary::Mod:
        if (b != 0)
            mooResult(result, moo_nummod(a, b));
        break;
    case AstExprBinary::Pow:
    {
        // moo: ^ with an integer exponent folds through moo_pow_int, so the folded constant is the run time's bits
        // (docs/DESIGN.md, "Layer 3 as built", "The compiler"; "math library", "pow and ^"). A fractional exponent
        // stays unfolded; division by zero and a saturated power are never folded, because the run time raises the
        // named errors ("division by zero" in every build, "number overflow in '^'" in a checked build).
        if (!moo_numisinteger(b))
            break;
        fixed_t r = 0;
        int status = moo_pow_int(a, b >> MOO_NUMBER_FRACTION_BITS, &r);
        if (status == MOO_POW_OK)
            mooResult(result, r);
        // MOO_POW_SATURATED and MOO_POW_DIVISION_BY_ZERO: leave them for run time (docs/DESIGN.md,
        // "Overflow", "Division by zero")
        break;
    }
    case AstExprBinary::CompareLt:
        result.type = Constant::Type_Boolean;
        result.valueBoolean = a < b;
        break;
    case AstExprBinary::CompareLe:
        result.type = Constant::Type_Boolean;
        result.valueBoolean = a <= b;
        break;
    case AstExprBinary::CompareGt:
        result.type = Constant::Type_Boolean;
        result.valueBoolean = a > b;
        break;
    case AstExprBinary::CompareGe:
        result.type = Constant::Type_Boolean;
        result.valueBoolean = a >= b;
        break;
    default:
        break; // the rest are not number operations
    }
}

static bool constantsEqual(const Constant& la, const Constant& ra)
{
    LUAU_ASSERT(la.type != Constant::Type_Unknown && ra.type != Constant::Type_Unknown);

    switch (la.type)
    {
    case Constant::Type_Nil:
        return ra.type == Constant::Type_Nil;

    case Constant::Type_Boolean:
        return ra.type == Constant::Type_Boolean && la.valueBoolean == ra.valueBoolean;

    case Constant::Type_Number:
    {
        // moo: equal as raw Q48.16 values (docs/DESIGN.md "Layer 3 as built", "The compiler")
        return ra.type == Constant::Type_Number && la.valueNumber == ra.valueNumber;
    }

    // moo: the float vector is removed (docs/DESIGN.md "vec3, quat and transform").

    case Constant::Type_String:
        return ra.type == Constant::Type_String && la.stringLength == ra.stringLength && memcmp(la.valueString, ra.valueString, la.stringLength) == 0;

    case Constant::Type_Table:
        return ra.type == Constant::Type_Table && la.valueTable == ra.valueTable;

    case Constant::Type_Integer:
        if (FFlag::LuauIntegerType2)
            return ra.type == Constant::Type_Integer && la.valueInteger64 == ra.valueInteger64;
        [[fallthrough]];

    default:
        LUAU_ASSERT(!"Unexpected constant type in comparison");
        return false;
    }
}

// moo: the float vector is removed (docs/DESIGN.md "vec3, quat and transform").

static void foldUnary(Constant& result, AstExprUnary::Op op, const Constant& arg)
{
    switch (op)
    {
    case AstExprUnary::Op::Not:
        if (arg.type != Constant::Type_Unknown)
        {
            result.type = Constant::Type_Boolean;
            result.valueBoolean = !arg.isTruthful();
        }
        break;

    case AstExprUnary::Op::Minus:
        if (arg.type == Constant::Type_Number)
        {
            result.type = Constant::Type_Number;
            result.valueNumber = moo_numunm(arg.valueNumber);
        }
        // moo: the float vector is removed; unary minus never folds a vector (docs/DESIGN.md "vec3, quat and transform").
        break;

    case AstExprUnary::Op::Len:
        if (arg.type == Constant::Type_String)
        {
            result.type = Constant::Type_Number;
            result.valueNumber = moo_numfromint(int64_t(arg.stringLength));
        }
        break;

    default:
        LUAU_ASSERT(!"Unexpected unary operation");
    }
}

static void foldBinary(Constant& result, AstExprBinary::Op op, const Constant& la, const Constant& ra, AstNameTable& stringTable)
{
    if (la.type == Constant::Type_Number && ra.type == Constant::Type_Number && op != AstExprBinary::Concat &&
        op != AstExprBinary::CompareEq && op != AstExprBinary::CompareNe && op != AstExprBinary::And && op != AstExprBinary::Or)
    {
        mooFold(result, op, la, ra);
        return;
    }

    switch (op)
    {
    // moo: the float vector is removed, and number arithmetic folds only through mooFold above; binary operations
    // never fold a vector (docs/DESIGN.md "vec3, quat and transform").
    case AstExprBinary::Concat:
        if (la.type == Constant::Type_String && ra.type == Constant::Type_String && la.stringLength + ra.stringLength <= kConstantFoldStringLimit)
        {
            result.type = Constant::Type_String;
            result.stringLength = la.stringLength + ra.stringLength;
            if (la.stringLength == 0)
                result.valueString = ra.valueString;
            else if (ra.stringLength == 0)
                result.valueString = la.valueString;
            else
            {
                std::string tmp;
                tmp.reserve(result.stringLength + 1);
                tmp.append(la.valueString, la.stringLength);
                tmp.append(ra.valueString, ra.stringLength);
                AstName name = stringTable.getOrAdd(tmp.c_str(), result.stringLength);
                result.valueString = name.value;
            }
        }
        break;

    case AstExprBinary::CompareNe:
        if (la.type != Constant::Type_Unknown && ra.type != Constant::Type_Unknown)
        {
            result.type = Constant::Type_Boolean;
            result.valueBoolean = !constantsEqual(la, ra);
        }
        break;

    case AstExprBinary::CompareEq:
        if (la.type != Constant::Type_Unknown && ra.type != Constant::Type_Unknown)
        {
            result.type = Constant::Type_Boolean;
            result.valueBoolean = constantsEqual(la, ra);
        }
        break;

    case AstExprBinary::And:
        if (la.type != Constant::Type_Unknown)
        {
            result = la.isTruthful() ? ra : la;
        }
        break;

    case AstExprBinary::Or:
        if (la.type != Constant::Type_Unknown)
        {
            result = la.isTruthful() ? la : ra;
        }
        break;

    default:
        LUAU_ASSERT(!"Unexpected binary operation");
    }
}

static void foldInterpString(Constant& result, AstExprInterpString* expr, DenseHashMap<AstExpr*, Constant>& constants, AstNameTable& stringTable)
{
    LUAU_ASSERT(expr->strings.size == expr->expressions.size + 1);
    size_t resultLength = 0;
    for (size_t index = 0; index < expr->strings.size; ++index)
    {
        resultLength += expr->strings.data[index].size;
        if (index < expr->expressions.size)
        {
            const Constant* c = constants.find(expr->expressions.data[index]);
            LUAU_ASSERT(c != nullptr && c->type == Constant::Type::Type_String);
            resultLength += c->stringLength;
        }
    }

    if (resultLength > kConstantFoldStringLimit)
        return;

    result.type = Constant::Type_String;
    result.stringLength = unsigned(resultLength);

    if (resultLength == 0)
    {
        result.valueString = "";
        return;
    }

    std::string tmp;
    tmp.reserve(resultLength);

    for (size_t index = 0; index < expr->strings.size; ++index)
    {
        AstArray<char> string = expr->strings.data[index];
        tmp.append(string.data, string.size);
        if (index < expr->expressions.size)
        {
            const Constant* c = constants.find(expr->expressions.data[index]);
            tmp.append(c->valueString, c->stringLength);
        }
    }
    result.type = Constant::Type_String;
    result.stringLength = unsigned(resultLength);
    AstName name = stringTable.getOrAdd(tmp.c_str(), resultLength);
    result.valueString = name.value;
}

// Pass to detect which tables are mutated or 'escape'
struct TableMutationTracker : AstVisitor
{
    const DenseHashMap<AstLocal*, Variable>& variables;

    DenseHashSet<AstLocal*> escaped;

    TableMutationTracker(const DenseHashMap<AstLocal*, Variable>& variables)
        : variables(variables)
    {
    }

    void markEscaped(AstExpr* expr)
    {
        for (;;)
        {
            if (AstExprLocal* local = expr->as<AstExprLocal>())
            {
                escaped.insert(local->local);
                return;
            }
            else if (AstExprGroup* group = expr->as<AstExprGroup>())
            {
                expr = group->expr;
            }
            else if (AstExprTypeAssertion* assertion = expr->as<AstExprTypeAssertion>())
            {
                expr = assertion->expr;
            }
            else if (AstExprInstantiate* inst = expr->as<AstExprInstantiate>())
            {
                expr = inst->expr;
            }
            else if (AstExprIfElse* ifElse = expr->as<AstExprIfElse>())
            {
                markEscaped(ifElse->trueExpr); // recurse through true branch
                expr = ifElse->falseExpr;      // continue loop with false branch
            }
            else if (AstExprBinary* bin = expr->as<AstExprBinary>())
            {
                if (bin->op == AstExprBinary::And || bin->op == AstExprBinary::Or)
                {
                    markEscaped(bin->left); // recurse through lhs
                    expr = bin->right;      // continue loop with rhs
                }
                else
                {
                    return;
                }
            }
            else
            {
                return;
            }
        }
    }

    void markEscapedTableIndex(AstExpr* expr, bool isLvalue)
    {
        if (AstExprIndexName* idx = expr->as<AstExprIndexName>())
        {
            markEscaped(idx->expr);
        }
        else if (AstExprIndexExpr* idx = expr->as<AstExprIndexExpr>())
        {
            markEscaped(idx->expr);

            if (isLvalue)
                markEscaped(idx->index);
        }
    }

    bool visit(AstExprCall* node) override
    {
        // Values passed in as arguments can escape
        for (AstExpr* arg : node->args)
            markEscaped(arg);

        // Table indexed in a self call escapes through 'self'
        if (node->self)
            markEscapedTableIndex(node->func, false);

        return true;
    }

    bool visit(AstExprTable* node) override
    {
        // Values stored inside a table constructor can escape
        for (const AstExprTable::Item& item : node->items)
        {
            if (item.key)
                markEscaped(item.key);

            markEscaped(item.value);
        }

        return true;
    }

    bool visit(AstStatLocal* node) override
    {
        // Aliasing a table reference marks the source as escaped
        for (size_t i = 0; i < node->values.size && i < node->vars.size; ++i)
            markEscaped(node->values.data[i]);

        return true;
    }

    bool visit(AstStatAssign* node) override
    {
        // RHS values that are table locals are being aliased
        for (AstExpr* rhs : node->values)
            markEscaped(rhs);

        // LHS index expressions mutate the table being indexed
        for (AstExpr* lhs : node->vars)
            markEscapedTableIndex(lhs, true);

        return true;
    }

    bool visit(AstStatCompoundAssign* node) override
    {
        // LHS index expressions mutate the table
        markEscapedTableIndex(node->var, true);
        return true;
    }

    bool visit(AstStatFunction* node) override
    {
        // Adding a method on a table mutates it
        markEscapedTableIndex(node->name, true);
        return true;
    }

    bool visit(AstStatForIn* node) override
    {
        // Iterator state values escape
        for (AstExpr* expr : node->values)
            markEscaped(expr);

        return true;
    }

    bool visit(AstStatReturn* node) override
    {
        // Returning a table is sometimes safe, but when it's combined with upvalues and local functions, it's very brittle
        for (AstExpr* expr : node->list)
            markEscaped(expr);

        return true;
    }
};

struct ConstantVisitor : AstVisitor
{
    DenseHashMap<AstExpr*, Constant>& constants;
    DenseHashMap<AstLocal*, Variable>& variables;
    DenseHashMap<AstLocal*, Constant>& locals;

    const DenseHashMap<AstExprCall*, int>* builtins;
    bool foldLibraryK = false;
    bool vectorDoublePrecision;
    LibraryMemberConstantCallback libraryMemberConstantCb;
    AstNameTable& stringTable;
    std::vector<DenseHashMap<AstName, Constant>> constantTables;

    bool wasEmpty = false;

    std::vector<Constant> builtinArgs;

    const DenseHashMap<AstLocal*, TableConstantKind>& constantTableLocals;
    DenseHashMap<AstLocal*, Constant> tableLocals;

    ExprConstantChangeLog* exprChangeLog = nullptr;
    LocalConstantChangeLog* localChangeLog = nullptr;

    ConstantVisitor(
        DenseHashMap<AstExpr*, Constant>& constants,
        DenseHashMap<AstLocal*, Variable>& variables,
        DenseHashMap<AstLocal*, Constant>& locals,
        const DenseHashMap<AstExprCall*, int>* builtins,
        bool foldLibraryK,
        bool vectorDoublePrecision,
        LibraryMemberConstantCallback libraryMemberConstantCb,
        AstNameTable& stringTable,
        const DenseHashMap<AstLocal*, TableConstantKind>& constantTableLocals,
        ExprConstantChangeLog* exprChangeLog = nullptr,
        LocalConstantChangeLog* localChangeLog = nullptr
    )
        : constants(constants)
        , variables(variables)
        , locals(locals)
        , builtins(builtins)
        , foldLibraryK(foldLibraryK)
        , vectorDoublePrecision(vectorDoublePrecision)
        , libraryMemberConstantCb(libraryMemberConstantCb)
        , stringTable(stringTable)
        , constantTableLocals(constantTableLocals)
        , exprChangeLog(exprChangeLog)
        , localChangeLog(localChangeLog)
    {
        // since we do a single pass over the tree, if the initial state was empty we don't need to clear out old entries
        wasEmpty = constants.empty() && locals.empty();
    }

    Constant analyze(AstExpr* node)
    {
        Constant result;
        result.type = Constant::Type_Unknown;

        if (AstExprGroup* expr = node->as<AstExprGroup>())
        {
            result = analyze(expr->expr);
        }
        else if (node->is<AstExprConstantNil>())
        {
            result.type = Constant::Type_Nil;
        }
        else if (AstExprConstantBool* expr = node->as<AstExprConstantBool>())
        {
            result.type = Constant::Type_Boolean;
            result.valueBoolean = expr->value;
        }
        else if (AstExprConstantNumber* expr = node->as<AstExprConstantNumber>())
        {
            // moo: the AST carries the exact raw Q48.16 value the parser read (docs/DESIGN.md "Literals")
            result.type = Constant::Type_Number;
            result.valueNumber = expr->rawValue;
        }
        else if (AstExprConstantInteger* expr = node->as<AstExprConstantInteger>())
        {
            result.type = Constant::Type_Integer;
            result.valueInteger64 = expr->value;
        }
        else if (AstExprConstantString* expr = node->as<AstExprConstantString>())
        {
            result.type = Constant::Type_String;
            result.valueString = expr->value.data;
            result.stringLength = unsigned(expr->value.size);
        }
        else if (AstExprLocal* expr = node->as<AstExprLocal>())
        {
            if (const Constant* l = locals.find(expr->local))
                result = *l;
            else if (const Constant* l = tableLocals.find(expr->local))
                result = *l;
        }
        else if (node->is<AstExprGlobal>())
        {
            // nope
        }
        else if (node->is<AstExprVarargs>())
        {
            // nope
        }
        else if (AstExprCall* expr = node->as<AstExprCall>())
        {
            analyze(expr->func);

            const int* bfid = builtins ? builtins->find(expr) : nullptr;

            if (bfid && *bfid != LBF_NONE)
            {
                // since recursive calls to analyze() may reuse the vector we need to be careful and preserve existing contents
                size_t offset = builtinArgs.size();
                bool canFold = true;

                builtinArgs.reserve(offset + expr->args.size);

                for (size_t i = 0; i < expr->args.size; ++i)
                {
                    Constant ac = analyze(expr->args.data[i]);

                    if (ac.type == Constant::Type_Unknown || ac.type == Constant::Type_Table)
                        canFold = false;
                    else
                        builtinArgs.push_back(ac);
                }

                if (canFold)
                {
                    LUAU_ASSERT(builtinArgs.size() == offset + expr->args.size);
                    result = foldBuiltin(stringTable, *bfid, builtinArgs.data() + offset, expr->args.size, vectorDoublePrecision);
                }

                builtinArgs.resize(offset);
            }
            else
            {
                for (size_t i = 0; i < expr->args.size; ++i)
                    analyze(expr->args.data[i]);
            }
        }
        else if (AstExprIndexName* expr = node->as<AstExprIndexName>())
        {
            Constant value = analyze(expr->expr);
            if (value.type == Constant::Type_Table)
            {
                LUAU_ASSERT(value.valueTable < constantTables.size());
                if (value.valueTable < constantTables.size())
                {
                    const DenseHashMap<AstName, Constant>& props = constantTables[value.valueTable];
                    if (const Constant* prop = props.find(expr->index))
                        result = *prop;
                }
            }
            // moo: the float vector is removed; no vector component or library constant folding (docs/DESIGN.md "vec3, quat and transform").
            else if (foldLibraryK)
            {
                if (AstExprGlobal* eg = expr->expr->as<AstExprGlobal>())
                {
                    if (eg->name == "math")
                        result = foldBuiltinMath(expr->index);

                    // if we have a custom handler and the constant hasn't been resolved
                    if (libraryMemberConstantCb && result.type == Constant::Type_Unknown)
                        libraryMemberConstantCb(eg->name.value, expr->index.value, reinterpret_cast<Luau::CompileConstant*>(&result));
                }
            }
        }
        else if (AstExprIndexExpr* expr = node->as<AstExprIndexExpr>())
        {
            Constant indexVal = analyze(expr->index);
            Constant tableVal = analyze(expr->expr);

            if (tableVal.type == Constant::Type_Table && indexVal.type == Constant::Type_String)
            {
                LUAU_ASSERT(tableVal.valueTable < constantTables.size());
                if (tableVal.valueTable < constantTables.size() && indexVal.stringLength != 0)
                {
                    const DenseHashMap<AstName, Constant>& props = constantTables[tableVal.valueTable];
                    AstName indexName = stringTable.getOrAdd(indexVal.valueString, indexVal.stringLength);
                    if (const Constant* prop = props.find(std::move(indexName)))
                        result = *prop;
                }
            }
        }
        else if (AstExprFunction* expr = node->as<AstExprFunction>())
        {
            // this is necessary to propagate constant information in all child functions
            expr->body->visit(this);
        }
        else if (AstExprTable* expr = node->as<AstExprTable>())
        {
            // If expr is a constant table, update result to be a table constant, and insert it into constantTables
            DenseHashMap<AstName, Constant> props;
            for (size_t i = 0; i < expr->items.size; ++i)
            {
                const AstExprTable::Item& item = expr->items.data[i];

                Constant valueVal = analyze(item.value);

                if (item.key)
                {
                    Constant keyVal = analyze(item.key);

                    if (keyVal.type == Constant::Type_String && valueVal.type != Constant::Type_Unknown && valueVal.type != Constant::Type_Table &&
                        keyVal.stringLength != 0)
                    {
                        AstName constKey = stringTable.getOrAdd(keyVal.valueString, keyVal.stringLength);

                        props[std::move(constKey)] = std::move(valueVal);
                    }
                    // TODO: Support other types of keys
                }
            }

            if (props.size() == expr->items.size)
            {
                result.type = Constant::Type_Table;
                result.valueTable = constantTables.size();
                constantTables.push_back(std::move(props));
            }
        }
        else if (AstExprUnary* expr = node->as<AstExprUnary>())
        {
            Constant arg = analyze(expr->expr);

            if (arg.type != Constant::Type_Unknown)
                foldUnary(result, expr->op, arg);
        }
        else if (AstExprBinary* expr = node->as<AstExprBinary>())
        {
            Constant la = analyze(expr->left);
            Constant ra = analyze(expr->right);

            // note: ra doesn't need to be constant to fold and/or
            if (la.type != Constant::Type_Unknown)
                foldBinary(result, expr->op, la, ra, stringTable);
        }
        else if (AstExprTypeAssertion* expr = node->as<AstExprTypeAssertion>())
        {
            Constant arg = analyze(expr->expr);

            result = arg;
        }
        else if (AstExprIfElse* expr = node->as<AstExprIfElse>())
        {
            Constant cond = analyze(expr->condition);
            Constant trueExpr = analyze(expr->trueExpr);
            Constant falseExpr = analyze(expr->falseExpr);

            if (cond.type != Constant::Type_Unknown)
                result = cond.isTruthful() ? trueExpr : falseExpr;
        }
        else if (AstExprInterpString* expr = node->as<AstExprInterpString>())
        {
            bool onlyConstantSubExpr = true;
            for (AstExpr* expression : expr->expressions)
                if (analyze(expression).type != Constant::Type_String)
                    onlyConstantSubExpr = false;

            if (onlyConstantSubExpr)
                foldInterpString(result, expr, constants, stringTable);
        }
        else if (AstExprInstantiate* expr = node->as<AstExprInstantiate>())
        {
            result = analyze(expr->expr);
        }
        else
        {
            LUAU_ASSERT(!"Unknown expression type");
        }

        recordConstant(constants, node, result);

        return result;
    }

    template<typename T>
    void recordConstant(DenseHashMap<T, Constant>& map, T key, const Constant& value)
    {
        if (value.type == Constant::Type_Table)
        {
            // Table constants are recorded in a separate map
        }
        else if (value.type != Constant::Type_Unknown)
        {
            logChange(map, key);
            map[key] = value;
        }
        else if (wasEmpty)
        {
            // No need to clear out entries if we started with empty maps
        }
        else if (Constant* old = map.find(key))
        {
            logChange(map, key, old);
            old->type = Constant::Type_Unknown;
        }
    }

    void logChange(DenseHashMap<AstExpr*, Constant>& map, AstExpr* key, const Constant* existing = nullptr)
    {
        if (!exprChangeLog)
            return;

        const Constant* old = existing ? existing : map.find(key);
        exprChangeLog->push_back({key, old ? *old : Constant{}, old == nullptr});
    }

    void logChange(DenseHashMap<AstLocal*, Constant>& map, AstLocal* key, const Constant* existing = nullptr)
    {
        if (!localChangeLog)
            return;

        const Constant* old = existing ? existing : map.find(key);
        localChangeLog->push_back({key, old ? *old : Constant{}, old == nullptr});
    }

    void recordValue(AstLocal* local, const Constant& value)
    {
        // note: we rely on trackValues to have been run before us
        Variable* v = variables.find(local);
        LUAU_ASSERT(v);

        if (!v->written)
        {
            if (value.type == Constant::Type_Table)
            {
                v->constant = false;
                tableLocals[local] = value;
            }
            else
            {
                v->constant = (value.type != Constant::Type_Unknown);
                recordConstant(locals, local, value);
            }
        }
    }

    bool visit(AstExpr* node) override
    {
        // note: we short-circuit the visitor traversal through any expression trees by returning false
        // recursive traversal is happening inside analyze() which makes it easier to get the resulting value of the subexpression
        analyze(node);

        return false;
    }

    void recordLocal(AstLocal* local, AstExpr* value)
    {
        Constant arg = analyze(value);

        if (arg.type == Constant::Type_Table)
        {
            // If this table could be mutated later, record Constant_Unknown instead of Constant_Table
            const TableConstantKind* kind = constantTableLocals.find(local);
            if (kind && *kind == ConstantTable)
                recordValue(local, arg);
            else
                recordValue(local, {});
        }
        else
        {
            recordValue(local, arg);
        }
    }

    bool visit(AstStatLocal* node) override
    {
        // all values that align wrt indexing are simple - we just match them 1-1
        for (size_t i = 0; i < node->vars.size && i < node->values.size; ++i)
        {
            if (FFlag::LuauExperimentalIfLocalSyntax)
            {
                recordLocal(node->vars.data[i], node->values.data[i]);
            }
            else
            {
                AstExpr* rhs = node->values.data[i];
                Constant arg = analyze(rhs);

                if (arg.type == Constant::Type_Table)
                {
                    AstLocal* local = node->vars.data[i];

                    // If this table could be mutated later, record Constant_Unknown instead of Constant_Table
                    const TableConstantKind* kind = constantTableLocals.find(local);
                    if (kind && *kind == ConstantTable)
                        recordValue(local, arg);
                    else
                        recordValue(local, {});
                }
                else
                    recordValue(node->vars.data[i], arg);
            }
        }

        if (node->vars.size > node->values.size)
        {
            // if we have trailing variables, then depending on whether the last value is capable of returning multiple values
            // (aka call or varargs), we either don't know anything about these vars, or we know they're nil
            AstExpr* last = node->values.size ? node->values.data[node->values.size - 1] : nullptr;
            bool multRet = last && (last->is<AstExprCall>() || last->is<AstExprVarargs>());

            if (!multRet)
            {
                for (size_t i = node->values.size; i < node->vars.size; ++i)
                {
                    Constant nil = {Constant::Type_Nil};
                    recordValue(node->vars.data[i], nil);
                }
            }
        }
        else
        {
            // we can have more values than variables; in this case we still need to analyze them to make sure we do constant propagation inside
            // them
            for (size_t i = node->vars.size; i < node->values.size; ++i)
                analyze(node->values.data[i]);
        }

        return false;
    }

    bool visit(AstStatIf* node) override
    {
        if (AstLocal* local = node->conditionLocal)
        {
            recordLocal(local, node->condition);

            node->thenbody->visit(this);

            if (node->elsebody)
                node->elsebody->visit(this);

            return false;
        }

        return true;
    }

    bool visit(AstExprIfElse* node) override
    {
        if (node->conditionLocal)
            recordLocal(node->conditionLocal, node->condition);

        analyze(node);
        return false;
    }
};

void buildTableConstantMap(DenseHashMap<AstLocal*, TableConstantKind>& result, const DenseHashMap<AstLocal*, Variable>& variables, AstNode* root)
{
    TableMutationTracker tracker{variables};
    root->visit(&tracker);

    for (auto& [local, var] : variables)
    {
        if (var.written)
            continue;

        if (!var.init || !unwrapExprOfType<AstExprTable>(var.init))
            continue;

        if (!tracker.escaped.contains(local))
            result[local] = ConstantTable;
    }
}

void undoChanges(DenseHashMap<AstExpr*, Constant>& constants, const ExprConstantChangeLog& changes)
{
    for (auto it = changes.rbegin(); it != changes.rend(); ++it)
    {
        if (it->wasAbsent)
        {
            if (Constant* old = constants.find(it->key))
                old->type = Constant::Type_Unknown;
        }
        else
        {
            constants[it->key] = it->oldValue;
        }
    }
}

void undoChanges(DenseHashMap<AstLocal*, Constant>& locals, const LocalConstantChangeLog& changes)
{
    for (auto it = changes.rbegin(); it != changes.rend(); ++it)
    {
        if (it->wasAbsent)
        {
            if (Constant* old = locals.find(it->key))
                old->type = Constant::Type_Unknown;
        }
        else
        {
            locals[it->key] = it->oldValue;
        }
    }
}

void foldConstants(
    DenseHashMap<AstExpr*, Constant>& constants,
    DenseHashMap<AstLocal*, Variable>& variables,
    DenseHashMap<AstLocal*, Constant>& locals,
    const DenseHashMap<AstExprCall*, int>* builtins,
    bool foldLibraryK,
    bool vectorDoublePrecision,
    LibraryMemberConstantCallback libraryMemberConstantCb,
    AstNode* root,
    AstNameTable& stringTable,
    const DenseHashMap<AstLocal*, TableConstantKind>& tableConstants,
    ExprConstantChangeLog* exprChangeLog,
    LocalConstantChangeLog* localChangeLog
)
{
    ConstantVisitor visitor{
        constants,
        variables,
        locals,
        builtins,
        foldLibraryK,
        vectorDoublePrecision,
        libraryMemberConstantCb,
        stringTable,
        tableConstants,
        exprChangeLog,
        localChangeLog
    };
    root->visit(&visitor);
}

} // namespace Compile
} // namespace Luau
