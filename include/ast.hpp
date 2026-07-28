#pragma once

#include "span.hpp"
#include "lexer.hpp"
#include "arena.hpp"

#include <cstdint>
#include <string_view>

struct Type;
struct Expr;
struct Stmt;

enum class TypeKind
{
    INVALID,

    INT_CONSTANT,

    I8,
    I16,
    I32,
    I64,

    U8,
    U16,
    U32,
    U64,

    FLOAT_CONSTANT,

    F32,
    F64,

    BOOL,
    CHAR,
    VOID,

    NAMED,
    POINTER,  // T*
    OPTIONAL, // T?
    ARRAY,    // T[N]
    SLICE,    // T[]
    VARIADIC, // T[...]
    FUNC,     // func(T1, T2) -> R
};

struct Type
{
    TypeKind kind = TypeKind::INVALID;
    Span span;

    union
    {
        struct
        {
            std::string_view name;
        } named;

        struct
        {
            Type *inner;
        } wrapper;

        struct
        {
            Type *element;
            uint64_t size;
        } array;

        struct
        {
            Type *element;
        } slice;

        struct
        {
            Type **params;
            uint32_t param_count;
            Type *return_type;
        } func;
    };

    Type() : kind(TypeKind::INVALID), span() {}

    static Type *makePrimitive(ArenaAllocator &arena, TypeKind kind, Span span)
    {
        Type *t = arena.create<Type>();
        t->kind = kind;
        t->span = span;
        return t;
    }

    static Type *makeNamed(ArenaAllocator &arena, std::string_view name, Span span)
    {
        Type *t = arena.create<Type>();
        t->kind = TypeKind::NAMED;
        t->span = span;
        t->named.name = name;
        return t;
    }

    static Type *makePointer(ArenaAllocator &arena, Type *inner, Span span)
    {
        Type *t = arena.create<Type>();
        t->kind = TypeKind::POINTER;
        t->span = span;
        t->wrapper.inner = inner;
        return t;
    }

    static Type *makeOptional(ArenaAllocator &arena, Type *inner, Span span)
    {
        Type *t = arena.create<Type>();
        t->kind = TypeKind::OPTIONAL;
        t->span = span;
        t->wrapper.inner = inner;
        return t;
    }

    static Type *makeVariadic(ArenaAllocator &arena, Type *inner, Span span)
    {
        Type *t = arena.create<Type>();
        t->kind = TypeKind::VARIADIC;
        t->span = span;
        t->wrapper.inner = inner;
        return t;
    }

    static Type *makeArray(ArenaAllocator &arena, Type *element, uint64_t size, Span span)
    {
        Type *t = arena.create<Type>();
        t->kind = TypeKind::ARRAY;
        t->span = span;
        t->array.element = element;
        t->array.size = size;
        return t;
    }

    static Type *makeSlice(ArenaAllocator &arena, Type *element, Span span)
    {
        Type *t = arena.create<Type>();
        t->kind = TypeKind::SLICE;
        t->span = span;
        t->slice.element = element;
        return t;
    }

    static Type *makeFunc(ArenaAllocator &arena, Type **params, uint32_t param_count, Type *return_type, Span span)
    {
        Type *t = arena.create<Type>();
        t->kind = TypeKind::FUNC;
        t->span = span;
        t->func.params = params;
        t->func.param_count = param_count;
        t->func.return_type = return_type;
        return t;
    }
};

enum class ExprKind
{
    INVALID,

    LITERAL_INT,
    LITERAL_FLOAT,
    LITERAL_CHAR,
    LITERAL_STRING,
    LITERAL_ARRAY,
    LITERAL_BOOL,
    IDENTIFIER,

    BINARY,       // a + b, a == b, a && b, ...
    UNARY,        // -a, !a, ~a, *a (deref), &a (addr-of)
    ASSIGN,       // a = b, a += b, ...
    CALL,         // f(a, b, c)
    INDEX,        // arr[i]
    MEMBER,       // a.b
    ARROW_MEMBER, // a->b
    CAST,         // a as i32
    SIZEOF,
    TERNARY,      // cond ? a : b
    PRE_INC_DEC,  // ++a, --a
    POST_INC_DEC, // a++, a--
};

struct Expr
{
    ExprKind kind = ExprKind::INVALID;
    Span span;
    Type *resolved_type = nullptr;

