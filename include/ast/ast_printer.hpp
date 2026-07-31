#pragma once
#include "ast/ast.hpp"
#include <string>

class AstPrinter
{
public:
    static std::string print(SourceFile *file);

private:
    static void printStmt(Stmt *s, int depth, std::string &out);

    static void printExpr(Expr *e, int depth, std::string &out);

    static void printType(Type *t, std::string &out);

    static void indent(int depth, std::string &out);

    static const char *tokenOpName(TokenKind k);
};
