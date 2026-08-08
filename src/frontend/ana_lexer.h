#ifndef ANA_LEXER_H
#define ANA_LEXER_H

#include <stdint.h>
#include <stddef.h>
#include "ana_ast.h"

namespace ana {
namespace frontend {

enum class TokenType {
    TOKEN_EOF,
    TOKEN_FN,           // .fn
    TOKEN_END_FN,       // .end_fn
    TOKEN_CLASS,        // .class
    TOKEN_END_CLASS,    // .end_class
    TOKEN_FIELD,        // .field
    TOKEN_REGISTERS,    // .registers
    TOKEN_IMPORT_SYS,   // .import-sys
    TOKEN_OPCODE,
    TOKEN_IDENTIFIER,
    TOKEN_LABEL,        // label:
    TOKEN_REGISTER,     // p0..pN, v0..vN
    TOKEN_INT_LITERAL,
    TOKEN_LBRACKET,     // [
    TOKEN_RBRACKET,     // ]
    TOKEN_LPAREN,       // (
    TOKEN_RPAREN,       // )
    TOKEN_COLON,        // :
    TOKEN_COMMA,        // ,
    TOKEN_PLUS,         // +
    TOKEN_MINUS,        // -
    TOKEN_ARROW,        // ->
    TOKEN_TYPE,         // i32, i64, ptr, f32, void
    TOKEN_LIKELY,       // likely
    TOKEN_UNLIKELY,     // unlikely
    TOKEN_STRING_LITERAL // "string literal"
};

struct StringView {
    const char* str;
    size_t length;

    bool equals(const char* other) const {
        size_t i = 0;
        while (i < length && other[i] != '\0') {
            if (str[i] != other[i]) return false;
            i++;
        }
        return i == length && other[i] == '\0';
    }
};

struct Token {
    TokenType type;
    StringView view;
    Opcode opcode;
    DataType data_type;
    int64_t int_val;
    const char* string_val;
    size_t string_len;
    uint64_t string_hash;
    Register reg;
    uint32_t line;
};

// Compile-time FNV-1a hash for fast string view lookup
constexpr uint64_t fnv1a_hash(const char* str, size_t len) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(str[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

class Lexer {
private:
    const char* source_;
    size_t cursor_;
    uint32_t line_;

    char peek() const;
    char advance();
    void skip_whitespace_and_comments();

public:
    explicit Lexer(const char* source);
    Token next_token();
};

} // namespace frontend
} // namespace ana

#endif // ANA_LEXER_H