    union
    {
        struct
        {
            int64_t value;
        } literal_int;
        struct
        {
            double value;
        } literal_float;
        struct
        {
            char value;
        } literal_char;
        struct
        {
            const char *value;
            uint32_t length;
        } literal_string;
        struct
        {
            bool value;
        } literal_bool;
        struct
        {
            Expr **values;
            uint32_t value_count;
        } literal_array;
        struct
        {
            std::string_view name;
            bool is_global;
        } identifier;

        struct
        {
            TokenKind op;
            Expr *left;
            Expr *right;
        } binary;
        struct
        {
            TokenKind op;
            Expr *operand;
        } unary;
        struct
        {
            TokenKind op;
            Expr *target;
            Expr *value;
        } assign;

        struct
        {
            Expr *callee;
            Expr **args;
            uint32_t arg_count;
        } call;
        struct
        {
            Expr *array;
            Expr *index;
        } index;
        struct
        {
            Expr *object;
            std::string_view field;
        } member;

        struct
        {
            Expr *operand;
            Type *target_type;
        } cast;
        struct
        {
            Type *type;
            Expr *expr;
            bool is_type;
        } size_of;

        struct
        {
            Expr *cond;
            Expr *then_branch;
            Expr *else_branch;
        } ternary;
        struct
        {
            TokenKind op;
            Expr *operand;
        } pre_inc_dec;
        struct
        {
            TokenKind op;
            Expr *operand;
        } post_inc_dec;
    };

    Expr() : kind(ExprKind::INVALID), span(), resolved_type(nullptr) {}

    static Expr *makeLiteralInt(ArenaAllocator &arena, int64_t value, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::LITERAL_INT;
        e->span = span;
        e->literal_int.value = value;
        return e;
    }

    static Expr *makeLiteralFloat(ArenaAllocator &arena, double value, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::LITERAL_FLOAT;
        e->span = span;
        e->literal_float.value = value;
        return e;
    }

    static Expr *makeLiteralChar(ArenaAllocator &arena, char value, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::LITERAL_CHAR;
        e->span = span;
        e->literal_char.value = value;
        return e;
    }

    static Expr *makeLiteralString(ArenaAllocator &arena, const char *value, uint32_t length, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::LITERAL_STRING;
        e->span = span;
        e->literal_string.value = value;
        e->literal_string.length = length;
        return e;
    }

    static Expr *makeLiteralBool(ArenaAllocator &arena, bool value, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::LITERAL_BOOL;
        e->span = span;
        e->literal_bool.value = value;
        return e;
    }

    static Expr *makeLiteralArray(ArenaAllocator &arena, Expr **values, uint32_t value_count, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::LITERAL_ARRAY;
        e->span = span;
        e->literal_array = {values, value_count};
        return e;
    }

    static Expr *makeIdentifier(ArenaAllocator &arena, std::string_view name, bool is_global, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::IDENTIFIER;
        e->span = span;
        e->identifier.name = name;
        e->identifier.is_global = is_global;
        return e;
    }

    static Expr *makeBinary(ArenaAllocator &arena, TokenKind op, Expr *left, Expr *right, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::BINARY;
        e->span = span;
        e->binary = {op, left, right};
        return e;
    }

    static Expr *makeUnary(ArenaAllocator &arena, TokenKind op, Expr *operand, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::UNARY;
        e->span = span;
        e->unary = {op, operand};
        return e;
    }

    static Expr *makeAssign(ArenaAllocator &arena, TokenKind op, Expr *target, Expr *value, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::ASSIGN;
        e->span = span;
        e->assign = {op, target, value};
        return e;
    }

    static Expr *makeCall(ArenaAllocator &arena, Expr *callee, Expr **args, uint32_t arg_count, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::CALL;
        e->span = span;
        e->call = {callee, args, arg_count};
        return e;
    }

    static Expr *makeIndex(ArenaAllocator &arena, Expr *array, Expr *index, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::INDEX;
        e->span = span;
        e->index = {array, index};
        return e;
    }

    static Expr *makeMember(ArenaAllocator &arena, Expr *object, std::string_view field, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::MEMBER;
        e->span = span;
        e->member = {object, field};
        return e;
    }

    static Expr *makeArrowMember(ArenaAllocator &arena, Expr *object, std::string_view field, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::ARROW_MEMBER;
        e->span = span;
        e->member = {object, field};
        return e;
    }

