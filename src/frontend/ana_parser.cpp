#include "ana_parser.h"

namespace ana {
namespace frontend {

Parser::Parser(const char* source, ArenaAllocator& arena)
    : lexer_(source), current_tok_(), arena_(arena),
      had_error_(false), error_msg_(nullptr), error_line_(0) {
    // current_tok_ was previously left uninitialised and read by advance().
    advance();
}

Token Parser::advance() {
    Token prev = current_tok_;
    current_tok_ = lexer_.next_token();
    if (current_tok_.type == TokenType::TOKEN_ERROR && !had_error_) {
        had_error_ = true;
        error_msg_ = "Invalid character in source";
        error_line_ = current_tok_.line;
    }
    return prev;
}

bool Parser::match(TokenType type) {
    if (current_tok_.type == type) {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(TokenType type, const char* errmsg) {
    if (current_tok_.type == type) {
        advance();
        return true;
    }
    // The message used to be discarded and every caller ignored the return
    // value, so malformed input silently produced a garbage AST.
    if (!had_error_) {
        had_error_ = true;
        error_msg_ = errmsg;
        error_line_ = current_tok_.line;
    }
    return false;
}

const char* Parser::allocate_string(StringView view) {
    char* str = static_cast<char*>(arena_.alloc(view.length + 1));
    if (!str) return "";
    for (size_t i = 0; i < view.length; ++i) {
        str[i] = view.str[i];
    }
    str[view.length] = '\0';
    return str;
}

Program* Parser::parse_program() {
    Program* prog = arena_.create<Program>();
    prog->classes = nullptr;
    prog->functions = nullptr;

    ClassDecl** last_class = &prog->classes;
    Function** last_func = &prog->functions;

    while (current_tok_.type != TokenType::TOKEN_EOF) {
        if (current_tok_.type == TokenType::TOKEN_CLASS) {
            ClassDecl* cls = parse_class();
            if (cls) {
                *last_class = cls;
                last_class = &cls->next;
            }
        } else if (current_tok_.type == TokenType::TOKEN_FN) {
            Function* fn = parse_function();
            if (fn) {
                *last_func = fn;
                last_func = &fn->next;
            }
        } else {
            advance();
        }
    }

    resolve_class_layouts(prog);
    // Never hand back a partially-parsed program as if it were valid.
    if (had_error_) return nullptr;
    return prog;
}

ClassDecl* Parser::parse_class() {
    expect(TokenType::TOKEN_CLASS, "Expected .class");
    if (current_tok_.type != TokenType::TOKEN_IDENTIFIER) {
        // Latch the failure: silently returning nullptr let a malformed
        // '.class' with no name parse as a valid empty program.
        if (!had_error_) { had_error_ = true; error_msg_ = "Expected class name after .class"; }
        return nullptr;
    }

    ClassDecl* cls = arena_.create<ClassDecl>();
    cls->name = allocate_string(current_tok_.view);
    cls->fields = nullptr;
    cls->size = 8; // Offset 0 reserved for VTablePtr
    cls->vtable_array = nullptr;
    cls->vtable_size = 0;
    cls->next = nullptr;
    advance();

    ClassField** last_field = &cls->fields;

    while (current_tok_.type != TokenType::TOKEN_END_CLASS && current_tok_.type != TokenType::TOKEN_EOF) {
        if (current_tok_.type == TokenType::TOKEN_FIELD) {
            advance();
            // The lexer folds "name:" into a single TOKEN_LABEL and consumes the
            // colon, so a field declaration arrives as LABEL TYPE -- not as
            // IDENTIFIER COLON TYPE. Matching only the latter meant every
            // .field was silently dropped: classes ended up with no fields and
            // a bare 8-byte size, so instances were under-allocated and any
            // field access aliased the vtable pointer at offset 0.
            if (current_tok_.type == TokenType::TOKEN_LABEL ||
                current_tok_.type == TokenType::TOKEN_IDENTIFIER) {
                const bool colon_consumed = (current_tok_.type == TokenType::TOKEN_LABEL);
                ClassField* field = arena_.create<ClassField>();
                field->name = allocate_string(current_tok_.view);
                field->next = nullptr; // never leave the list unterminated
                advance();
                if (!colon_consumed) expect(TokenType::TOKEN_COLON, "Expected : after field name");
                if (current_tok_.type != TokenType::TOKEN_TYPE) {
                    if (!had_error_) { had_error_ = true; error_msg_ = "Expected field type"; }
                    return nullptr;
                }
                field->type = current_tok_.data_type;
                advance();

                uint32_t type_size = (field->type == DataType::I64 || field->type == DataType::PTR) ? 8 : 4;
                // Align field offset
                cls->size = (cls->size + (type_size - 1)) & ~(type_size - 1);
                field->offset = cls->size;
                cls->size += type_size;

                *last_field = field;
                last_field = &field->next;
            } else {
                if (!had_error_) { had_error_ = true; error_msg_ = "Expected field name after .field"; }
                return nullptr;
            }
        } else {
            advance();
        }
    }
    expect(TokenType::TOKEN_END_CLASS, "Expected .end_class");

    // Allocate 64-byte cache-line aligned VTable array (default capacity 16 slots)
    cls->vtable_size = 16;
    cls->vtable_array = static_cast<void**>(arena_.alloc(cls->vtable_size * sizeof(void*), 64));
    for (uint32_t i = 0; i < cls->vtable_size; ++i) {
        cls->vtable_array[i] = nullptr;
    }

    return cls;
}

Function* Parser::parse_function() {
    expect(TokenType::TOKEN_FN, "Expected .fn");
    if (current_tok_.type != TokenType::TOKEN_IDENTIFIER) return nullptr;

    Function* fn = arena_.create<Function>();
    fn->name = allocate_string(current_tok_.view);
    fn->params = nullptr;
    fn->local_count = 0;
    fn->first_block = nullptr;
    fn->next = nullptr;
    advance();

    expect(TokenType::TOKEN_LPAREN, "Expected (");
    Parameter** last_param = &fn->params;
    uint32_t param_idx = 0;

    while (current_tok_.type != TokenType::TOKEN_RPAREN && current_tok_.type != TokenType::TOKEN_EOF) {
        if (current_tok_.type == TokenType::TOKEN_REGISTER || current_tok_.type == TokenType::TOKEN_IDENTIFIER) {
            Parameter* param = arena_.create<Parameter>();
            param->name = allocate_string(current_tok_.view);
            param->reg.type = RegisterType::PARAM;
            param->reg.index = param_idx++;
            advance();
            expect(TokenType::TOKEN_COLON, "Expected :");
            param->type = current_tok_.data_type;
            advance();

            *last_param = param;
            last_param = &param->next;
            if (current_tok_.type == TokenType::TOKEN_COMMA) advance();
        } else {
            advance();
        }
    }
    fn->param_count = param_idx;
    expect(TokenType::TOKEN_RPAREN, "Expected )");

    if (match(TokenType::TOKEN_ARROW)) {
        fn->return_type = current_tok_.data_type;
        advance();
    }

    BasicBlock** last_block = &fn->first_block;

    while (current_tok_.type != TokenType::TOKEN_END_FN && current_tok_.type != TokenType::TOKEN_EOF) {
        if (current_tok_.type == TokenType::TOKEN_REGISTERS) {
            advance();
            if (current_tok_.type == TokenType::TOKEN_INT_LITERAL) {
                fn->local_count = static_cast<uint32_t>(current_tok_.int_val);
                advance();
            }
        } else {
            BasicBlock* bb = parse_basic_block();
            if (bb) {
                apply_dce(bb);
                *last_block = bb;
                last_block = &bb->next;
            } else {
                advance();
            }
        }
    }

    expect(TokenType::TOKEN_END_FN, "Expected .end_fn");
    return fn;
}

BasicBlock* Parser::parse_basic_block() {
    BasicBlock* bb = arena_.create<BasicBlock>();
    bb->label = "entry";
    bb->first_insn = nullptr;
    bb->last_insn = nullptr;
    bb->next = nullptr;

    if (current_tok_.type == TokenType::TOKEN_LABEL) {
        bb->label = allocate_string(current_tok_.view);
        advance();
    }

    Instruction** last_insn = &bb->first_insn;

    while (current_tok_.type != TokenType::TOKEN_END_FN &&
           current_tok_.type != TokenType::TOKEN_LABEL &&
           current_tok_.type != TokenType::TOKEN_EOF) {
        if (current_tok_.type == TokenType::TOKEN_OPCODE) {
            Instruction* insn = parse_instruction();
            if (insn) {
                optimize_instruction(insn);
                *last_insn = insn;
                bb->last_insn = insn;
                last_insn = &insn->next;
            }
        } else {
            advance();
        }
    }

    return bb;
}

Instruction* Parser::parse_instruction() {
    Instruction* insn = arena_.create<Instruction>();
    insn->op = current_tok_.opcode;
    insn->dest = {};
    insn->src1 = {};
    insn->src2 = {};
    insn->vtable_slot = 0;
    insn->target_label = nullptr;
    insn->next = nullptr;
    advance();

    // Parse operands according to opcode requirements
    switch (insn->op) {
        case Opcode::ADD_I32:
        case Opcode::SUB_I32:
        case Opcode::MUL_I32:
        case Opcode::DIV_I32:
        case Opcode::ADD_I64:
        case Opcode::SUB_I64:
        case Opcode::MUL_I64:
        case Opcode::DIV_I64:
        case Opcode::AND_I32:
        case Opcode::AND_I64:
        case Opcode::OR_I32:
        case Opcode::OR_I64:
        case Opcode::XOR_I32:
        case Opcode::XOR_I64:
        case Opcode::SHL_I32:
        case Opcode::SHL_I64:
        case Opcode::SHR_I32:
        case Opcode::SHR_I64:
        case Opcode::USHR_I32:
        case Opcode::USHR_I64:
        case Opcode::BTS_I64:
        case Opcode::BTR_I64:
        case Opcode::ADD_FLOAT_32:
        case Opcode::ADD_FLOAT_64:
        case Opcode::SUB_FLOAT_64:
        case Opcode::MUL_FLOAT_64:
        case Opcode::DIV_FLOAT_64:
        case Opcode::ADD_VECTOR_I32X4:
        case Opcode::SUB_VECTOR_I32X4: {
            bool have_dest = false;
            bool have_src1 = false;
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->dest = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                have_dest = true;
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->src1 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                have_src1 = true;
                advance();
            } else if (current_tok_.type == TokenType::TOKEN_INT_LITERAL) {
                insn->src1 = Operand::make_const(current_tok_.int_val);
                have_src1 = true;
                advance();
            }
            // A zero-initialised Operand is indistinguishable from register 0,
            // so a truncated operand list used to be accepted and silently
            // compiled as if it referenced v0. Reject it instead.
            if (!have_dest || !have_src1) {
                if (!had_error_) { had_error_ = true; error_msg_ = "Missing operand in arithmetic instruction"; }
            }
            if (match(TokenType::TOKEN_COMMA)) {
                if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                    insn->src2 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                    advance();
                } else if (current_tok_.type == TokenType::TOKEN_INT_LITERAL) {
                    insn->src2 = Operand::make_const(current_tok_.int_val);
                    advance();
                }
            }
            break;
        }
        case Opcode::NEG_I32:
        case Opcode::NEG_I64:
        case Opcode::POPCOUNT_I64:
        case Opcode::LZCNT_I64: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->dest = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->src1 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
            } else if (current_tok_.type == TokenType::TOKEN_INT_LITERAL) {
                insn->src1 = Operand::make_const(current_tok_.int_val);
                advance();
            }
            break;
        }
        case Opcode::CONST_STRING: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->dest = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_STRING_LITERAL) {
                insn->string_val = current_tok_.string_val;
                insn->string_len = current_tok_.string_len;
                insn->string_hash = current_tok_.string_hash;
                advance();
            }
            break;
        }
        case Opcode::STR_LEN: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->dest = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->src1 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
            } else if (current_tok_.type == TokenType::TOKEN_STRING_LITERAL) {
                insn->string_val = current_tok_.string_val;
                insn->string_len = current_tok_.string_len;
                insn->string_hash = current_tok_.string_hash;
                advance();
            }
            break;
        }
        case Opcode::GOTO: {
            if (current_tok_.type == TokenType::TOKEN_IDENTIFIER || current_tok_.type == TokenType::TOKEN_LABEL) {
                insn->target_label = allocate_string(current_tok_.view);
                advance();
            }
            break;
        }
        case Opcode::IF_EQ:
        case Opcode::IF_NE:
        case Opcode::IF_LT:
        case Opcode::IF_GE: {
            if (current_tok_.type == TokenType::TOKEN_LIKELY) {
                insn->is_likely = true;
                advance();
            } else if (current_tok_.type == TokenType::TOKEN_UNLIKELY) {
                insn->is_likely = false;
                advance();
            }
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->src1 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->src2 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
            } else if (current_tok_.type == TokenType::TOKEN_INT_LITERAL) {
                insn->src2 = Operand::make_const(current_tok_.int_val);
                advance();
            }
            if (match(TokenType::TOKEN_COMMA)) {
                if (current_tok_.type == TokenType::TOKEN_IDENTIFIER || current_tok_.type == TokenType::TOKEN_LABEL) {
                    insn->target_label = allocate_string(current_tok_.view);
                    advance();
                }
            }
            break;
        }
        case Opcode::IF_Z:
        case Opcode::IF_NZ: {
            if (current_tok_.type == TokenType::TOKEN_LIKELY) {
                insn->is_likely = true;
                advance();
            } else if (current_tok_.type == TokenType::TOKEN_UNLIKELY) {
                insn->is_likely = false;
                advance();
            }
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->src1 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
            }
            if (match(TokenType::TOKEN_COMMA)) {
                if (current_tok_.type == TokenType::TOKEN_IDENTIFIER || current_tok_.type == TokenType::TOKEN_LABEL) {
                    insn->target_label = allocate_string(current_tok_.view);
                    advance();
                }
            }
            break;
        }
        case Opcode::ATOMIC_CAS_I64: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->dest = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (match(TokenType::TOKEN_LBRACKET)) {
                Register base_reg = current_tok_.reg;
                advance();
                int32_t off = 0;
                if (match(TokenType::TOKEN_PLUS)) {
                    off = static_cast<int32_t>(current_tok_.int_val);
                    advance();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ]");
                insn->src1 = Operand::make_mem(base_reg.type, base_reg.index, off);
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->src2 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
            }
            break;
        }
        case Opcode::ATOMIC_XCHG_I64:
        case Opcode::ATOMIC_ADD_I64:
        case Opcode::ATOMIC_AND_I64:
        case Opcode::ATOMIC_OR_I64: {
            if (match(TokenType::TOKEN_LBRACKET)) {
                Register base_reg = current_tok_.reg;
                advance();
                int32_t off = 0;
                if (match(TokenType::TOKEN_PLUS)) {
                    off = static_cast<int32_t>(current_tok_.int_val);
                    advance();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ]");
                insn->dest = Operand::make_mem(base_reg.type, base_reg.index, off);
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->src1 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
            } else if (current_tok_.type == TokenType::TOKEN_INT_LITERAL) {
                insn->src1 = Operand::make_const(current_tok_.int_val);
                advance();
            }
            break;
        }
        case Opcode::FENCE: {
            break;
        }
        case Opcode::NEW_INSTANCE: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->dest = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_IDENTIFIER || current_tok_.type == TokenType::TOKEN_LABEL) {
                insn->target_label = allocate_string(current_tok_.view);
                advance();
            }
            break;
        }
        case Opcode::MOVE_CONST: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->dest = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_INT_LITERAL) {
                insn->src1 = Operand::make_const(current_tok_.int_val);
                advance();
            }
            break;
        }
        case Opcode::MOVE: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->dest = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->src1 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
            }
            break;
        }
        case Opcode::LOAD_MEM: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->dest = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (match(TokenType::TOKEN_LBRACKET)) {
                Register base_reg = current_tok_.reg;
                advance();
                int32_t off = 0;
                if (match(TokenType::TOKEN_PLUS)) {
                    off = static_cast<int32_t>(current_tok_.int_val);
                    advance();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ]");
                insn->src1 = Operand::make_mem(base_reg.type, base_reg.index, off);
            }
            break;
        }
        case Opcode::STORE_MEM: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                // Form 1: store-mem src_reg, [base + offset]
                Operand src_op = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
                if (match(TokenType::TOKEN_LBRACKET)) {
                    Register base_reg = current_tok_.reg;
                    advance();
                    int32_t off = 0;
                    if (match(TokenType::TOKEN_PLUS)) {
                        off = static_cast<int32_t>(current_tok_.int_val);
                        advance();
                    }
                    expect(TokenType::TOKEN_RBRACKET, "Expected ]");
                    insn->dest = Operand::make_mem(base_reg.type, base_reg.index, off);
                    insn->src1 = src_op;
                }
            } else if (match(TokenType::TOKEN_LBRACKET)) {
                // Form 2: store-mem [base + offset], src
                Register base_reg = current_tok_.reg;
                advance();
                int32_t off = 0;
                if (match(TokenType::TOKEN_PLUS)) {
                    off = static_cast<int32_t>(current_tok_.int_val);
                    advance();
                }
                expect(TokenType::TOKEN_RBRACKET, "Expected ]");
                insn->dest = Operand::make_mem(base_reg.type, base_reg.index, off);
                expect(TokenType::TOKEN_COMMA, "Expected ,");
                if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                    insn->src1 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                    advance();
                } else if (current_tok_.type == TokenType::TOKEN_INT_LITERAL) {
                    insn->src1 = Operand::make_const(current_tok_.int_val);
                    advance();
                }
            }
            break;
        }
        case Opcode::CALL_VIRT:
        case Opcode::CALL_VIRT_FAST: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->src1 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index); // Object ptr (p0)
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_INT_LITERAL) {
                insn->vtable_slot = static_cast<int32_t>(current_tok_.int_val);
                advance();
            }
            if (match(TokenType::TOKEN_ARROW)) {
                if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                    insn->dest = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                    advance();
                }
            }
            break;
        }
        case Opcode::BIND_VTABLE: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->dest = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
                expect(TokenType::TOKEN_COMMA, "Expected ,");
            }
            if (current_tok_.type == TokenType::TOKEN_INT_LITERAL) {
                insn->src1 = Operand::make_const(current_tok_.int_val);
                advance();
            }
            break;
        }
        case Opcode::RETURN_VAL: {
            if (current_tok_.type == TokenType::TOKEN_REGISTER) {
                insn->src1 = Operand::make_reg(current_tok_.reg.type, current_tok_.reg.index);
                advance();
            } else if (current_tok_.type == TokenType::TOKEN_INT_LITERAL) {
                insn->src1 = Operand::make_const(current_tok_.int_val);
                advance();
            }
            break;
        }
        default:
            break;
    }

    return insn;
}

