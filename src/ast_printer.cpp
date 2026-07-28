#include "ast_printer.hpp"
#include <sstream>

void AstPrinter::indent(int depth, std::string &out)
{
    for (int i = 0; i < depth; i++)
        out += "  ";
}

const char *AstPrinter::tokenOpName(TokenKind k)
{
    switch (k)
    {
    case TokenKind::PLUS_TOKEN:
        return "+";
    case TokenKind::MINUS_TOKEN:
        return "-";
    case TokenKind::MULTIPLY_TOKEN:
        return "*";
    case TokenKind::DIVIDE_TOKEN:
        return "/";
    case TokenKind::MODULO_TOKEN:
        return "%";
    case TokenKind::EQUAL_EQUAL_TOKEN:
        return "==";
    case TokenKind::NOT_EQUAL_TOKEN:
        return "!=";
    case TokenKind::LESS_TOKEN:
        return "<";
    case TokenKind::GREATER_TOKEN:
        return ">";
    case TokenKind::LESS_EQUAL_TOKEN:
        return "<=";
    case TokenKind::GREATER_EQUAL_TOKEN:
        return ">=";
    case TokenKind::LOGICAL_AND_TOKEN:
        return "&&";
    case TokenKind::LOGICAL_OR_TOKEN:
        return "||";
    case TokenKind::LOGICAL_NOT_TOKEN:
        return "!";
    case TokenKind::AMPERSAND_TOKEN:
        return "&";
    case TokenKind::PIPE_TOKEN:
        return "|";
    case TokenKind::CARET_TOKEN:
        return "^";
    case TokenKind::TILDE_TOKEN:
        return "~";
    case TokenKind::ASSIGN_TOKEN:
        return "=";
    case TokenKind::PLUS_ASSIGN_TOKEN:
        return "+=";
    case TokenKind::MINUS_ASSIGN_TOKEN:
        return "-=";
    case TokenKind::INCREMENT_TOKEN:
        return "++";
    case TokenKind::DECREMENT_TOKEN:
        return "--";
    default:
        return "?op?";
    }
}

void AstPrinter::printType(Type *t, std::string &out)
{
    if (!t)
    {
        out += "<void>";
        return;
    }
    switch (t->kind)
    {
    case TypeKind::I8:
        out += "i8";
        break;
    case TypeKind::I16:
        out += "i16";
        break;
    case TypeKind::I32:
        out += "i32";
        break;
    case TypeKind::I64:
        out += "i64";
        break;
    case TypeKind::U8:
        out += "u8";
        break;
    case TypeKind::U16:
        out += "u16";
        break;
    case TypeKind::U32:
        out += "u32";
        break;
    case TypeKind::U64:
        out += "u64";
        break;
    case TypeKind::F32:
        out += "f32";
        break;
    case TypeKind::F64:
        out += "f64";
        break;
    case TypeKind::BOOL:
        out += "bool";
        break;
    case TypeKind::CHAR:
        out += "char";
        break;
    case TypeKind::VOID:
        out += "void";
        break;
    case TypeKind::NAMED:
        out += std::string(t->named.name);
        break;
    case TypeKind::POINTER:
        printType(t->wrapper.inner, out);
        out += "*";
        break;
    case TypeKind::OPTIONAL:
        printType(t->wrapper.inner, out);
        out += "?";
        break;
    case TypeKind::VARIADIC:
        printType(t->wrapper.inner, out);
        out += "[...]";
        break;
    case TypeKind::ARRAY:
        printType(t->array.element, out);
        out += "[" + std::to_string(t->array.size) + "]";
        break;
    case TypeKind::SLICE:
        printType(t->slice.element, out);
        out += "[]";
        break;
    case TypeKind::FUNC:
        out += "func(...) -> ";
        printType(t->func.return_type, out);
        break;
    default:
        out += "<invalid>";
        break;
    }
}

