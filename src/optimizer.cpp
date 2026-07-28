#include "optimizer.hpp"
#include "ast.hpp"        
#include <vector>
#include <cstring>

static bool isIntConstant(Expr *e)
{
    return e && e->kind == ExprKind::LITERAL_INT;
}

static bool isFloatConstant(Expr *e)
{
    return e && e->kind == ExprKind::LITERAL_FLOAT;
}

static bool isBoolConstant(Expr *e)
{
    return e && e->kind == ExprKind::LITERAL_BOOL;
}

static bool isCharConstant(Expr *e)
{
    return e && e->kind == ExprKind::LITERAL_CHAR;
}

static bool isStringConstant(Expr *e)
{
    return e && e->kind == ExprKind::LITERAL_STRING;
}

static bool isConstant(Expr *e)
{
    return isIntConstant(e) || isFloatConstant(e) || isBoolConstant(e) ||
           isCharConstant(e) || isStringConstant(e);
}

static uint64_t sizeofType(Type *t)
{
    if (!t) return 0;
    switch (t->kind)
    {
        case TypeKind::I8:  case TypeKind::U8:
        case TypeKind::BOOL: case TypeKind::CHAR:
            return 1;
        case TypeKind::I16: case TypeKind::U16:
            return 2;
        case TypeKind::I32: case TypeKind::U32: case TypeKind::F32:
            return 4;
        case TypeKind::I64: case TypeKind::U64: case TypeKind::F64:
            return 8;
        case TypeKind::POINTER: case TypeKind::OPTIONAL:
        case TypeKind::SLICE:   case TypeKind::FUNC:
            return 8;
        case TypeKind::ARRAY:
            return sizeofType(t->array.element) * t->array.size;
        case TypeKind::VARIADIC:
            return 8; 
        default:
            return 0; 
    }
}