void Parser::optimize_instruction(Instruction* insn) {
    // AST Constant Folding
    if (insn->src1.kind == OperandKind::CONST_INT && insn->src2.kind == OperandKind::CONST_INT) {
        int64_t v1 = insn->src1.const_val;
        int64_t v2 = insn->src2.const_val;
        int64_t res = 0;
        bool folded = false;

        // Every fold used 64-bit signed arithmetic, which disagreed with what
        // the /32 opcodes compute at runtime, invoked signed-overflow UB on
        // +, - and *, and invoked shift UB for counts >= 64. Folding now
        // mirrors the backend exactly: unsigned wrapping math, explicit 32-bit
        // narrowing for the /32 family, and hardware-masked shift counts.
        const uint64_t u1 = static_cast<uint64_t>(v1);
        const uint64_t u2 = static_cast<uint64_t>(v2);
        const int32_t w1 = static_cast<int32_t>(v1);
        const int32_t w2 = static_cast<int32_t>(v2);
        const uint32_t x1 = static_cast<uint32_t>(w1);
        const uint32_t x2 = static_cast<uint32_t>(w2);
        const uint32_t sh32 = static_cast<uint32_t>(u2) & 31u;
        const uint32_t sh64 = static_cast<uint32_t>(u2) & 63u;

        switch (insn->op) {
            // ---- 32-bit: wrap at 32 bits, then sign-extend (matches the JIT)
            case Opcode::ADD_I32: res = static_cast<int32_t>(x1 + x2); folded = true; break;
            case Opcode::SUB_I32: res = static_cast<int32_t>(x1 - x2); folded = true; break;
            case Opcode::MUL_I32: res = static_cast<int32_t>(x1 * x2); folded = true; break;
            case Opcode::DIV_I32:
                if (w2 == 0) break;                       // left for the runtime guard
                res = (w2 == -1) ? static_cast<int32_t>(0u - x1)
                                 : static_cast<int32_t>(w1 / w2);
                folded = true; break;
            case Opcode::AND_I32: res = static_cast<int32_t>(x1 & x2); folded = true; break;
            case Opcode::OR_I32:  res = static_cast<int32_t>(x1 | x2); folded = true; break;
            case Opcode::XOR_I32: res = static_cast<int32_t>(x1 ^ x2); folded = true; break;
            case Opcode::SHL_I32: res = static_cast<int32_t>(x1 << sh32); folded = true; break;
            case Opcode::SHR_I32: res = static_cast<int32_t>(w1 >> sh32); folded = true; break;
            case Opcode::USHR_I32: res = static_cast<int32_t>(x1 >> sh32); folded = true; break;

            // ---- 64-bit
            case Opcode::ADD_I64: res = static_cast<int64_t>(u1 + u2); folded = true; break;
            case Opcode::SUB_I64: res = static_cast<int64_t>(u1 - u2); folded = true; break;
            case Opcode::MUL_I64: res = static_cast<int64_t>(u1 * u2); folded = true; break;
            case Opcode::DIV_I64:
                if (v2 == 0) break;
                res = (v2 == -1) ? static_cast<int64_t>(0ULL - u1) : (v1 / v2);
                folded = true; break;
            case Opcode::AND_I64: res = v1 & v2; folded = true; break;
            case Opcode::OR_I64:  res = v1 | v2; folded = true; break;
            case Opcode::XOR_I64: res = v1 ^ v2; folded = true; break;
            case Opcode::SHL_I64: res = static_cast<int64_t>(u1 << sh64); folded = true; break;
            case Opcode::SHR_I64: res = v1 >> sh64; folded = true; break;
            case Opcode::USHR_I64: res = static_cast<int64_t>(u1 >> sh64); folded = true; break;
            default: break;
        }

        if (folded) {
            insn->op = Opcode::MOVE_CONST;
            insn->src1 = Operand::make_const(res);
            insn->src2.kind = OperandKind::CONST_INT;
            insn->src2.const_val = 0;
        }
    }
}

