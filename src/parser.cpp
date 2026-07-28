#include "span.hpp"
#include "parser.hpp"
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <iostream>

const Token &Parser::current() const { return tokens_[pos_]; }

const Token &Parser::peek(int offset) const
{
    std::size_t idx = pos_ + offset;
    if (idx >= tokens_.size())
        return tokens_.back();
    return tokens_[idx];
}

const Token &Parser::advance()
{
    const Token &t = tokens_[pos_];
    if (!isAtEnd())
        pos_++;
    return t;
}

bool Parser::check(TokenKind kind) const
{
    return current().kind == kind;
}

bool Parser::match(TokenKind kind)
{
    if (check(kind))
    {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(TokenKind kind, const std::string &what)
{
    if (check(kind))
    {
        advance();
        return true;
    }
    errors_.error("expected " + what, current().span);
    return false;
}

bool Parser::isAtEnd() const
{
    return current().kind == TokenKind::END_OF_FILE_TOKEN;
}

Span Parser::spanFrom(const Token &start) const
{

    const Token &endTok = tokens_[pos_ > 0 ? pos_ - 1 : 0];
    return Span::merge(start.span, endTok.span);
}

void Parser::synchronize()
{

    while (!isAtEnd())
    {
        if (tokens_[pos_ > 0 ? pos_ - 1 : 0].kind == TokenKind::SEMICOLON_TOKEN)
            return;

        switch (current().kind)
        {
        case TokenKind::FUNC_KEYWORD:
        case TokenKind::STRUCT_KEYWORD:
        case TokenKind::ENUM_KEYWORD:
        case TokenKind::UNION_KEYWORD:
        case TokenKind::VAR_KEYWORD:
        case TokenKind::IF_KEYWORD:
        case TokenKind::WHILE_KEYWORD:
        case TokenKind::FOR_KEYWORD:
        case TokenKind::RETURN_KEYWORD:
        case TokenKind::IMPORT_KEYWORD:
            return;
        default:
            advance();
        }
    }
}

SourceFile *Parser::parseSourceFile()
{
    std::vector<Stmt *> decls;
    decls.reserve(16);

    Span fileStartSpan = current().span;

    while (!isAtEnd())
    {

        if (current().kind == TokenKind::LINE_COMMENT_TOKEN ||
            current().kind == TokenKind::BLOCK_COMMENT_TOKEN)
        {
            advance();
            continue;
        }

        Stmt *decl = parseTopLevelDecl();
        if (decl != nullptr)
            decls.push_back(decl);
        else
            synchronize();
    }

    SourceFile *file = arena_.create<SourceFile>();
    file->file_id = file_id_;
    file->decl_count = static_cast<uint32_t>(decls.size());
    file->top_level_decls = arena_.create_array<Stmt *>(decls.size());
    for (std::size_t i = 0; i < decls.size(); i++)
        file->top_level_decls[i] = decls[i];
    file->span = Span::merge(fileStartSpan, tokens_[pos_ > 0 ? pos_ - 1 : 0].span);

    return file;
}

Stmt *Parser::parseTopLevelDecl()
{
    if (check(TokenKind::IMPORT_KEYWORD))
        return parseImportDecl();
    if (check(TokenKind::EXTERN_KEYWORD))
    {
        advance();
        if (!expect(TokenKind::FUNC_KEYWORD, "'func' after 'extern'"))
            return nullptr;
        return parseFuncDecl(true);
    }
    if (check(TokenKind::FUNC_KEYWORD))
    {
        advance();
        return parseFuncDecl(false);
    }
    if (check(TokenKind::STRUCT_KEYWORD))
        return parseStructDecl();
    if (check(TokenKind::UNION_KEYWORD))
        return parseUnionDecl();
    if (check(TokenKind::ENUM_KEYWORD))
        return parseEnumDecl();
    if (check(TokenKind::VAR_KEYWORD))
    {
        advance();
        return parseVarDecl(true);
    }
    if (check(TokenKind::CONST_KEYWORD))
    {
        advance();
        return parseVarDecl(false);
    }

    errors_.error("expected a high level declaration (func, struct, enum, union, import, let, var)",
                  current().span);
    return nullptr;
}

Stmt *Parser::parseImportDecl()
{
    const Token &start = current();
    advance();

    if (!check(TokenKind::IDENTIFIER_TOKEN))
    {
        errors_.error("expected module's name after 'import'", current().span);
        return nullptr;
    }
    std::string_view name = advance().source;
    match(TokenKind::SEMICOLON_TOKEN);

    return Stmt::makeImportDecl(arena_, name, spanFrom(start));
}

Field *Parser::parseParamList(uint32_t &out_count, bool *isvariadic)
{
    std::vector<Field> tmp;
    tmp.reserve(4);

    if (!check(TokenKind::RIGHT_PAREN_TOKEN))
    {
        do
        {
            Field f;
            f.span = current().span;

            if (check(TokenKind::ELLIPSIS_TOKEN))
            {
                advance();
                *isvariadic = true;

                if (!check(TokenKind::RIGHT_PAREN_TOKEN))
                {
                    errors_.error("no parameters allowed after variadic '...'", current().span);
                    while (!check(TokenKind::RIGHT_PAREN_TOKEN) &&
                           !check(TokenKind::LEFT_BRACE_TOKEN) &&
                           !isAtEnd())
                    {
                        advance();
                    }
                }
                break;
            }

            if (!check(TokenKind::IDENTIFIER_TOKEN))
            {
                errors_.error("expected parameters name", current().span);
                break;
            }
            f.name = advance().source;

            if (!expect(TokenKind::COLON_TOKEN, "':' followed by the parameter's type"))
                break;

            f.type = parseType();

            if (match(TokenKind::ASSIGN_TOKEN))
                f.default_value = parseAssignment();

            tmp.push_back(f);
        } while (match(TokenKind::COMMA_TOKEN));
    }

    out_count = static_cast<uint32_t>(tmp.size());
    Field *fields = arena_.create_array<Field>(tmp.size());
    for (std::size_t i = 0; i < tmp.size(); i++)
        fields[i] = tmp[i];
    return fields;
}

Stmt *Parser::parseFuncDecl(bool is_extern)
{
    const Token &start = current();

    if (!check(TokenKind::IDENTIFIER_TOKEN))
    {
        errors_.error("expected function's name", current().span);
        return nullptr;
    }
    std::string_view name = advance().source;

    if (!expect(TokenKind::LEFT_PAREN_TOKEN, "'(' after the function's name"))
        return nullptr;

    uint32_t paramCount = 0;

    // SORRY I DIDNT HAD ANY IDEAS
    bool isvariadic = false;

    Field *params = parseParamList(paramCount, &isvariadic);

    expect(TokenKind::RIGHT_PAREN_TOKEN, "')' after the parameters");

    Type *returnType = nullptr;
    if (match(TokenKind::ARROW_TOKEN))
        returnType = parseType();

    Stmt *body = nullptr;
    if (is_extern)
    {
        match(TokenKind::SEMICOLON_TOKEN);
    }
    else
    {
        if (check(TokenKind::LEFT_BRACE_TOKEN))
            body = parseBlock();
        else
            errors_.error("expected functions body ('{...}')", current().span);
    }

    return Stmt::makeFuncDecl(arena_, name, params, paramCount, returnType, body, is_extern, isvariadic, returnType != nullptr, spanFrom(start));
}

Field *Parser::parseFieldList(TokenKind terminator, uint32_t &out_count)
{
    std::vector<Field> tmp;
    tmp.reserve(4);

    while (!check(terminator) && !isAtEnd())
    {
        Field f;
        f.span = current().span;

        if (!check(TokenKind::IDENTIFIER_TOKEN))
        {
            errors_.error("expected field's name", current().span);
            break;
        }
        f.name = advance().source;

        if (!expect(TokenKind::COLON_TOKEN, "':' followed by the field's type"))
            break;

        f.type = parseType();
        tmp.push_back(f);

        if (!match(TokenKind::COMMA_TOKEN))
            break;
    }

    out_count = static_cast<uint32_t>(tmp.size());
    Field *fields = arena_.create_array<Field>(tmp.size());
    for (std::size_t i = 0; i < tmp.size(); i++)
        fields[i] = tmp[i];
    return fields;
}

Stmt *Parser::parseStructDecl()
{
    const Token &start = current();
    advance();

    if (!check(TokenKind::IDENTIFIER_TOKEN))
    {
        errors_.error("expected struct's name", current().span);
        return nullptr;
    }
    std::string_view name = advance().source;

    expect(TokenKind::LEFT_BRACE_TOKEN, "'{' after the struct's name");
    uint32_t fieldCount = 0;
    Field *fields = parseFieldList(TokenKind::RIGHT_BRACE_TOKEN, fieldCount);
    expect(TokenKind::RIGHT_BRACE_TOKEN, "'}' at the end of the struct");

    return Stmt::makeStructDecl(arena_, name, fields, fieldCount, spanFrom(start));
}

Stmt *Parser::parseUnionDecl()
{
    const Token &start = current();
    advance();

    if (!check(TokenKind::IDENTIFIER_TOKEN))
    {
        errors_.error("expected union's name", current().span);
        return nullptr;
    }
    std::string_view name = advance().source;

    expect(TokenKind::LEFT_BRACE_TOKEN, "'{' after the union's name");
    uint32_t fieldCount = 0;
    Field *fields = parseFieldList(TokenKind::RIGHT_BRACE_TOKEN, fieldCount);
    expect(TokenKind::RIGHT_BRACE_TOKEN, "'}' at the end of the union");

    return Stmt::makeUnionDecl(arena_, name, fields, fieldCount, spanFrom(start));
}

Stmt *Parser::parseEnumDecl()
{
    const Token &start = current();
    advance();

    if (!check(TokenKind::IDENTIFIER_TOKEN))
    {
        errors_.error("expected enum's name", current().span);
        return nullptr;
    }
    std::string_view name = advance().source;

    expect(TokenKind::LEFT_BRACE_TOKEN, "'{' after the enum's name");

    std::vector<std::string_view> names;
    std::vector<Expr *> values;

    while (!check(TokenKind::RIGHT_BRACE_TOKEN) && !isAtEnd())
    {
        if (!check(TokenKind::IDENTIFIER_TOKEN))
        {
            errors_.error("expected enum's variant name", current().span);
            break;
        }
        names.push_back(advance().source);

        if (match(TokenKind::ASSIGN_TOKEN))
            values.push_back(parseAssignment());
        else
            values.push_back(nullptr);

        if (!match(TokenKind::COMMA_TOKEN))
            break;
    }

    expect(TokenKind::RIGHT_BRACE_TOKEN, "'}' at the end of the enum");

    uint32_t count = static_cast<uint32_t>(names.size());
    std::string_view *namesArr = arena_.create_array<std::string_view>(count);
    Expr **valuesArr = arena_.create_array<Expr *>(count);
    for (uint32_t i = 0; i < count; i++)
    {
        namesArr[i] = names[i];
        valuesArr[i] = values[i];
    }

    return Stmt::makeEnumDecl(arena_, name, namesArr, valuesArr, count, spanFrom(start));
}

Stmt *Parser::parseStmt()
{
    switch (current().kind)
    {
    case TokenKind::LEFT_BRACE_TOKEN:
        return parseBlock();
    case TokenKind::VAR_KEYWORD:
        advance();
        return parseVarDecl(true);
    case TokenKind::CONST_KEYWORD:
        advance();
        return parseVarDecl(false);
    case TokenKind::IF_KEYWORD:
        return parseIf();
    case TokenKind::WHILE_KEYWORD:
        return parseWhile();
    case TokenKind::DO_KEYWORD:
        return parseDoWhile();
    case TokenKind::FOR_KEYWORD:
        return parseFor();
    case TokenKind::SWITCH_KEYWORD:
        return parseSwitchOrMatch(false);
    case TokenKind::MATCH_KEYWORD:
        return parseSwitchOrMatch(true);
    case TokenKind::RETURN_KEYWORD:
        return parseReturn();
    case TokenKind::DEFER_KEYWORD:
        return parseDefer();
    case TokenKind::BREAK_KEYWORD:
    {
        const Token &start = advance();
        match(TokenKind::SEMICOLON_TOKEN);
        return Stmt::makeBreak(arena_, spanFrom(start));
    }
    case TokenKind::CONTINUE_KEYWORD:
    {
        const Token &start = advance();
        match(TokenKind::SEMICOLON_TOKEN);
        return Stmt::makeContinue(arena_, spanFrom(start));
    }
    default:
        return parseExprStmt();
    }
}

Stmt *Parser::parseBlock()
{
    const Token &start = current();
    expect(TokenKind::LEFT_BRACE_TOKEN, "'{'");

    std::vector<Stmt *> stmts;
    stmts.reserve(8);

    while (!check(TokenKind::RIGHT_BRACE_TOKEN) && !isAtEnd())
    {
        Stmt *s = parseStmt();
        if (s != nullptr)
            stmts.push_back(s);
        else
            synchronize();
    }

    expect(TokenKind::RIGHT_BRACE_TOKEN, "'}' closes the block");

    Stmt **arr = arena_.create_array<Stmt *>(stmts.size());
    for (std::size_t i = 0; i < stmts.size(); i++)
        arr[i] = stmts[i];

    return Stmt::makeBlock(arena_, arr, static_cast<uint32_t>(stmts.size()), spanFrom(start));
}

Stmt *Parser::parseVarDecl(bool is_mutable)
{
    const Token &start = tokens_[pos_ > 0 ? pos_ - 1 : 0];

    if (!check(TokenKind::IDENTIFIER_TOKEN))
    {
        errors_.error("expected variable's name", current().span);
        return nullptr;
    }
    std::string_view name = advance().source;

    Type *type = nullptr;
    if (match(TokenKind::COLON_TOKEN))
        type = parseType();

    Expr *init = nullptr;
    if (match(TokenKind::ASSIGN_TOKEN))
        init = parseExpr();

    match(TokenKind::SEMICOLON_TOKEN);

    return Stmt::makeVarDecl(arena_, name, type, init, is_mutable, false, spanFrom(start));
}

Stmt *Parser::parseIf()
{
    const Token &start = advance();
    expect(TokenKind::LEFT_PAREN_TOKEN, "'(' after 'if'");
    Expr *cond = parseExpr();
    expect(TokenKind::RIGHT_PAREN_TOKEN, "')' after the condition");

    Stmt *thenBranch = parseStmt();
    Stmt *elseBranch = nullptr;
    if (match(TokenKind::ELSE_KEYWORD))
        elseBranch = parseStmt();

    return Stmt::makeIf(arena_, cond, thenBranch, elseBranch, spanFrom(start));
}

Stmt *Parser::parseWhile()
{
    const Token &start = advance();
    expect(TokenKind::LEFT_PAREN_TOKEN, "'(' after 'while'");
    Expr *cond = parseExpr();
    expect(TokenKind::RIGHT_PAREN_TOKEN, "')' after the condition");
    Stmt *body = parseStmt();

    return Stmt::makeWhile(arena_, cond, body, spanFrom(start));
}

Stmt *Parser::parseDoWhile()
{
    const Token &start = advance();
    Stmt *body = parseStmt();
    expect(TokenKind::WHILE_KEYWORD, "'while' after the 'do' body");
    expect(TokenKind::LEFT_PAREN_TOKEN, "'(' after 'while'");
    Expr *cond = parseExpr();
    expect(TokenKind::RIGHT_PAREN_TOKEN, "')' after the condition");
    match(TokenKind::SEMICOLON_TOKEN);

    return Stmt::makeDoWhile(arena_, cond, body, spanFrom(start));
}

Stmt *Parser::parseFor()
{
    const Token &start = advance();
    expect(TokenKind::LEFT_PAREN_TOKEN, "'(' after 'for'");

    Stmt *init = nullptr;
    if (check(TokenKind::VAR_KEYWORD))
    {
        advance();
        init = parseVarDecl(true);
    }
    else if (check(TokenKind::CONST_KEYWORD))
    {
        advance();
        init = parseVarDecl(false);
    }
    else if (!check(TokenKind::SEMICOLON_TOKEN))
    {
        init = parseExprStmt();
    }
    else
    {
        advance();
    }

    Expr *cond = nullptr;
    if (!check(TokenKind::SEMICOLON_TOKEN))
        cond = parseExpr();
    expect(TokenKind::SEMICOLON_TOKEN, "';' after for's condition");

    Expr *step = nullptr;
    if (!check(TokenKind::RIGHT_PAREN_TOKEN))
        step = parseExpr();
    expect(TokenKind::RIGHT_PAREN_TOKEN, "')' after for");

    Stmt *body = parseStmt();

    return Stmt::makeFor(arena_, init, cond, step, body, spanFrom(start));
}

Stmt *Parser::parseSwitchOrMatch(bool is_match)
{
    const Token &start = advance();
    expect(TokenKind::LEFT_PAREN_TOKEN, "'(' after switch/match");
    Expr *subject = parseExpr();
    expect(TokenKind::RIGHT_PAREN_TOKEN, "')'");
    expect(TokenKind::LEFT_BRACE_TOKEN, "'{'");

    std::vector<CaseClause> cases;

    while (!check(TokenKind::RIGHT_BRACE_TOKEN) && !isAtEnd())
    {
        CaseClause cc;
        cc.span = current().span;

        if (match(TokenKind::CASE_KEYWORD))
        {
            cc.value = parseExpr();
        }
        else if (match(TokenKind::DEFAULT_KEYWORD))
        {
            cc.value = nullptr;
        }
        else
        {
            errors_.error("expected 'case' or 'default'", current().span);
            synchronize();
            continue;
        }

        expect(TokenKind::COLON_TOKEN, "':' after the case's tag");

        std::vector<Stmt *> body;
        while (!check(TokenKind::CASE_KEYWORD) && !check(TokenKind::DEFAULT_KEYWORD) &&
               !check(TokenKind::RIGHT_BRACE_TOKEN) && !isAtEnd())
        {
            Stmt *s = parseStmt();
            if (s != nullptr)
                body.push_back(s);
            else
                synchronize();
        }

        cc.body_count = static_cast<uint32_t>(body.size());
        cc.body = arena_.create_array<Stmt *>(body.size());
        for (std::size_t i = 0; i < body.size(); i++)
            cc.body[i] = body[i];

        cases.push_back(cc);
    }

    expect(TokenKind::RIGHT_BRACE_TOKEN, "'}' after the switch/match body");

    CaseClause *arr = arena_.create_array<CaseClause>(cases.size());
    for (std::size_t i = 0; i < cases.size(); i++)
        arr[i] = cases[i];

    return Stmt::makeSwitch(arena_, subject, arr, static_cast<uint32_t>(cases.size()),
                            is_match, spanFrom(start));
}

Stmt *Parser::parseReturn()
{
    const Token &start = advance();
    Expr *value = nullptr;
    if (!check(TokenKind::SEMICOLON_TOKEN))
        value = parseExpr();
    match(TokenKind::SEMICOLON_TOKEN);

    return Stmt::makeReturn(arena_, value, spanFrom(start));
}

Stmt *Parser::parseDefer()
{
    const Token &start = advance();
    Stmt *body = parseStmt();
    return Stmt::makeDefer(arena_, body, spanFrom(start));
}

Stmt *Parser::parseExprStmt()
{
    const Token &start = current();
    Expr *e = parseExpr();
    match(TokenKind::SEMICOLON_TOKEN);
    return Stmt::makeExprStmt(arena_, e, spanFrom(start));
}

bool Parser::isTypeKeyword(TokenKind kind) const
{
    switch (kind)
    {
    case TokenKind::I8_KEYWORD:
    case TokenKind::I16_KEYWORD:
    case TokenKind::I32_KEYWORD:
    case TokenKind::I64_KEYWORD:
    case TokenKind::U8_KEYWORD:
    case TokenKind::U16_KEYWORD:
    case TokenKind::U32_KEYWORD:
    case TokenKind::U64_KEYWORD:
    case TokenKind::F32_KEYWORD:
    case TokenKind::F64_KEYWORD:
    case TokenKind::BOOL_KEYWORD:
    case TokenKind::CHAR_KEYWORD:
    case TokenKind::VOID_KEYWORD:
        return true;
    default:
        return false;
    }
}

Type *Parser::parseType()
{
    const Token &start = current();
    Type *base = nullptr;

    static const std::unordered_map<TokenKind, TypeKind> primMap = {
        {TokenKind::I8_KEYWORD, TypeKind::I8},
        {TokenKind::I16_KEYWORD, TypeKind::I16},
        {TokenKind::I32_KEYWORD, TypeKind::I32},
        {TokenKind::I64_KEYWORD, TypeKind::I64},
        {TokenKind::U8_KEYWORD, TypeKind::U8},
        {TokenKind::U16_KEYWORD, TypeKind::U16},
        {TokenKind::U32_KEYWORD, TypeKind::U32},
        {TokenKind::U64_KEYWORD, TypeKind::U64},
        {TokenKind::F32_KEYWORD, TypeKind::F32},
        {TokenKind::F64_KEYWORD, TypeKind::F64},
        {TokenKind::BOOL_KEYWORD, TypeKind::BOOL},
        {TokenKind::CHAR_KEYWORD, TypeKind::CHAR},
        {TokenKind::VOID_KEYWORD, TypeKind::VOID},
    };

    if (isTypeKeyword(current().kind))
    {
        base = Type::makePrimitive(arena_, primMap.at(current().kind), current().span);
        advance();
    }
    else if (check(TokenKind::IDENTIFIER_TOKEN))
    {
        base = Type::makeNamed(arena_, current().source, current().span);
        advance();
    }
    else if (check(TokenKind::FUNC_KEYWORD))
    {
        advance();
        expect(TokenKind::LEFT_PAREN_TOKEN, "'(' in function type");
        std::vector<Type *> params;
        if (!check(TokenKind::RIGHT_PAREN_TOKEN))
        {
            do
            {
                params.push_back(parseType());
            } while (match(TokenKind::COMMA_TOKEN));
        }
        expect(TokenKind::RIGHT_PAREN_TOKEN, "')' in function type");
        Type *ret = nullptr;
        if (match(TokenKind::ARROW_TOKEN))
            ret = parseType();

        Type **arr = arena_.create_array<Type *>(params.size());
        for (std::size_t i = 0; i < params.size(); i++)
            arr[i] = params[i];

        base = Type::makeFunc(arena_, arr, static_cast<uint32_t>(params.size()), ret, spanFrom(start));
    }
    else
    {
        errors_.error("expected a type", current().span);
        return Type::makePrimitive(arena_, TypeKind::INVALID, current().span);
    }

    for (;;)
    {
        if (check(TokenKind::MULTIPLY_TOKEN))
        {
            advance();
            base = Type::makePointer(arena_, base, spanFrom(start));
        }
        else if (check(TokenKind::QUESTION_TOKEN))
        {
            advance();
            base = Type::makeOptional(arena_, base, spanFrom(start));
        }
        else if (check(TokenKind::LEFT_BRACKET_TOKEN))
        {
            advance();
            if (check(TokenKind::ELLIPSIS_TOKEN))
            {
                advance();
                expect(TokenKind::RIGHT_BRACKET_TOKEN, "']' after '...'");
                base = Type::makeVariadic(arena_, base, spanFrom(start));
            }
            else if (check(TokenKind::INTEGER_TOKEN))
            {
                uint64_t size = static_cast<uint64_t>(current().integer_value);
                advance();
                expect(TokenKind::RIGHT_BRACKET_TOKEN, "']' after array size");
                base = Type::makeArray(arena_, base, size, spanFrom(start));
            }
            else
            {
                expect(TokenKind::RIGHT_BRACKET_TOKEN, "']' (slice)");
                base = Type::makeSlice(arena_, base, spanFrom(start));
            }
        }
        else
        {
            break;
        }
    }

    return base;
}

bool Parser::isAssignOp(TokenKind kind) const
{
    switch (kind)
    {
    case TokenKind::ASSIGN_TOKEN:
    case TokenKind::PLUS_ASSIGN_TOKEN:
    case TokenKind::MINUS_ASSIGN_TOKEN:
    case TokenKind::MULTIPLY_ASSIGN_TOKEN:
    case TokenKind::DIVIDE_ASSIGN_TOKEN:
    case TokenKind::MODULO_ASSIGN_TOKEN:
    case TokenKind::AND_ASSIGN_TOKEN:
    case TokenKind::OR_ASSIGN_TOKEN:
    case TokenKind::XOR_ASSIGN_TOKEN:
    case TokenKind::LEFT_SHIFT_ASSIGN_TOKEN:
    case TokenKind::RIGHT_SHIFT_ASSIGN_TOKEN:
        return true;
    default:
        return false;
    }
}

Expr *Parser::parseExpr() { return parseAssignment(); }

Expr *Parser::parseAssignment()
{
    const Token &start = current();
    Expr *left = parseTernary();

    if (isAssignOp(current().kind))
    {
        TokenKind op = advance().kind;
        Expr *right = parseAssignment();
        return Expr::makeAssign(arena_, op, left, right, spanFrom(start));
    }
    return left;
}

Expr *Parser::parseTernary()
{
    const Token &start = current();
    Expr *cond = parseLogicalOr();

    if (match(TokenKind::QUESTION_TOKEN))
    {
        Expr *thenExpr = parseAssignment();
        expect(TokenKind::COLON_TOKEN, "':' in ternary operator");
        Expr *elseExpr = parseAssignment();
        return Expr::makeTernary(arena_, cond, thenExpr, elseExpr, spanFrom(start));
    }
    return cond;
}

#define BIN_LEVEL(NAME, NEXT, ...)                                             \
    Expr *Parser::NAME()                                                       \
    {                                                                          \
        const Token &start = current();                                        \
        Expr *left = NEXT();                                                   \
        while (__VA_ARGS__)                                                    \
        {                                                                      \
            TokenKind op = advance().kind;                                     \
            Expr *right = NEXT();                                              \
            left = Expr::makeBinary(arena_, op, left, right, spanFrom(start)); \
        }                                                                      \
        return left;                                                           \
    }

BIN_LEVEL(parseLogicalOr, parseLogicalAnd, check(TokenKind::LOGICAL_OR_TOKEN))
BIN_LEVEL(parseLogicalAnd, parseBitwiseOr, check(TokenKind::LOGICAL_AND_TOKEN))
BIN_LEVEL(parseBitwiseOr, parseBitwiseXor, check(TokenKind::PIPE_TOKEN))
BIN_LEVEL(parseBitwiseXor, parseBitwiseAnd, check(TokenKind::CARET_TOKEN))
BIN_LEVEL(parseBitwiseAnd, parseEquality, check(TokenKind::AMPERSAND_TOKEN))
BIN_LEVEL(parseEquality, parseRelational,
          check(TokenKind::EQUAL_EQUAL_TOKEN) || check(TokenKind::NOT_EQUAL_TOKEN))
BIN_LEVEL(parseRelational, parseShift,
          check(TokenKind::LESS_TOKEN) || check(TokenKind::GREATER_TOKEN) ||
              check(TokenKind::LESS_EQUAL_TOKEN) || check(TokenKind::GREATER_EQUAL_TOKEN))
BIN_LEVEL(parseShift, parseAdditive,
          check(TokenKind::LEFT_SHIFT_TOKEN) || check(TokenKind::RIGHT_SHIFT_TOKEN))
BIN_LEVEL(parseAdditive, parseMultiplicative,
          check(TokenKind::PLUS_TOKEN) || check(TokenKind::MINUS_TOKEN))
BIN_LEVEL(parseMultiplicative, parseUnary,
          check(TokenKind::MULTIPLY_TOKEN) || check(TokenKind::DIVIDE_TOKEN) || check(TokenKind::MODULO_TOKEN))

#undef BIN_LEVEL

Expr *Parser::parseUnary()
{
    const Token &start = current();

    if (check(TokenKind::LOGICAL_NOT_TOKEN) || check(TokenKind::TILDE_TOKEN) ||
        check(TokenKind::MINUS_TOKEN) || check(TokenKind::PLUS_TOKEN) ||
        check(TokenKind::MULTIPLY_TOKEN) || check(TokenKind::AMPERSAND_TOKEN))
    {
        TokenKind op = advance().kind;
        Expr *operand = parseUnary();
        return Expr::makeUnary(arena_, op, operand, spanFrom(start));
    }

    if (check(TokenKind::INCREMENT_TOKEN) || check(TokenKind::DECREMENT_TOKEN))
    {
        TokenKind op = advance().kind;
        Expr *operand = parseUnary();
        return Expr::makePreIncDec(arena_, op, operand, spanFrom(start));
    }

    if (check(TokenKind::SIZEOF_KEYWORD))
    {
        advance();
        expect(TokenKind::LEFT_PAREN_TOKEN, "'(' after sizeof");

        if (isTypeKeyword(current().kind))
        {
            Type *t = parseType();
            expect(TokenKind::RIGHT_PAREN_TOKEN, "')' after sizeof(T)");
            return Expr::makeSizeofType(arena_, t, spanFrom(start));
        }
        Expr *e = parseExpr();
        expect(TokenKind::RIGHT_PAREN_TOKEN, "')' after sizeof(expr)");
        return Expr::makeSizeofExpr(arena_, e, spanFrom(start));
    }

    return parsePostfix();
}

Expr *Parser::parsePostfix()
{
    const Token &start = current();
    Expr *e = parsePrimary();

    for (;;)
    {
        if (check(TokenKind::LEFT_PAREN_TOKEN))
        {
            advance();
            std::vector<Expr *> tmp;
            if (!check(TokenKind::RIGHT_PAREN_TOKEN))
            {
                do
                {
                    tmp.push_back(parseAssignment());
                } while (match(TokenKind::COMMA_TOKEN));
            }
            expect(TokenKind::RIGHT_PAREN_TOKEN, "')' after arguments");

            Expr **args = arena_.create_array<Expr *>(tmp.size());
            for (std::size_t i = 0; i < tmp.size(); i++)
                args[i] = tmp[i];

            e = Expr::makeCall(arena_, e, args, static_cast<uint32_t>(tmp.size()), spanFrom(start));
        }
        else if (check(TokenKind::LEFT_BRACKET_TOKEN))
        {
            advance();
            Expr *idx = parseExpr();
            expect(TokenKind::RIGHT_BRACKET_TOKEN, "']' after the index");
            e = Expr::makeIndex(arena_, e, idx, spanFrom(start));
        }
        else if (check(TokenKind::DOT_TOKEN))
        {
            advance();
            if (!check(TokenKind::IDENTIFIER_TOKEN))
            {
                errors_.error("expected field's name after '.'", current().span);
                break;
            }
            std::string_view field = advance().source;
            e = Expr::makeMember(arena_, e, field, spanFrom(start));
        }
        else if (check(TokenKind::ARROW_TOKEN))
        {
            advance();
            if (!check(TokenKind::IDENTIFIER_TOKEN))
            {
                errors_.error("expected field's name after '->'", current().span);
                break;
            }
            std::string_view field = advance().source;
            e = Expr::makeArrowMember(arena_, e, field, spanFrom(start));
        }
        else if (check(TokenKind::INCREMENT_TOKEN) || check(TokenKind::DECREMENT_TOKEN))
        {
            TokenKind op = advance().kind;
            e = Expr::makePostIncDec(arena_, op, e, spanFrom(start));
        }
        else
        {
            break;
        }
    }

    return e;
}

Expr *Parser::parsePrimary()
{
    const Token &start = current();

    switch (current().kind)
    {
    case TokenKind::INTEGER_TOKEN:
    {
        int64_t v = advance().integer_value;
        return Expr::makeLiteralInt(arena_, v, spanFrom(start));
    }
    case TokenKind::FLOAT_TOKEN:
    {
        double v = advance().float_value;
        return Expr::makeLiteralFloat(arena_, v, spanFrom(start));
    }
    case TokenKind::CHAR_TOKEN:
    {
        char v = advance().string_value[0];
        return Expr::makeLiteralChar(arena_, v, spanFrom(start));
    }
    case TokenKind::STRING_TOKEN:
    {
        const char *v = advance().string_value;
        return Expr::makeLiteralString(arena_, v, static_cast<uint32_t>(std::strlen(v)), spanFrom(start));
    }
    case TokenKind::IDENTIFIER_TOKEN:
    {
        std::string_view name = advance().source;
        return Expr::makeIdentifier(arena_, name, false, spanFrom(start));
    }
    case TokenKind::LEFT_BRACKET_TOKEN:
    {
        advance();
        std::vector<Expr *> tmp;
        if (!check(TokenKind::RIGHT_BRACKET_TOKEN))
        {
            do
            {
                tmp.push_back(parseAssignment());
            } while (match(TokenKind::COMMA_TOKEN));
        }
        expect(TokenKind::RIGHT_BRACKET_TOKEN, "']' after array literal");

        Expr **values = arena_.create_array<Expr *>(tmp.size());
        for (std::size_t i = 0; i < tmp.size(); i++)
            values[i] = tmp[i];

        return Expr::makeLiteralArray(arena_, values, static_cast<uint32_t>(tmp.size()), spanFrom(start));
    }
    case TokenKind::LEFT_PAREN_TOKEN:
    {
        advance();

        if (isTypeKeyword(current().kind) &&
            (peek(1).kind == TokenKind::RIGHT_PAREN_TOKEN))
        {
            Type *t = parseType();
            expect(TokenKind::RIGHT_PAREN_TOKEN, "')' after cast type");
            Expr *operand = parseUnary();
            return Expr::makeCast(arena_, operand, t, spanFrom(start));
        }

        Expr *inner = parseExpr();
        expect(TokenKind::RIGHT_PAREN_TOKEN, "')' closing expression");
        return inner;
    }
    default:
        errors_.error("expected a expression", current().span);
        advance();
        return Expr::makeLiteralInt(arena_, 0, spanFrom(start));
    }
}