#pragma once

#include "span.hpp"
#include <string_view>

enum class TokenKind
{
    END_OF_FILE_TOKEN,
    INVALID_TOKEN,

    INTEGER_TOKEN, // 123, 0x1A, 010, 0b101
    FLOAT_TOKEN,   // 3.14, 1e10, 0x1p4
    CHAR_TOKEN,    // 'a'
    STRING_TOKEN,  // "text"
    IDENTIFIER_TOKEN,

    I8_KEYWORD,    // i8
    I16_KEYWORD,   // i16
    I32_KEYWORD,   // i32
    I64_KEYWORD,   // i64

    U8_KEYWORD,    // u8
    U16_KEYWORD,   // u16
    U32_KEYWORD,   // u32
    U64_KEYWORD,   // u64

    F32_KEYWORD,   // f32
    F64_KEYWORD,   // f64

    BOOL_KEYWORD,  // bool
    CHAR_KEYWORD,  // char
    VOID_KEYWORD,  // void

    VAR_KEYWORD,
    CONST_KEYWORD,

    FUNC_KEYWORD,

    AS_KEYWORD,

    BREAK_KEYWORD,
    CASE_KEYWORD,
    CONTINUE_KEYWORD,
    DEFAULT_KEYWORD,
    DO_KEYWORD,
    ELSE_KEYWORD,
    FOR_KEYWORD,
    GOTO_KEYWORD,
    IF_KEYWORD,
    RETURN_KEYWORD,
    SIZEOF_KEYWORD,
    SWITCH_KEYWORD,
    WHILE_KEYWORD,

    ENUM_KEYWORD,
    STRUCT_KEYWORD,
    UNION_KEYWORD,

    EXTERN_KEYWORD,
    STATIC_KEYWORD,
    VOLATILE_KEYWORD,

    DEFER_KEYWORD,
    MATCH_KEYWORD,
    IMPORT_KEYWORD,
    PACKAGE_KEYWORD,

    PLUS_TOKEN,     // +
    MINUS_TOKEN,    // -
    MULTIPLY_TOKEN, // *
    DIVIDE_TOKEN,   // /
    MODULO_TOKEN,   // %

    INCREMENT_TOKEN, // ++
    DECREMENT_TOKEN, // --

    EQUAL_EQUAL_TOKEN,   // ==
    NOT_EQUAL_TOKEN,     // !=
    LESS_TOKEN,          //
    GREATER_TOKEN,       // >
    LESS_EQUAL_TOKEN,    // <=
    GREATER_EQUAL_TOKEN, // >=

    LOGICAL_AND_TOKEN, // &&
    LOGICAL_OR_TOKEN,  // ||
    LOGICAL_NOT_TOKEN, // !

    AMPERSAND_TOKEN,   // &
    PIPE_TOKEN,        // |
    CARET_TOKEN,       // ^
    TILDE_TOKEN,       // ~
    LEFT_SHIFT_TOKEN,  //
    RIGHT_SHIFT_TOKEN, // >>

    ASSIGN_TOKEN,             // =
    PLUS_ASSIGN_TOKEN,        // +=
    MINUS_ASSIGN_TOKEN,       // -=
    MULTIPLY_ASSIGN_TOKEN,    // *=
    DIVIDE_ASSIGN_TOKEN,      // /=
    MODULO_ASSIGN_TOKEN,      // %=
    AND_ASSIGN_TOKEN,         // &=
    OR_ASSIGN_TOKEN,          // |=
    XOR_ASSIGN_TOKEN,         // ^=
    LEFT_SHIFT_ASSIGN_TOKEN,  // <<=
    RIGHT_SHIFT_ASSIGN_TOKEN, // >>=

    LEFT_PAREN_TOKEN,    // (
    RIGHT_PAREN_TOKEN,   // )
    LEFT_BRACE_TOKEN,    // {
    RIGHT_BRACE_TOKEN,   // }
    LEFT_BRACKET_TOKEN,  // [
    RIGHT_BRACKET_TOKEN, // ]
    SEMICOLON_TOKEN,     // ;
    COLON_TOKEN,         // :
    COMMA_TOKEN,         // ,
    DOT_TOKEN,           // .
    ARROW_TOKEN,         // ->
    QUESTION_TOKEN,      // ?
    ELLIPSIS_TOKEN,      // ...

    HASH_TOKEN,      // #
    HASH_HASH_TOKEN, // ##

    LINE_COMMENT_TOKEN,  // //
    BLOCK_COMMENT_TOKEN, // /* */
};

typedef struct Token
{
    TokenKind kind = TokenKind::INVALID_TOKEN;
    Span span;

    std::string_view source;

    union
    {
        long long integer_value;
        double float_value;
        char *string_value;
    };
} Token;