void AstPrinter::printExpr(Expr *e, int depth, std::string &out)
{
    if (!e)
    {
        indent(depth, out);
        out += "<null-expr>\n";
        return;
    }
    indent(depth, out);

    switch (e->kind)
    {
    case ExprKind::LITERAL_INT:
        out += "IntLiteral(" + std::to_string(e->literal_int.value) + ")\n";
        break;
    case ExprKind::LITERAL_FLOAT:
        out += "FloatLiteral(" + std::to_string(e->literal_float.value) + ")\n";
        break;
    case ExprKind::LITERAL_CHAR:
        out += std::string("CharLiteral('") + e->literal_char.value + "')\n";
        break;
    case ExprKind::LITERAL_STRING:
        out += "StringLiteral(\"" + std::string(e->literal_string.value) + "\")\n";
        break;
    case ExprKind::LITERAL_BOOL:
        out += std::string("BoolLiteral(") + (e->literal_bool.value ? "true" : "false") + ")\n";
        break;
    case ExprKind::LITERAL_ARRAY:
        out += std::string("ArrayLiteral(\n");
        for (size_t i = 0; i < e->literal_array.value_count; i++)
        {
            printExpr(e->literal_array.values[i], depth + 1, out);
        }
        indent(depth, out);
        out += std::string(")\n");
        break;
    case ExprKind::IDENTIFIER:
        out += "Identifier(" + std::string(e->identifier.name) + ", ";
        out += (e->identifier.is_global ? "true" : "false");
        out += ")\n";
        break;

    case ExprKind::BINARY:
        out += std::string("Binary(") + tokenOpName(e->binary.op) + ")\n";
        printExpr(e->binary.left, depth + 1, out);
        printExpr(e->binary.right, depth + 1, out);
        break;

    case ExprKind::UNARY:
        out += std::string("Unary(") + tokenOpName(e->unary.op) + ")\n";
        printExpr(e->unary.operand, depth + 1, out);
        break;

    case ExprKind::ASSIGN:
        out += std::string("Assign(") + tokenOpName(e->assign.op) + ")\n";
        printExpr(e->assign.target, depth + 1, out);
        printExpr(e->assign.value, depth + 1, out);
        break;

    case ExprKind::CALL:
        out += "Call\n";
        indent(depth + 1, out);
        out += "callee:\n";
        printExpr(e->call.callee, depth + 2, out);
        indent(depth + 1, out);
        out += "args:\n";
        for (uint32_t i = 0; i < e->call.arg_count; i++)
            printExpr(e->call.args[i], depth + 2, out);
        break;

    case ExprKind::INDEX:
        out += "Index\n";
        printExpr(e->index.array, depth + 1, out);
        printExpr(e->index.index, depth + 1, out);
        break;

    case ExprKind::MEMBER:
        out += "Member(." + std::string(e->member.field) + ")\n";
        printExpr(e->member.object, depth + 1, out);
        break;
    case ExprKind::ARROW_MEMBER:
        out += "ArrowMember(." + std::string(e->member.field) + ")\n";
        printExpr(e->member.object, depth + 1, out);
        break;

    case ExprKind::CAST:
        out += "Cast(";
        printType(e->cast.target_type, out);
        out += ")\n";
        printExpr(e->cast.operand, depth + 1, out);
        break;

    case ExprKind::SIZEOF:
        if (e->size_of.is_type)
        {
            out += "Sizeof(type=";
            printType(e->size_of.type, out);
            out += ")\n";
        }
        else
        {
            out += "Sizeof(expr)\n";
            printExpr(e->size_of.expr, depth + 1, out);
        }
        break;

    case ExprKind::TERNARY:
        out += "Ternary\n";
        printExpr(e->ternary.cond, depth + 1, out);
        printExpr(e->ternary.then_branch, depth + 1, out);
        printExpr(e->ternary.else_branch, depth + 1, out);
        break;

    case ExprKind::PRE_INC_DEC:
        out += std::string("PreIncDec(") + tokenOpName(e->pre_inc_dec.op) + ")\n";
        printExpr(e->pre_inc_dec.operand, depth + 1, out);
        break;

    case ExprKind::POST_INC_DEC:
        out += std::string("PostIncDec(") + tokenOpName(e->post_inc_dec.op) + ")\n";
        printExpr(e->post_inc_dec.operand, depth + 1, out);
        break;

    default:
        out += "<invalid-expr>\n";
    }
}

