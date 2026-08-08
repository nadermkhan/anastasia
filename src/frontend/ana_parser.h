#ifndef ANA_PARSER_H
#define ANA_PARSER_H

#include "ana_lexer.h"
#include "arena_allocator.h"

namespace ana {
namespace frontend {

class Parser {
private:
    Lexer lexer_;
    Token current_tok_;
    ArenaAllocator& arena_;
    bool had_error_;
    const char* error_msg_;
    uint32_t error_line_;

    Token advance();
    bool match(TokenType type);
    bool expect(TokenType type, const char* errmsg);

    const char* allocate_string(StringView view);
    ClassDecl* parse_class();
    Function* parse_function();
    BasicBlock* parse_basic_block();
    Instruction* parse_instruction();

    // AST-level Optimizations
    void optimize_instruction(Instruction* insn);
    void apply_dce(BasicBlock* block);
    void resolve_class_layouts(Program* prog);

public:
    Parser(const char* source, ArenaAllocator& arena);
    Program* parse_program();

    // parse_program() returns nullptr on malformed input; these describe why.
    bool has_error() const { return had_error_; }
    const char* error_message() const { return error_msg_ ? error_msg_ : ""; }
    uint32_t error_line() const { return error_line_; }
};

} // namespace frontend
} // namespace ana

#endif // ANA_PARSER_H