void Parser::apply_dce(BasicBlock* block) {
    if (!block || !block->first_insn) return;

    Instruction* curr = block->first_insn;
    while (curr) {
        if (curr->op == Opcode::RETURN_VOID || curr->op == Opcode::RETURN_VAL || curr->op == Opcode::GOTO) {
            // Drop all subsequent instructions in this block
            curr->next = nullptr;
            block->last_insn = curr;
            break;
        }
        curr = curr->next;
    }
}

void Parser::resolve_class_layouts(Program* prog) {
    // This was a no-op, so ClassDecl::size stayed 0, every field offset stayed
    // 0 (all fields aliased each other) and `new-instance` always fell back to
    // a hard-coded 16-byte allocation.
    if (!prog) return;

    for (ClassDecl* cls = prog->classes; cls != nullptr; cls = cls->next) {
        // Offset 0 holds the vtable pointer, so instance fields start at 8.
        // (Recomputing from 0 here would overlay the first field on the vtable
        // pointer -- exactly the kind of silent corruption this pass exists to
        // prevent.) This mirrors the layout parse_class() assigns inline.
        uint32_t offset = 8;
        for (ClassField* f = cls->fields; f != nullptr; f = f->next) {
            uint32_t sz = (f->type == DataType::I64 || f->type == DataType::PTR) ? 8u : 4u;
            offset = (offset + (sz - 1)) & ~(sz - 1);
            f->offset = offset;
            offset += sz;
        }
        cls->size = (offset + 7) & ~7u;
        if (cls->size < 8) cls->size = 8;
    }
}

} // namespace frontend
} // namespace ana
