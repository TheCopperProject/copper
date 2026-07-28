#pragma once

#include "ast.hpp"
#include "errors.hpp"
#include "arena.hpp"
#include "symbol_table.hpp"

#include <string>
#include <unordered_map>

class Sema
{
public:
    Sema(ArenaAllocator &arena, ErrorCollector &errors);

    void analyze(SourceFile *file);

private:
    bool isGlobal;

    ArenaAllocator &arena_;
    ErrorCollector &errors_;
    SymbolTable symbols_;

    std::unordered_map<std::string, Stmt *> types_;
    std::unordered_map<TypeKind, Type *> primitive_cache_;

    Type *current_return_type_ = nullptr;
    std::string current_function_name_;
    int loop_depth_ = 0;
    int switch_depth_ = 0;

    void collectTypes(SourceFile *file);
    void validateTypeBodies(SourceFile *file);
    void collectSignatures(SourceFile *file);
    void checkTopLevelBodies(SourceFile *file);
    Type* inferReturnType(Stmt *body, bool &hasReturn);
    void collectReturnTypes(Stmt *s, std::vector<Type *> &types, bool &hasVoid);

    void checkFuncBody(Stmt *decl);
    void checkGlobalVarInit(Stmt *decl);

    void checkStmt(Stmt *s);
    void checkBlock(Stmt *s);
    void checkVarDecl(Stmt *s);
    void checkIf(Stmt *s);
    void checkWhile(Stmt *s);
    void checkDoWhile(Stmt *s);
    void checkFor(Stmt *s);
    void checkSwitch(Stmt *s);
    void checkReturn(Stmt *s);

    Type *checkExpr(Expr *e);
    Type *checkBinary(Expr *e);
    Type *checkUnary(Expr *e);
    Type *checkAssign(Expr *e);
    Type *checkCall(Expr *e);
    bool isFormattable(Type *t);
    Type *checkIndex(Expr *e);
    Type *checkMember(Expr *e);
    Type *checkCast(Expr *e);
    Type *checkSizeof(Expr *e);
    Type *checkTernary(Expr *e);
    Type *checkIncDec(TokenKind op, Expr *operand, Span span);
    Type *checkLiteralArray(Expr *e);

    bool stmtAlwaysReturns(Stmt *s);

    Type *resolveType(Type *t);
    Type *getPrimitive(TypeKind kind);

    bool typesEqual(Type *a, Type *b);
    Type *promoteNumeric(Type *a, Type *b, bool &ok);
    bool isAssignable(Type *from, Type *to);
    bool isValidCast(Type *from, Type *to);

    bool isIntegerType(Type *t);
    bool isFloatType(Type *t);
    bool isNumericType(Type *t);
    bool isUnsignedType(Type *t);
    bool isBoolType(Type *t);
    bool isScalarType(Type *t);
    int bitWidth(TypeKind k);

    bool isLValue(Expr *e);
    bool checkAssignableTarget(Expr *target);

    std::string typeToString(Type *t);
    std::string opName(TokenKind k);

    void error(const std::string &message, const Span &span);
};