    static Expr *makeCast(ArenaAllocator &arena, Expr *operand, Type *target_type, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::CAST;
        e->span = span;
        e->cast = {operand, target_type};
        return e;
    }

    static Expr *makeSizeofType(ArenaAllocator &arena, Type *type, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::SIZEOF;
        e->span = span;
        e->size_of = {type, nullptr, true};
        return e;
    }

    static Expr *makeSizeofExpr(ArenaAllocator &arena, Expr *expr, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::SIZEOF;
        e->span = span;
        e->size_of = {nullptr, expr, false};
        return e;
    }

    static Expr *makeTernary(ArenaAllocator &arena, Expr *cond, Expr *then_b, Expr *else_b, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::TERNARY;
        e->span = span;
        e->ternary = {cond, then_b, else_b};
        return e;
    }

    static Expr *makePreIncDec(ArenaAllocator &arena, TokenKind op, Expr *operand, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::PRE_INC_DEC;
        e->span = span;
        e->pre_inc_dec = {op, operand};
        return e;
    }

    static Expr *makePostIncDec(ArenaAllocator &arena, TokenKind op, Expr *operand, Span span)
    {
        Expr *e = arena.create<Expr>();
        e->kind = ExprKind::POST_INC_DEC;
        e->span = span;
        e->post_inc_dec = {op, operand};
        return e;
    }
};

struct Field
{
    std::string_view name;
    Type *type = nullptr;
    Expr *default_value = nullptr;
    Span span;
};

enum class StmtKind
{
    INVALID,

    EXPR_STMT, // f(x);
    BLOCK,     // { ... }

    CONST_DECL,  // const x = 5;         let x: i32 = 5;
    VAR_DECL,    // var x = 5;
    FUNC_DECL,   // func f(a: i32) -> i32 { ... }
    STRUCT_DECL, // struct Point { x: i32, y: i32 }
    ENUM_DECL,   // enum Color { RED, GREEN, BLUE }
    UNION_DECL,  // union Value { i: i32, f: f32 }
    IMPORT_DECL, // import math;

    IF_STMT,
    WHILE_STMT,
    DO_WHILE_STMT,
    FOR_STMT,
    SWITCH_STMT,
    MATCH_STMT,

    BREAK_STMT,
    CONTINUE_STMT,
    RETURN_STMT,
    DEFER_STMT,
};

struct CaseClause
{
    Expr *value = nullptr;
    Stmt **body = nullptr;
    uint32_t body_count = 0;
    Span span;
};

struct Stmt
{
    StmtKind kind = StmtKind::INVALID;
    Span span;

    union
    {
        struct
        {
            Expr *expr;
        } expr_stmt;

        struct
        {
            Stmt **stmts;
            uint32_t count;
        } block;

        struct
        {
            std::string_view name;
            Type *type;
            Expr *init;
            bool is_mutable;
            bool is_global;
        } var_decl;

        struct
        {
            std::string_view name;
            Field *params;
            uint32_t param_count;
            Type *return_type;
            Stmt *body;
            bool is_variadic;
            bool is_extern;
            bool has_explicit_return_type;
        } func_decl;

        struct
        {
            std::string_view name;
            Field *fields;
            uint32_t field_count;
        } struct_union_decl;

        struct
        {
            std::string_view name;
            std::string_view *variant_names;
            Expr **variant_values;
            uint32_t variant_count;
        } enum_decl;

        struct
        {
            std::string_view module_name;
        } import_decl;

        struct
        {
            Expr *cond;
            Stmt *then_branch;
            Stmt *else_branch;
        } if_stmt;

        struct
        {
            Expr *cond;
            Stmt *body;
        } while_stmt;

        struct
        {
            Stmt *init;
            Expr *cond;
            Expr *step;
            Stmt *body;
        } for_stmt;

        struct
        {
            Expr *subject;
            CaseClause *cases;
            uint32_t case_count;
        } switch_stmt;

        struct
        {
            int _unused;
        } break_stmt;
        struct
        {
            int _unused;
        } continue_stmt;

        struct
        {
            Expr *value;
        } return_stmt;

        struct
        {
            Stmt *body;
        } defer_stmt;
    };

    Stmt() : kind(StmtKind::INVALID), span() {}

