#include "lexer.hpp"
#include "arena.hpp"
#include "token.hpp"

#include <cctype>
#include <cstring>
#include <unordered_map>
#include <map>
#include <string>

static const std::unordered_map<std::string, TokenKind> keywordMap = {
    {"i8", TokenKind::I8_KEYWORD},
    {"i16", TokenKind::I16_KEYWORD},
    {"i32", TokenKind::I32_KEYWORD},
    {"i64", TokenKind::I64_KEYWORD},
    {"u8", TokenKind::U8_KEYWORD},
    {"u16", TokenKind::U16_KEYWORD},
    {"u32", TokenKind::U32_KEYWORD},
    {"u64", TokenKind::U64_KEYWORD},
    {"f32", TokenKind::F32_KEYWORD},
    {"f64", TokenKind::F64_KEYWORD},
    {"bool", TokenKind::BOOL_KEYWORD},
    {"char", TokenKind::CHAR_KEYWORD},
    {"void", TokenKind::VOID_KEYWORD},
    {"var", TokenKind::VAR_KEYWORD},
    {"const", TokenKind::CONST_KEYWORD},
    {"func", TokenKind::FUNC_KEYWORD},
    {"as", TokenKind::AS_KEYWORD},
    {"break", TokenKind::BREAK_KEYWORD},
    {"case", TokenKind::CASE_KEYWORD},
    {"continue", TokenKind::CONTINUE_KEYWORD},
    {"default", TokenKind::DEFAULT_KEYWORD},
    {"do", TokenKind::DO_KEYWORD},
    {"else", TokenKind::ELSE_KEYWORD},
    {"for", TokenKind::FOR_KEYWORD},
    {"goto", TokenKind::GOTO_KEYWORD},
    {"if", TokenKind::IF_KEYWORD},
    {"return", TokenKind::RETURN_KEYWORD},
    {"sizeof", TokenKind::SIZEOF_KEYWORD},
    {"switch", TokenKind::SWITCH_KEYWORD},
    {"while", TokenKind::WHILE_KEYWORD},
    {"enum", TokenKind::ENUM_KEYWORD},
    {"struct", TokenKind::STRUCT_KEYWORD},
    {"union", TokenKind::UNION_KEYWORD},
    {"extern", TokenKind::EXTERN_KEYWORD},
    {"static", TokenKind::STATIC_KEYWORD},
    {"volatile", TokenKind::VOLATILE_KEYWORD},
    {"defer", TokenKind::DEFER_KEYWORD},
    {"match", TokenKind::MATCH_KEYWORD},
    {"import", TokenKind::IMPORT_KEYWORD},
    {"package", TokenKind::PACKAGE_KEYWORD},
};

static std::map<std::string, uint32_t> file_ids = {};

static uint32_t file_counter = 0;

static bool isIdentStart(char c) {
    return std::isalpha((unsigned char) c) || c == '_';
}

static bool isIdentCont(char c) {
    return std::isalnum((unsigned char) c) || c == '_';
}

static bool isHexDigit(char c) {
    return std::isxdigit((unsigned char) c);
}