static Expr *foldBinary(Expr *expr, ArenaAllocator &arena)
{
    Expr *l = expr->binary.left;
    Expr *r = expr->binary.right;
    TokenKind op = expr->binary.op;

    if (isIntConstant(l) && isIntConstant(r))
    {
        int64_t lv = l->literal_int.value;
        int64_t rv = r->literal_int.value;

        switch (op)
        {
            case TokenKind::PLUS_TOKEN:     return Expr::makeLiteralInt(arena, lv + rv, expr->span);
            case TokenKind::MINUS_TOKEN:    return Expr::makeLiteralInt(arena, lv - rv, expr->span);
            case TokenKind::MULTIPLY_TOKEN: return Expr::makeLiteralInt(arena, lv * rv, expr->span);
            case TokenKind::DIVIDE_TOKEN:   if (rv != 0) return Expr::makeLiteralInt(arena, lv / rv, expr->span); break;
            case TokenKind::MODULO_TOKEN:   if (rv != 0) return Expr::makeLiteralInt(arena, lv % rv, expr->span); break;
            case TokenKind::AMPERSAND_TOKEN:return Expr::makeLiteralInt(arena, lv & rv, expr->span);
            case TokenKind::PIPE_TOKEN:     return Expr::makeLiteralInt(arena, lv | rv, expr->span);
            case TokenKind::CARET_TOKEN:    return Expr::makeLiteralInt(arena, lv ^ rv, expr->span);
            case TokenKind::LEFT_SHIFT_TOKEN: return Expr::makeLiteralInt(arena, lv << rv, expr->span);
            case TokenKind::RIGHT_SHIFT_TOKEN:return Expr::makeLiteralInt(arena, lv >> rv, expr->span);
            case TokenKind::EQUAL_EQUAL_TOKEN:   return Expr::makeLiteralBool(arena, lv == rv, expr->span);
            case TokenKind::NOT_EQUAL_TOKEN:     return Expr::makeLiteralBool(arena, lv != rv, expr->span);
            case TokenKind::LESS_TOKEN:          return Expr::makeLiteralBool(arena, lv <  rv, expr->span);
            case TokenKind::GREATER_TOKEN:       return Expr::makeLiteralBool(arena, lv >  rv, expr->span);
            case TokenKind::LESS_EQUAL_TOKEN:    return Expr::makeLiteralBool(arena, lv <= rv, expr->span);
            case TokenKind::GREATER_EQUAL_TOKEN: return Expr::makeLiteralBool(arena, lv >= rv, expr->span);
            case TokenKind::LOGICAL_AND_TOKEN:   return Expr::makeLiteralBool(arena, lv && rv, expr->span);
            case TokenKind::LOGICAL_OR_TOKEN:    return Expr::makeLiteralBool(arena, lv || rv, expr->span);
            default: break;
        }
        return expr;
    }

    if (isFloatConstant(l) && isFloatConstant(r))
    {
        double lv = l->literal_float.value;
        double rv = r->literal_float.value;

        switch (op)
        {
            case TokenKind::PLUS_TOKEN:     return Expr::makeLiteralFloat(arena, lv + rv, expr->span);
            case TokenKind::MINUS_TOKEN:    return Expr::makeLiteralFloat(arena, lv - rv, expr->span);
            case TokenKind::MULTIPLY_TOKEN: return Expr::makeLiteralFloat(arena, lv * rv, expr->span);
            case TokenKind::DIVIDE_TOKEN:   return Expr::makeLiteralFloat(arena, lv / rv, expr->span);
            case TokenKind::EQUAL_EQUAL_TOKEN:   return Expr::makeLiteralBool(arena, lv == rv, expr->span);
            case TokenKind::NOT_EQUAL_TOKEN:     return Expr::makeLiteralBool(arena, lv != rv, expr->span);
            case TokenKind::LESS_TOKEN:          return Expr::makeLiteralBool(arena, lv <  rv, expr->span);
            case TokenKind::GREATER_TOKEN:       return Expr::makeLiteralBool(arena, lv >  rv, expr->span);
            case TokenKind::LESS_EQUAL_TOKEN:    return Expr::makeLiteralBool(arena, lv <= rv, expr->span);
            case TokenKind::GREATER_EQUAL_TOKEN: return Expr::makeLiteralBool(arena, lv >= rv, expr->span);
            default: break;
        }
        return expr;
    }

    if (isBoolConstant(l) && isBoolConstant(r))
    {
        bool lv = l->literal_bool.value;
        bool rv = r->literal_bool.value;

        switch (op)
        {
            case TokenKind::EQUAL_EQUAL_TOKEN: return Expr::makeLiteralBool(arena, lv == rv, expr->span);
            case TokenKind::NOT_EQUAL_TOKEN:   return Expr::makeLiteralBool(arena, lv != rv, expr->span);
            case TokenKind::LOGICAL_AND_TOKEN: return Expr::makeLiteralBool(arena, lv && rv, expr->span);
            case TokenKind::LOGICAL_OR_TOKEN:  return Expr::makeLiteralBool(arena, lv || rv, expr->span);
            default: break;
        }
        return expr;
    }

    if (isCharConstant(l) && isCharConstant(r))
    {
        char lv = l->literal_char.value;
        char rv = r->literal_char.value;

        switch (op)
        {
            case TokenKind::PLUS_TOKEN:     return Expr::makeLiteralChar(arena, lv + rv, expr->span);
            case TokenKind::MINUS_TOKEN:    return Expr::makeLiteralChar(arena, lv - rv, expr->span);
            case TokenKind::MULTIPLY_TOKEN: return Expr::makeLiteralChar(arena, lv * rv, expr->span);
            case TokenKind::DIVIDE_TOKEN:   if (rv != 0) return Expr::makeLiteralChar(arena, lv / rv, expr->span); break;
            case TokenKind::MODULO_TOKEN:   if (rv != 0) return Expr::makeLiteralChar(arena, lv % rv, expr->span); break;
            case TokenKind::EQUAL_EQUAL_TOKEN:   return Expr::makeLiteralBool(arena, lv == rv, expr->span);
            case TokenKind::NOT_EQUAL_TOKEN:     return Expr::makeLiteralBool(arena, lv != rv, expr->span);
            case TokenKind::LESS_TOKEN:          return Expr::makeLiteralBool(arena, lv <  rv, expr->span);
            case TokenKind::GREATER_TOKEN:       return Expr::makeLiteralBool(arena, lv >  rv, expr->span);
            case TokenKind::LESS_EQUAL_TOKEN:    return Expr::makeLiteralBool(arena, lv <= rv, expr->span);
            case TokenKind::GREATER_EQUAL_TOKEN: return Expr::makeLiteralBool(arena, lv >= rv, expr->span);
            default: break;
        }
        return expr;
    }

    if (isStringConstant(l) && isStringConstant(r))
    {
        bool same_len = (l->literal_string.length == r->literal_string.length);
        bool eq = same_len && std::memcmp(l->literal_string.value, r->literal_string.value,
                                          l->literal_string.length) == 0;
        switch (op)
        {
            case TokenKind::EQUAL_EQUAL_TOKEN: return Expr::makeLiteralBool(arena, eq, expr->span);
            case TokenKind::NOT_EQUAL_TOKEN:   return Expr::makeLiteralBool(arena, !eq, expr->span);
            default: break;
        }
        return expr;
    }

    if (isBoolConstant(l))
    {
        if (op == TokenKind::LOGICAL_AND_TOKEN && !l->literal_bool.value)
            return Expr::makeLiteralBool(arena, false, expr->span);
        if (op == TokenKind::LOGICAL_OR_TOKEN  &&  l->literal_bool.value)
            return Expr::makeLiteralBool(arena, true, expr->span);
    }

    return expr;
}