    static Stmt *makeExprStmt(ArenaAllocator &arena, Expr *expr, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::EXPR_STMT;
        s->span = span;
        s->expr_stmt.expr = expr;
        return s;
    }

    static Stmt *makeBlock(ArenaAllocator &arena, Stmt **stmts, uint32_t count, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::BLOCK;
        s->span = span;
        s->block = {stmts, count};
        return s;
    }

    static Stmt *makeVarDecl(ArenaAllocator &arena, std::string_view name, Type *type,
                             Expr *init, bool is_mutable, bool is_global, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = is_mutable ? StmtKind::VAR_DECL : StmtKind::CONST_DECL;
        s->span = span;
        s->var_decl = {name, type, init, is_mutable, is_global};
        return s;
    }

    static Stmt *makeFuncDecl(ArenaAllocator &arena, std::string_view name, Field *params,
                              uint32_t param_count, Type *return_type, Stmt *body,
                              bool is_extern, bool is_variadic, bool hasExplicitReturnType, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::FUNC_DECL;
        s->span = span;
        s->func_decl = {name, params, param_count, return_type, body, is_variadic, is_extern, hasExplicitReturnType};
        return s;
    }

    static Stmt *makeStructDecl(ArenaAllocator &arena, std::string_view name, Field *fields,
                                uint32_t field_count, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::STRUCT_DECL;
        s->span = span;
        s->struct_union_decl = {name, fields, field_count};
        return s;
    }

    static Stmt *makeUnionDecl(ArenaAllocator &arena, std::string_view name, Field *fields,
                               uint32_t field_count, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::UNION_DECL;
        s->span = span;
        s->struct_union_decl = {name, fields, field_count};
        return s;
    }

    static Stmt *makeEnumDecl(ArenaAllocator &arena, std::string_view name,
                              std::string_view *names, Expr **values,
                              uint32_t count, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::ENUM_DECL;
        s->span = span;
        s->enum_decl = {name, names, values, count};
        return s;
    }

    static Stmt *makeImportDecl(ArenaAllocator &arena, std::string_view module_name, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::IMPORT_DECL;
        s->span = span;
        s->import_decl.module_name = module_name;
        return s;
    }

    static Stmt *makeIf(ArenaAllocator &arena, Expr *cond, Stmt *then_b, Stmt *else_b, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::IF_STMT;
        s->span = span;
        s->if_stmt = {cond, then_b, else_b};
        return s;
    }

    static Stmt *makeWhile(ArenaAllocator &arena, Expr *cond, Stmt *body, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::WHILE_STMT;
        s->span = span;
        s->while_stmt = {cond, body};
        return s;
    }

    static Stmt *makeDoWhile(ArenaAllocator &arena, Expr *cond, Stmt *body, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::DO_WHILE_STMT;
        s->span = span;
        s->while_stmt = {cond, body};
        return s;
    }

    static Stmt *makeFor(ArenaAllocator &arena, Stmt *init, Expr *cond, Expr *step, Stmt *body, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::FOR_STMT;
        s->span = span;
        s->for_stmt = {init, cond, step, body};
        return s;
    }

    static Stmt *makeSwitch(ArenaAllocator &arena, Expr *subject, CaseClause *cases,
                            uint32_t case_count, bool is_match, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = is_match ? StmtKind::MATCH_STMT : StmtKind::SWITCH_STMT;
        s->span = span;
        s->switch_stmt = {subject, cases, case_count};
        return s;
    }

    static Stmt *makeBreak(ArenaAllocator &arena, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::BREAK_STMT;
        s->span = span;
        return s;
    }

    static Stmt *makeContinue(ArenaAllocator &arena, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::CONTINUE_STMT;
        s->span = span;
        return s;
    }

    static Stmt *makeReturn(ArenaAllocator &arena, Expr *value, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::RETURN_STMT;
        s->span = span;
        s->return_stmt.value = value;
        return s;
    }

    static Stmt *makeDefer(ArenaAllocator &arena, Stmt *body, Span span)
    {
        Stmt *s = arena.create<Stmt>();
        s->kind = StmtKind::DEFER_STMT;
        s->span = span;
        s->defer_stmt.body = body;
        return s;
    }
};

struct SourceFile
{
    uint32_t file_id = 0;
    Stmt **top_level_decls = nullptr;
    uint32_t decl_count = 0;
    Span span;
};