std::vector<Token> tokenify(const char* source_name, const char* source,
                            ArenaAllocator& arena) {
    std::vector<Token> tokens;

    const std::size_t len = std::strlen(source);

    uint32_t line = 1;
    uint32_t column = 1;
    uint32_t pos = 0;

    uint32_t file_id = 0;
    auto it = file_ids.find(source_name);
    if (it != file_ids.end()) {
        file_id = it->second;
    } else {
        file_ids[source_name] = file_counter;
        file_id = file_counter++;
    }

    auto peek = [&](int offset = 0) -> char {
        std::size_t p = pos + offset;
        return p < len ? source[p] : '\0';
    };

    auto advance = [&]() -> char {
        char c = source[pos++];
        if (c == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
        return c;
    };

    auto makeSpan = [&](uint32_t startLine, uint32_t startCol,
                        uint32_t startOff) -> Span {
        Span s;
        s.start_line = startLine;
        s.start_column = startCol;
        s.end_line = line;
        s.end_column = column;
        s.offset = startOff;
        s.length = pos - startOff;
        s.file_id = file_id;
        return s;
    };

    auto pushToken = [&](TokenKind kind, uint32_t startLine, uint32_t startCol,
                         uint32_t startOff) {
        Token t;
        t.kind = kind;
        t.span = makeSpan(startLine, startCol, startOff);
        t.source = std::string_view(source + startOff, pos - startOff);
        tokens.push_back(t);
    };

    while (pos < len) {
        char c = peek();

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }

        uint32_t startLine = line;
        uint32_t startCol = column;
        uint32_t startOff = pos;

        if (c == '/' && peek(1) == '/') {
            while (pos < len && peek() != '\n')
                advance();
            pushToken(TokenKind::LINE_COMMENT_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            advance();
            advance();
            while (pos < len && !(peek() == '*' && peek(1) == '/'))
                advance();
            if (pos < len) {
                advance();
                advance();
            }
            pushToken(TokenKind::BLOCK_COMMENT_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }

        if (isIdentStart(c)) {
            while (pos < len && isIdentCont(peek()))
                advance();
            std::string lexeme(source + startOff, pos - startOff);

            auto kwIt = keywordMap.find(lexeme);
            TokenKind kind = (kwIt != keywordMap.end())
                                 ? kwIt->second
                                 : TokenKind::IDENTIFIER_TOKEN;
            pushToken(kind, startLine, startCol, startOff);
            continue;
        }

        if (std::isdigit((unsigned char) c)) {
            bool isFloat = false;

            if (c == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                advance();
                advance();
                while (pos < len && (isHexDigit(peek()) || peek() == '.')) {
                    if (peek() == '.')
                        isFloat = true;
                    advance();
                }
                if (peek() == 'p' || peek() == 'P') {
                    isFloat = true;
                    advance();
                    if (peek() == '+' || peek() == '-')
                        advance();
                    while (pos < len && std::isdigit((unsigned char) peek()))
                        advance();
                }
            } else if (c == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
                advance();
                advance();
                while (pos < len && (peek() == '0' || peek() == '1'))
                    advance();
            } else {
                while (pos < len && std::isdigit((unsigned char) peek()))
                    advance();
                if (peek() == '.' && std::isdigit((unsigned char) peek(1))) {
                    isFloat = true;
                    advance();
                    while (pos < len && std::isdigit((unsigned char) peek()))
                        advance();
                }
                if (peek() == 'e' || peek() == 'E') {
                    isFloat = true;
                    advance();
                    if (peek() == '+' || peek() == '-')
                        advance();
                    while (pos < len && std::isdigit((unsigned char) peek()))
                        advance();
                }
            }

            std::string lexeme(source + startOff, pos - startOff);
            Token t;
            t.kind =
                isFloat ? TokenKind::FLOAT_TOKEN : TokenKind::INTEGER_TOKEN;
            t.span = makeSpan(startLine, startCol, startOff);
            t.source = std::string_view(source + startOff, pos - startOff);
            if (isFloat)
                t.float_value = std::strtod(lexeme.c_str(), nullptr);
            else
                t.integer_value = std::strtoll(lexeme.c_str(), nullptr, 0);
            tokens.push_back(t);
            continue;
        }

        if (c == '\'') {
            advance();
            char* buf = arena.create_array<char>(2);
            std::size_t n = 0;

            if (peek() == '\\') {
                advance();
                char esc = advance();
                switch (esc) {
                    case 'n':
                        buf[n++] = '\n';
                        break;
                    case 't':
                        buf[n++] = '\t';
                        break;
                    case '0':
                        buf[n++] = '\0';
                        break;
                    case 'r':
                        buf[n++] = '\r';
                        break;
                    case '\\':
                        buf[n++] = '\\';
                        break;
                    case '\'':
                        buf[n++] = '\'';
                        break;
                    default:
                        buf[n++] = esc;
                        break;
                }
            } else if (pos < len && peek() != '\'') {
                buf[n++] = advance();
            }

            buf[n] = '\0';

            if (peek() == '\'') {
                advance();
            }

            Token t;
            t.kind = TokenKind::CHAR_TOKEN;
            t.span = makeSpan(startLine, startCol, startOff);
            t.source = std::string_view(source + startOff, pos - startOff);
            t.string_value = buf;
            tokens.push_back(t);
            continue;
        }

        if (c == '"') {
            advance();
            std::string tmp;
            tmp.reserve(16);

            while (pos < len && peek() != '"') {
                char ch = advance();
                if (ch == '\\' && pos < len) {
                    char esc = advance();
                    switch (esc) {
                        case 'n':
                            tmp.push_back('\n');
                            break;
                        case 't':
                            tmp.push_back('\t');
                            break;
                        case '0':
                            tmp.push_back('\0');
                            break;
                        case '\\':
                            tmp.push_back('\\');
                            break;
                        case '"':
                            tmp.push_back('"');
                            break;
                        default:
                            tmp.push_back(esc);
                            break;
                    }
                } else {
                    tmp.push_back(ch);
                }
            }
            if (peek() == '"')
                advance();

            char* buf = arena.create_array<char>(tmp.size() + 1);
            std::memcpy(buf, tmp.data(), tmp.size());
            buf[tmp.size()] = '\0';

            Token t;
            t.kind = TokenKind::STRING_TOKEN;
            t.span = makeSpan(startLine, startCol, startOff);
            t.source = std::string_view(source + startOff, pos - startOff);
            t.string_value = buf;
            tokens.push_back(t);
            continue;
        }

        auto match3 = [&](const char* s) {
            return peek(0) == s[0] && peek(1) == s[1] && peek(2) == s[2];
        };
        auto match2 = [&](const char* s) {
            return peek(0) == s[0] && peek(1) == s[1];
        };

        if (match3("...")) {
            advance();
            advance();
            advance();
            pushToken(TokenKind::ELLIPSIS_TOKEN, startLine, startCol, startOff);
            continue;
        }
        if (match3("<<=")) {
            advance();
            advance();
            advance();
            pushToken(TokenKind::LEFT_SHIFT_ASSIGN_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match3(">>=")) {
            advance();
            advance();
            advance();
            pushToken(TokenKind::RIGHT_SHIFT_ASSIGN_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }

        if (match2("+=")) {
            advance();
            advance();
            pushToken(TokenKind::PLUS_ASSIGN_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("-=")) {
            advance();
            advance();
            pushToken(TokenKind::MINUS_ASSIGN_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("*=")) {
            advance();
            advance();
            pushToken(TokenKind::MULTIPLY_ASSIGN_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("/=")) {
            advance();
            advance();
            pushToken(TokenKind::DIVIDE_ASSIGN_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("%=")) {
            advance();
            advance();
            pushToken(TokenKind::MODULO_ASSIGN_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("&=")) {
            advance();
            advance();
            pushToken(TokenKind::AND_ASSIGN_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("|=")) {
            advance();
            advance();
            pushToken(TokenKind::OR_ASSIGN_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("^=")) {
            advance();
            advance();
            pushToken(TokenKind::XOR_ASSIGN_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("==")) {
            advance();
            advance();
            pushToken(TokenKind::EQUAL_EQUAL_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("!=")) {
            advance();
            advance();
            pushToken(TokenKind::NOT_EQUAL_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("<=")) {
            advance();
            advance();
            pushToken(TokenKind::LESS_EQUAL_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2(">=")) {
            advance();
            advance();
            pushToken(TokenKind::GREATER_EQUAL_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("&&")) {
            advance();
            advance();
            pushToken(TokenKind::LOGICAL_AND_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("||")) {
            advance();
            advance();
            pushToken(TokenKind::LOGICAL_OR_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("++")) {
            advance();
            advance();
            pushToken(TokenKind::INCREMENT_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("--")) {
            advance();
            advance();
            pushToken(TokenKind::DECREMENT_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("<<")) {
            advance();
            advance();
            pushToken(TokenKind::LEFT_SHIFT_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2(">>")) {
            advance();
            advance();
            pushToken(TokenKind::RIGHT_SHIFT_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }
        if (match2("->")) {
            advance();
            advance();
            pushToken(TokenKind::ARROW_TOKEN, startLine, startCol, startOff);
            continue;
        }
        if (match2("##")) {
            advance();
            advance();
            pushToken(TokenKind::HASH_HASH_TOKEN, startLine, startCol,
                      startOff);
            continue;
        }

        advance();
        TokenKind kind;
        switch (c) {
            case '+':
                kind = TokenKind::PLUS_TOKEN;
                break;
            case '-':
                kind = TokenKind::MINUS_TOKEN;
                break;
            case '*':
                kind = TokenKind::MULTIPLY_TOKEN;
                break;
            case '/':
                kind = TokenKind::DIVIDE_TOKEN;
                break;
            case '%':
                kind = TokenKind::MODULO_TOKEN;
                break;
            case '<':
                kind = TokenKind::LESS_TOKEN;
                break;
            case '>':
                kind = TokenKind::GREATER_TOKEN;
                break;
            case '!':
                kind = TokenKind::LOGICAL_NOT_TOKEN;
                break;
            case '&':
                kind = TokenKind::AMPERSAND_TOKEN;
                break;
            case '|':
                kind = TokenKind::PIPE_TOKEN;
                break;
            case '^':
                kind = TokenKind::CARET_TOKEN;
                break;
            case '~':
                kind = TokenKind::TILDE_TOKEN;
                break;
            case '=':
                kind = TokenKind::ASSIGN_TOKEN;
                break;
            case '(':
                kind = TokenKind::LEFT_PAREN_TOKEN;
                break;
            case ')':
                kind = TokenKind::RIGHT_PAREN_TOKEN;
                break;
            case '{':
                kind = TokenKind::LEFT_BRACE_TOKEN;
                break;
            case '}':
                kind = TokenKind::RIGHT_BRACE_TOKEN;
                break;
            case '[':
                kind = TokenKind::LEFT_BRACKET_TOKEN;
                break;
            case ']':
                kind = TokenKind::RIGHT_BRACKET_TOKEN;
                break;
            case ';':
                kind = TokenKind::SEMICOLON_TOKEN;
                break;
            case ':':
                kind = TokenKind::COLON_TOKEN;
                break;
            case ',':
                kind = TokenKind::COMMA_TOKEN;
                break;
            case '.':
                kind = TokenKind::DOT_TOKEN;
                break;
            case '?':
                kind = TokenKind::QUESTION_TOKEN;
                break;
            case '#':
                kind = TokenKind::HASH_TOKEN;
                break;
            default:
                kind = TokenKind::INVALID_TOKEN;
                break;
        }
        pushToken(kind, startLine, startCol, startOff);
    }

    Token eof;
    eof.kind = TokenKind::END_OF_FILE_TOKEN;
    eof.span = makeSpan(line, column, pos);
    tokens.push_back(eof);

    return tokens;
}