static Expr *foldUnary(Expr *expr, ArenaAllocator &arena)
{
    Expr *op = expr->unary.operand;
    TokenKind kind = expr->unary.op;

    if (isIntConstant(op))
    {
        int64_t v = op->literal_int.value;
        switch (kind)
        {
            case TokenKind::MINUS_TOKEN:      return Expr::makeLiteralInt(arena, -v, expr->span);
            case TokenKind::TILDE_TOKEN:      return Expr::makeLiteralInt(arena, ~v, expr->span);
            case TokenKind::LOGICAL_NOT_TOKEN:return Expr::makeLiteralBool(arena, !v, expr->span);
            default: break;
        }
    }
    else if (isFloatConstant(op))
    {
        double v = op->literal_float.value;
        switch (kind)
        {
            case TokenKind::MINUS_TOKEN:      return Expr::makeLiteralFloat(arena, -v, expr->span);
            case TokenKind::LOGICAL_NOT_TOKEN:return Expr::makeLiteralBool(arena, !v, expr->span);
            default: break;
        }
    }
    else if (isBoolConstant(op))
    {
        bool v = op->literal_bool.value;
        switch (kind)
        {
            case TokenKind::LOGICAL_NOT_TOKEN:return Expr::makeLiteralBool(arena, !v, expr->span);
            default: break;
        }
    }
    else if (isCharConstant(op))
    {
        char v = op->literal_char.value;
        switch (kind)
        {
            case TokenKind::MINUS_TOKEN:      return Expr::makeLiteralChar(arena, -v, expr->span);
            case TokenKind::TILDE_TOKEN:      return Expr::makeLiteralChar(arena, ~v, expr->span);
            case TokenKind::LOGICAL_NOT_TOKEN:return Expr::makeLiteralBool(arena, !v, expr->span);
            default: break;
        }
    }
    return expr;
}

static Expr *foldTernary(Expr *expr, ArenaAllocator &arena)
{
    Expr *cond = expr->ternary.cond;
    if (isBoolConstant(cond))
        return cond->literal_bool.value ? expr->ternary.then_branch : expr->ternary.else_branch;
    return expr;
}

