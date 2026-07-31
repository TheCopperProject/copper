#pragma once

#include "ast/span.hpp"
#include "ast/ast.hpp"
#include "ast/lexer.hpp"
#include "errors.hpp"
#include "memory/arena.hpp"

#include <vector>
#include <cstdint>

class Parser {
public:
    Parser(std::vector<Token> tokens, ArenaAllocator& arena,
           ErrorCollector& errors, uint32_t file_id)
        : tokens_(std::move(tokens)),
          arena_(arena),
          errors_(errors),
          file_id_(file_id) {}

    SourceFile* parseSourceFile();

private:
    std::vector<Token> tokens_;
    ArenaAllocator& arena_;
    ErrorCollector& errors_;
    uint32_t file_id_;
    std::size_t pos_ = 0;

    const Token& current() const;
    const Token& peek(int offset = 0) const;
    const Token& advance();
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    bool expect(TokenKind kind, const std::string& what);
    bool isAtEnd() const;
    Span spanFrom(const Token& start) const;

    void synchronize();

    Stmt* parseTopLevelDecl();

    PackageDecl* parsePackageDecl();
    ImportDecl* parseImportDecl();

    Stmt* parseFuncDecl(bool is_extern);
    Stmt* parseStructDecl();
    Stmt* parseUnionDecl();
    Stmt* parseEnumDecl();

    Field* parseFieldList(TokenKind terminator, uint32_t& out_count);
    Field* parseParamList(uint32_t& out_count, bool* isvariadic);

    Stmt* parseStmt();
    Stmt* parseBlock();
    Stmt* parseVarDecl(bool is_mutable);
    Stmt* parseIf();
    Stmt* parseWhile();
    Stmt* parseDoWhile();
    Stmt* parseFor();
    Stmt* parseSwitchOrMatch(bool is_match);
    Stmt* parseReturn();
    Stmt* parseDefer();
    Stmt* parseExprStmt();

    Type* parseType();
    bool isTypeKeyword(TokenKind kind) const;

    Expr* parseExpr();
    Expr* parseAssignment();
    Expr* parseTernary();
    Expr* parseLogicalOr();
    Expr* parseLogicalAnd();
    Expr* parseBitwiseOr();
    Expr* parseBitwiseXor();
    Expr* parseBitwiseAnd();
    Expr* parseEquality();
    Expr* parseRelational();
    Expr* parseShift();
    Expr* parseAdditive();
    Expr* parseMultiplicative();
    Expr* parseUnary();
    Expr* parsePostfix();
    Expr* parsePrimary();

    bool isAssignOp(TokenKind kind) const;
};