void AstPrinter::printStmt(Stmt *s, int depth, std::string &out)
{
    if (!s)
    {
        indent(depth, out);
        out += "<null-stmt>\n";
        return;
    }
    indent(depth, out);

    switch (s->kind)
    {
    case StmtKind::EXPR_STMT:
        out += "ExprStmt\n";
        printExpr(s->expr_stmt.expr, depth + 1, out);
        break;

    case StmtKind::BLOCK:
        out += "Block\n";
        for (uint32_t i = 0; i < s->block.count; i++)
            printStmt(s->block.stmts[i], depth + 1, out);
        break;

    case StmtKind::CONST_DECL:
    case StmtKind::VAR_DECL:
        out += std::string(s->kind == StmtKind::CONST_DECL ? "LetDecl(" : "VarDecl(") + std::string(s->var_decl.name) + ": ";
        printType(s->var_decl.type, out);
        out += ", is_global: ";
        out += (s->var_decl.is_global ? "true" : "false");
        out += ")\n";
        if (s->var_decl.init)
            printExpr(s->var_decl.init, depth + 1, out);
        break;

    case StmtKind::FUNC_DECL:
        out += "FuncDecl(" + std::string(s->func_decl.name) + ", ";
        out += "variadic: ";
        out += s->func_decl.is_variadic ? "true" : "false";
        out += ") -> ";
        printType(s->func_decl.return_type, out);
        out += "\n";
        for (uint32_t i = 0; i < s->func_decl.param_count; i++)
        {
            indent(depth + 1, out);
            out += "param " + std::string(s->func_decl.params[i].name) + ": ";
            printType(s->func_decl.params[i].type, out);
            out += "\n";
        }
        if (s->func_decl.body)
            printStmt(s->func_decl.body, depth + 1, out);
        break;

    case StmtKind::STRUCT_DECL:
        out += "StructDecl(" + std::string(s->struct_union_decl.name) + ")\n";
        for (uint32_t i = 0; i < s->struct_union_decl.field_count; i++)
        {
            indent(depth + 1, out);
            out += std::string(s->struct_union_decl.fields[i].name) + ": ";
            printType(s->struct_union_decl.fields[i].type, out);
            out += "\n";
        }
        break;

    case StmtKind::UNION_DECL:
        out += "UnionDecl(" + std::string(s->struct_union_decl.name) + ")\n";
        for (uint32_t i = 0; i < s->struct_union_decl.field_count; i++)
        {
            indent(depth + 1, out);
            out += std::string(s->struct_union_decl.fields[i].name) + ": ";
            printType(s->struct_union_decl.fields[i].type, out);
            out += "\n";
        }
        break;

    case StmtKind::ENUM_DECL:
        out += "EnumDecl(" + std::string(s->enum_decl.name) + ")\n";
        for (uint32_t i = 0; i < s->enum_decl.variant_count; i++)
        {
            indent(depth + 1, out);
            out += std::string(s->enum_decl.variant_names[i]) + "\n";
            if (s->enum_decl.variant_values[i])
                printExpr(s->enum_decl.variant_values[i], depth + 2, out);
        }
        break;

    case StmtKind::IMPORT_DECL:
        out += "ImportDecl(" + std::string(s->import_decl.module_name) + ")\n";
        break;

    case StmtKind::IF_STMT:
        out += "If\n";
        indent(depth + 1, out);
        out += "cond:\n";
        printExpr(s->if_stmt.cond, depth + 2, out);
        indent(depth + 1, out);
        out += "then:\n";
        printStmt(s->if_stmt.then_branch, depth + 2, out);
        if (s->if_stmt.else_branch)
        {
            indent(depth + 1, out);
            out += "else:\n";
            printStmt(s->if_stmt.else_branch, depth + 2, out);
        }
        break;

    case StmtKind::WHILE_STMT:
        out += "While\n";
        printExpr(s->while_stmt.cond, depth + 1, out);
        printStmt(s->while_stmt.body, depth + 1, out);
        break;

    case StmtKind::DO_WHILE_STMT:
        out += "DoWhile\n";
        printStmt(s->while_stmt.body, depth + 1, out);
        printExpr(s->while_stmt.cond, depth + 1, out);
        break;

    case StmtKind::FOR_STMT:
        out += "For\n";
        if (s->for_stmt.init)
            printStmt(s->for_stmt.init, depth + 1, out);
        if (s->for_stmt.cond)
            printExpr(s->for_stmt.cond, depth + 1, out);
        if (s->for_stmt.step)
            printExpr(s->for_stmt.step, depth + 1, out);
        printStmt(s->for_stmt.body, depth + 1, out);
        break;

    case StmtKind::SWITCH_STMT:
    case StmtKind::MATCH_STMT:
        out += (s->kind == StmtKind::MATCH_STMT ? "Match\n" : "Switch\n");
        printExpr(s->switch_stmt.subject, depth + 1, out);
        for (uint32_t i = 0; i < s->switch_stmt.case_count; i++)
        {
            indent(depth + 1, out);
            CaseClause &cc = s->switch_stmt.cases[i];
            out += cc.value ? "case:\n" : "default:\n";
            if (cc.value)
                printExpr(cc.value, depth + 2, out);
            for (uint32_t j = 0; j < cc.body_count; j++)
                printStmt(cc.body[j], depth + 2, out);
        }
        break;

    case StmtKind::BREAK_STMT:
        out += "Break\n";
        break;
    case StmtKind::CONTINUE_STMT:
        out += "Continue\n";
        break;

    case StmtKind::RETURN_STMT:
        out += "Return\n";
        if (s->return_stmt.value)
            printExpr(s->return_stmt.value, depth + 1, out);
        break;

    case StmtKind::DEFER_STMT:
        out += "Defer\n";
        printStmt(s->defer_stmt.body, depth + 1, out);
        break;

    default:
        out += "<invalid-stmt>\n";
    }
}

std::string AstPrinter::print(SourceFile *file)
{
    std::string out;
    out += "SourceFile(file_id=" + std::to_string(file->file_id) + ")\n";
    for (uint32_t i = 0; i < file->decl_count; i++)
        printStmt(file->top_level_decls[i], 1, out);
    return out;
}