static Expr *foldCast(Expr *expr, ArenaAllocator &arena)
{
    Expr *op = expr->cast.operand;
    Type *target = expr->cast.target_type;
    if (!op || !target) return expr;

    switch (target->kind)
    {
        case TypeKind::I8: case TypeKind::I16: case TypeKind::I32: case TypeKind::I64:
        case TypeKind::U8: case TypeKind::U16: case TypeKind::U32: case TypeKind::U64:
        case TypeKind::BOOL: case TypeKind::CHAR:
        {
            int64_t v = 0;
            if      (isIntConstant(op))   v = op->literal_int.value;
            else if (isFloatConstant(op)) v = static_cast<int64_t>(op->literal_float.value);
            else if (isBoolConstant(op))  v = op->literal_bool.value ? 1 : 0;
            else if (isCharConstant(op))  v = op->literal_char.value;
            else return expr;
            return Expr::makeLiteralInt(arena, v, expr->span);
        }
        case TypeKind::F32: case TypeKind::F64:
        {
            double v = 0.0;
            if      (isIntConstant(op))   v = static_cast<double>(op->literal_int.value);
            else if (isFloatConstant(op)) v = op->literal_float.value;
            else if (isBoolConstant(op))  v = op->literal_bool.value ? 1.0 : 0.0;
            else if (isCharConstant(op))  v = static_cast<double>(op->literal_char.value);
            else return expr;
            return Expr::makeLiteralFloat(arena, v, expr->span);
        }
        default:
            return expr;
    }
}

static Expr *foldSizeof(Expr *expr, ArenaAllocator &arena)
{
    if (expr->size_of.is_type)
    {
        uint64_t sz = sizeofType(expr->size_of.type);
        if (sz > 0) return Expr::makeLiteralInt(arena, static_cast<int64_t>(sz), expr->span);
    }
    else if (expr->size_of.expr && expr->size_of.expr->resolved_type)
    {
        uint64_t sz = sizeofType(expr->size_of.expr->resolved_type);
        if (sz > 0) return Expr::makeLiteralInt(arena, static_cast<int64_t>(sz), expr->span);
    }
    return expr;
}

static Expr *foldIndex(Expr *expr, ArenaAllocator &arena)
{
    Expr *arr = expr->index.array;
    Expr *idx = expr->index.index;

    if (arr->kind == ExprKind::LITERAL_ARRAY && isIntConstant(idx))
    {
        int64_t i = idx->literal_int.value;
        if (i >= 0 && i < static_cast<int64_t>(arr->literal_array.value_count))
            return arr->literal_array.values[static_cast<uint32_t>(i)];
    }
    return expr;
}

static Expr *optimizerExpr(Expr *expr, ArenaAllocator &arena, ErrorCollector &errors)
{
    if (!expr) return nullptr;

    switch (expr->kind)
    {
        case ExprKind::BINARY:
            expr->binary.left  = optimizerExpr(expr->binary.left,  arena, errors);
            expr->binary.right = optimizerExpr(expr->binary.right, arena, errors);
            break;

        case ExprKind::UNARY:
            expr->unary.operand = optimizerExpr(expr->unary.operand, arena, errors);
            break;

        case ExprKind::ASSIGN:
            expr->assign.target = optimizerExpr(expr->assign.target, arena, errors);
            expr->assign.value  = optimizerExpr(expr->assign.value,  arena, errors);
            break;

        case ExprKind::CALL:
            expr->call.callee = optimizerExpr(expr->call.callee, arena, errors);
            for (uint32_t i = 0; i < expr->call.arg_count; ++i)
                expr->call.args[i] = optimizerExpr(expr->call.args[i], arena, errors);
            break;

        case ExprKind::INDEX:
            expr->index.array = optimizerExpr(expr->index.array, arena, errors);
            expr->index.index = optimizerExpr(expr->index.index, arena, errors);
            break;

        case ExprKind::ARROW_MEMBER:
        case ExprKind::MEMBER:
            expr->member.object = optimizerExpr(expr->member.object, arena, errors);
            break;

        case ExprKind::CAST:
            expr->cast.operand = optimizerExpr(expr->cast.operand, arena, errors);
            break;

        case ExprKind::SIZEOF:
            if (!expr->size_of.is_type)
                expr->size_of.expr = optimizerExpr(expr->size_of.expr, arena, errors);
            break;

        case ExprKind::TERNARY:
            expr->ternary.cond       = optimizerExpr(expr->ternary.cond,       arena, errors);
            expr->ternary.then_branch= optimizerExpr(expr->ternary.then_branch,arena, errors);
            expr->ternary.else_branch= optimizerExpr(expr->ternary.else_branch,arena, errors);
            break;

        case ExprKind::PRE_INC_DEC:
        case ExprKind::POST_INC_DEC:
            expr->pre_inc_dec.operand = optimizerExpr(expr->pre_inc_dec.operand, arena, errors);
            break;

        case ExprKind::LITERAL_ARRAY:
            for (uint32_t i = 0; i < expr->literal_array.value_count; ++i)
                expr->literal_array.values[i] = optimizerExpr(expr->literal_array.values[i], arena, errors);
            break;

        default:
            break;
    }

    switch (expr->kind)
    {
        case ExprKind::BINARY:       return foldBinary(expr, arena);
        case ExprKind::UNARY:        return foldUnary(expr, arena);
        case ExprKind::TERNARY:      return foldTernary(expr, arena);
        case ExprKind::CAST:         return foldCast(expr, arena);
        case ExprKind::SIZEOF:       return foldSizeof(expr, arena);
        case ExprKind::INDEX:        return foldIndex(expr, arena);
        default:                     return expr;
    }
}

static Stmt *optimizeStmt(Stmt *stmt, ArenaAllocator &arena, ErrorCollector &errors)
{
    if (!stmt) return nullptr;

    switch (stmt->kind)
    {
        case StmtKind::EXPR_STMT:
            stmt->expr_stmt.expr = optimizerExpr(stmt->expr_stmt.expr, arena, errors);
            break;

        case StmtKind::BLOCK:
            for (uint32_t i = 0; i < stmt->block.count; ++i)
                stmt->block.stmts[i] = optimizeStmt(stmt->block.stmts[i], arena, errors);
            break;

        case StmtKind::CONST_DECL:
        case StmtKind::VAR_DECL:
            if (stmt->var_decl.init)
                stmt->var_decl.init = optimizerExpr(stmt->var_decl.init, arena, errors);
            break;

        case StmtKind::FUNC_DECL:
            if (stmt->func_decl.body)
                stmt->func_decl.body = optimizeStmt(stmt->func_decl.body, arena, errors);
            for (uint32_t i = 0; i < stmt->func_decl.param_count; ++i)
            {
                Field &p = stmt->func_decl.params[i];
                if (p.default_value)
                    p.default_value = optimizerExpr(p.default_value, arena, errors);
            }
            break;

        case StmtKind::STRUCT_DECL:
        case StmtKind::UNION_DECL:
            for (uint32_t i = 0; i < stmt->struct_union_decl.field_count; ++i)
            {
                Field &f = stmt->struct_union_decl.fields[i];
                if (f.default_value)
                    f.default_value = optimizerExpr(f.default_value, arena, errors);
            }
            break;

        case StmtKind::ENUM_DECL:
            for (uint32_t i = 0; i < stmt->enum_decl.variant_count; ++i)
            {
                if (stmt->enum_decl.variant_values[i])
                    stmt->enum_decl.variant_values[i] = optimizerExpr(stmt->enum_decl.variant_values[i], arena, errors);
            }
            break;

        case StmtKind::IF_STMT:
        {
            stmt->if_stmt.cond = optimizerExpr(stmt->if_stmt.cond, arena, errors);
            stmt->if_stmt.then_branch = optimizeStmt(stmt->if_stmt.then_branch, arena, errors);
            if (stmt->if_stmt.else_branch)
                stmt->if_stmt.else_branch = optimizeStmt(stmt->if_stmt.else_branch, arena, errors);

            if (isBoolConstant(stmt->if_stmt.cond))
            {
                if (stmt->if_stmt.cond->literal_bool.value)
                    return stmt->if_stmt.then_branch;
                else
                    return stmt->if_stmt.else_branch ? stmt->if_stmt.else_branch
                                                     : Stmt::makeBlock(arena, nullptr, 0, stmt->span);
            }
            break;
        }

        case StmtKind::WHILE_STMT:
        {
            stmt->while_stmt.cond = optimizerExpr(stmt->while_stmt.cond, arena, errors);
            stmt->while_stmt.body = optimizeStmt(stmt->while_stmt.body, arena, errors);

            if (isBoolConstant(stmt->while_stmt.cond) && !stmt->while_stmt.cond->literal_bool.value)
                return Stmt::makeBlock(arena, nullptr, 0, stmt->span);
            break;
        }

        case StmtKind::DO_WHILE_STMT:
        {
            stmt->while_stmt.cond = optimizerExpr(stmt->while_stmt.cond, arena, errors);
            stmt->while_stmt.body = optimizeStmt(stmt->while_stmt.body, arena, errors);
            break;
        }

        case StmtKind::FOR_STMT:
        {
            if (stmt->for_stmt.init)
                stmt->for_stmt.init = optimizeStmt(stmt->for_stmt.init, arena, errors);
            if (stmt->for_stmt.cond)
                stmt->for_stmt.cond = optimizerExpr(stmt->for_stmt.cond, arena, errors);
            if (stmt->for_stmt.step)
                stmt->for_stmt.step = optimizerExpr(stmt->for_stmt.step, arena, errors);
            stmt->for_stmt.body = optimizeStmt(stmt->for_stmt.body, arena, errors);

            if (stmt->for_stmt.cond && isBoolConstant(stmt->for_stmt.cond) &&
                !stmt->for_stmt.cond->literal_bool.value)
            {
                if (stmt->for_stmt.init)
                {
                    Stmt **stmts = arena.create_array<Stmt *>(1);
                    stmts[0] = stmt->for_stmt.init;
                    return Stmt::makeBlock(arena, stmts, 1, stmt->span);
                }
                return Stmt::makeBlock(arena, nullptr, 0, stmt->span);
            }
            break;
        }

        case StmtKind::SWITCH_STMT:
        case StmtKind::MATCH_STMT:
        {
            stmt->switch_stmt.subject = optimizerExpr(stmt->switch_stmt.subject, arena, errors);
            for (uint32_t i = 0; i < stmt->switch_stmt.case_count; ++i)
            {
                CaseClause &c = stmt->switch_stmt.cases[i];
                if (c.value)
                    c.value = optimizerExpr(c.value, arena, errors);
                for (uint32_t j = 0; j < c.body_count; ++j)
                    c.body[j] = optimizeStmt(c.body[j], arena, errors);
            }
            break;
        }

        case StmtKind::RETURN_STMT:
            if (stmt->return_stmt.value)
                stmt->return_stmt.value = optimizerExpr(stmt->return_stmt.value, arena, errors);
            break;

        case StmtKind::DEFER_STMT:
            stmt->defer_stmt.body = optimizeStmt(stmt->defer_stmt.body, arena, errors);
            break;

        default:
            break;
    }
    return stmt;
}

SourceFile *optimizeSourceFile(SourceFile *source, ArenaAllocator &arena, ErrorCollector &errors)
{
    if (!source) return nullptr;

    std::vector<Stmt *> decls;
    decls.reserve(source->decl_count);

    for (uint32_t i = 0; i < source->decl_count; ++i)
    {
        Stmt *opt = optimizeStmt(source->top_level_decls[i], arena, errors);
        if (opt)
            decls.push_back(opt);
    }

    SourceFile *file = arena.create<SourceFile>();
    file->file_id = source->file_id;
    file->decl_count = static_cast<uint32_t>(decls.size());
    file->top_level_decls = arena.create_array<Stmt *>(decls.size());
    for (std::size_t i = 0; i < decls.size(); ++i)
        file->top_level_decls[i] = decls[i];
    file->span = source->span;

    return file;
}