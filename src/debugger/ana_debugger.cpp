#include "ana_debugger.h"
#include "../sys/object_heap.h"
#include <cstdlib>

namespace ana {
namespace debugger {

static inline bool streq_impl(const char* s1, const char* s2) {
    if (!s1 || !s2) return false;
    if (s1[0] == '.') s1++;
    if (s2[0] == '.') s2++;
    while (*s1 && *s2 && *s1 != ':' && *s2 != ':') {
        if (*s1 != *s2) return false;
        s1++; s2++;
    }
    char c1 = (*s1 == ':') ? '\0' : *s1;
    char c2 = (*s2 == ':') ? '\0' : *s2;
    return c1 == c2;
}

static void print_out(const char* str) {
    if (str) sys::raw_write(1, str, sys::freestanding_strlen(str));
}

static void print_num(int64_t n) {
    char buf[32];
    int pos = 30;
    buf[31] = '\0';
    bool neg = n < 0;
    uint64_t val = neg ? static_cast<uint64_t>(-n) : static_cast<uint64_t>(n);
    if (val == 0) {
        buf[pos--] = '0';
    } else {
        while (val > 0) {
            buf[pos--] = '0' + (val % 10);
            val /= 10;
        }
    }
    if (neg) buf[pos--] = '-';
    print_out(&buf[pos + 1]);
}

static void print_hex(uint64_t val) {
    print_out("0x");
    char hex_digits[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; --i) {
        uint8_t nibble = (val >> (i * 4)) & 0xF;
        char c[2] = { hex_digits[nibble], '\0' };
        print_out(c);
    }
}

AnaDebugger::AnaDebugger()
    : program_(nullptr), current_function_(nullptr), current_block_(nullptr),
      current_instruction_(nullptr), breakpoint_count_(0), current_line_(1), is_finished_(false) {
    sys::freestanding_memset(params_, 0, sizeof(params_));
    sys::freestanding_memset(registers_, 0, sizeof(registers_));
}

AnaDebugger::~AnaDebugger() {}

bool AnaDebugger::load_program_from_source(const char* source) {
    if (!source) return false;

    frontend::Parser parser(source, arena_);
    program_ = parser.parse_program();
    if (!program_ || !program_->functions) return false;

    current_function_ = program_->functions;
    current_block_ = current_function_->first_block;
    current_instruction_ = current_block_ ? current_block_->first_insn : nullptr;
    current_line_ = 1;
    is_finished_ = false;
    return true;
}

bool AnaDebugger::load_program_from_file(const char* filepath) {
    if (!filepath) return false;

    int fd = sys::raw_open(filepath, 0, 0);
    if (fd < 0) return false;

    char* buf = static_cast<char*>(malloc(64 * 1024));
    int64_t bytes_read = sys::raw_read(fd, buf, 64 * 1024 - 1);
    sys::raw_close(fd);

    if (bytes_read <= 0) {
        free(buf);
        return false;
    }
    buf[bytes_read] = '\0';

    bool ok = load_program_from_source(buf);
    free(buf);
    return ok;
}

void AnaDebugger::set_param(size_t index, int64_t val) {
    if (index < 8) params_[index] = val;
}

int64_t AnaDebugger::get_register(size_t index) const {
    if (index < 32) return registers_[index];
    return 0;
}

void AnaDebugger::set_breakpoint_label(const char* label) {
    if (breakpoint_count_ < 64 && label) {
        breakpoints_[breakpoint_count_] = {
            static_cast<uint32_t>(breakpoint_count_ + 1),
            label,
            0,
            true
        };
        breakpoint_count_++;
        print_out("[AnaDebugger] Breakpoint ");
        print_num(breakpoint_count_);
        print_out(" set at label :");
        print_out(label);
        print_out("\n");
    }
}

void AnaDebugger::set_breakpoint_line(uint32_t line) {
    if (breakpoint_count_ < 64) {
        breakpoints_[breakpoint_count_] = {
            static_cast<uint32_t>(breakpoint_count_ + 1),
            nullptr,
            line,
            true
        };
        breakpoint_count_++;
        print_out("[AnaDebugger] Breakpoint ");
        print_num(breakpoint_count_);
        print_out(" set at line ");
        print_num(line);
        print_out("\n");
    }
}

void AnaDebugger::list_breakpoints() const {
    print_out("\n--- Breakpoints ---\n");
    if (breakpoint_count_ == 0) {
        print_out("  No breakpoints set.\n");
        return;
    }
    for (size_t i = 0; i < breakpoint_count_; ++i) {
        print_out("  ["); print_num(breakpoints_[i].id); print_out("] ");
        if (breakpoints_[i].label) {
            print_out("Label: :"); print_out(breakpoints_[i].label);
        } else {
            print_out("Line: "); print_num(breakpoints_[i].line_number);
        }
        print_out(breakpoints_[i].enabled ? " (Enabled)\n" : " (Disabled)\n");
    }
}

void AnaDebugger::print_registers() const {
    print_out("\n--- Parameters & Registers ---\n");
    print_out("  Parameters:\n");
    for (size_t i = 0; i < 4; ++i) {
        print_out("    p"); print_num(i); print_out(" = ");
        print_num(params_[i]); print_out(" ("); print_hex(params_[i]); print_out(")\n");
    }
    print_out("  Virtual Registers:\n");
    for (size_t i = 0; i < 8; ++i) {
        print_out("    v"); print_num(i); print_out(" = ");
        print_num(registers_[i]); print_out(" ("); print_hex(registers_[i]); print_out(")\n");
    }
}

void AnaDebugger::print_memory(uint64_t addr, size_t count) const {
    print_out("\n--- Memory Examine ["); print_hex(addr); print_out("] ---\n");
    const uint64_t* ptr = reinterpret_cast<const uint64_t*>(addr);
    for (size_t i = 0; i < count; ++i) {
        print_hex(addr + i * 8); print_out(": ");
        print_hex(ptr[i]); print_out(" ("); print_num(ptr[i]); print_out(")\n");
    }
}

void AnaDebugger::print_current_instruction() const {
    if (!current_instruction_) {
        print_out("[Program Finished]\n");
        return;
    }
    print_out("=> [Line "); print_num(current_line_); print_out("] ");
    switch (current_instruction_->op) {
        case frontend::Opcode::MOVE_CONST: print_out("move-const"); break;
        case frontend::Opcode::MOVE: print_out("move"); break;
        case frontend::Opcode::ADD_I64: print_out("add-int/64"); break;
        case frontend::Opcode::SUB_I64: print_out("sub-int/64"); break;
        case frontend::Opcode::MUL_I64: print_out("mul-int/64"); break;
        case frontend::Opcode::DIV_I64: print_out("div-int/64"); break;
        case frontend::Opcode::AND_I64: print_out("and-int/64"); break;
        case frontend::Opcode::OR_I64:  print_out("or-int/64");  break;
        case frontend::Opcode::XOR_I64: print_out("xor-int/64"); break;
        case frontend::Opcode::IF_EQ: print_out("if-eq"); break;
        case frontend::Opcode::IF_NE: print_out("if-ne"); break;
        case frontend::Opcode::IF_LT: print_out("if-lt"); break;
        case frontend::Opcode::IF_GE: print_out("if-ge"); break;
        case frontend::Opcode::GOTO:  print_out("goto"); break;
        case frontend::Opcode::RETURN_VAL: print_out("return-val"); break;
        case frontend::Opcode::RETURN_VOID: print_out("return-void"); break;
        default: print_out("instruction"); break;
    }
    if (current_instruction_->target_label) {
        print_out(" :"); print_out(current_instruction_->target_label);
    }
    print_out("\n");
}

void AnaDebugger::execute_single_instruction(frontend::Instruction* insn) {
    if (!insn) return;

    auto eval_operand = [&](const frontend::Operand& op) -> int64_t {
        if (op.kind == frontend::OperandKind::CONST_INT) {
            return insn->src1.const_val;
        } else if (op.kind == frontend::OperandKind::REGISTER) {
            if (op.reg.type == frontend::RegisterType::PARAM) {
                return params_[op.reg.index & 7];
            } else {
                return registers_[op.reg.index & 31];
            }
        }
        return 0;
    };

    auto store_result = [&](const frontend::Register& reg, int64_t val) {
        if (reg.type == frontend::RegisterType::PARAM) {
            params_[reg.index & 7] = val;
        } else {
            registers_[reg.index & 31] = val;
        }
    };

    switch (insn->op) {
        case frontend::Opcode::MOVE_CONST: {
            store_result(insn->dest.reg, insn->src1.const_val);
            break;
        }
        case frontend::Opcode::MOVE: {
            int64_t val = eval_operand(insn->src1);
            store_result(insn->dest.reg, val);
            break;
        }
        case frontend::Opcode::ADD_I64:
        case frontend::Opcode::ADD_I32: {
            int64_t r1 = eval_operand(insn->src1);
            int64_t r2 = eval_operand(insn->src2);
            store_result(insn->dest.reg, r1 + r2);
            break;
        }
        case frontend::Opcode::SUB_I64:
        case frontend::Opcode::SUB_I32: {
            int64_t r1 = eval_operand(insn->src1);
            int64_t r2 = eval_operand(insn->src2);
            store_result(insn->dest.reg, r1 - r2);
            break;
        }
        case frontend::Opcode::MUL_I64:
        case frontend::Opcode::MUL_I32: {
            int64_t r1 = eval_operand(insn->src1);
            int64_t r2 = eval_operand(insn->src2);
            store_result(insn->dest.reg, r1 * r2);
            break;
        }
        case frontend::Opcode::DIV_I64:
        case frontend::Opcode::DIV_I32: {
            int64_t r1 = eval_operand(insn->src1);
            int64_t r2 = eval_operand(insn->src2);
            store_result(insn->dest.reg, r2 != 0 ? r1 / r2 : 0);
            break;
        }
        case frontend::Opcode::AND_I64: {
            int64_t r1 = eval_operand(insn->src1);
            int64_t r2 = eval_operand(insn->src2);
            store_result(insn->dest.reg, r1 & r2);
            break;
        }
        case frontend::Opcode::OR_I64: {
            int64_t r1 = eval_operand(insn->src1);
            int64_t r2 = eval_operand(insn->src2);
            store_result(insn->dest.reg, r1 | r2);
            break;
        }
        case frontend::Opcode::XOR_I64: {
            int64_t r1 = eval_operand(insn->src1);
            int64_t r2 = eval_operand(insn->src2);
            store_result(insn->dest.reg, r1 ^ r2);
            break;
        }
        case frontend::Opcode::RETURN_VAL: {
            int64_t ret_val = eval_operand(insn->src1);
            registers_[0] = ret_val;
            is_finished_ = true;
            print_out("[Return] Function returned: ");
            print_num(ret_val);
            print_out("\n");
            break;
        }
        case frontend::Opcode::RETURN_VOID: {
            is_finished_ = true;
            print_out("[Return] Function returned void\n");
            break;
        }
        default:
            break;
    }
}

bool AnaDebugger::check_breakpoint_hit() {
    if (!current_block_) return false;
    for (size_t i = 0; i < breakpoint_count_; ++i) {
        if (!breakpoints_[i].enabled) continue;
        if (breakpoints_[i].label && current_block_->label) {
            if (streq_impl(breakpoints_[i].label, current_block_->label)) {
                return true;
            }
        }
        if (breakpoints_[i].line_number == current_line_) {
            return true;
        }
    }
    return false;
}

bool AnaDebugger::step() {
    if (is_finished_ || !current_instruction_) {
        print_out("[AnaDebugger] Execution is finished.\n");
        return false;
    }

    execute_single_instruction(current_instruction_);
    current_line_++;

    if (current_instruction_->next) {
        current_instruction_ = current_instruction_->next;
    } else if (current_block_ && current_block_->next) {
        current_block_ = current_block_->next;
        current_instruction_ = current_block_->first_insn;
    } else {
        is_finished_ = true;
        current_instruction_ = nullptr;
    }
    return !is_finished_;
}

bool AnaDebugger::continue_execution() {
    while (!is_finished_ && current_instruction_) {
        if (check_breakpoint_hit()) {
            print_out("\n[AnaDebugger] Hit Breakpoint at Line ");
            print_num(current_line_);
            print_out("\n");
            print_current_instruction();
            return true;
        }
        step();
    }
    return false;
}

void AnaDebugger::run_interactive_repl() {
    print_out("\n=======================================================\n");
    print_out("  Anastasia Interactive Assembly Debugger (AnaDebugger)\n");
    print_out("=======================================================\n");
    print_out("Type 'help' for commands, 'step' to advance, 'regs' for state.\n\n");

    print_current_instruction();

    char line_buf[128];
    while (!is_finished_) {
        print_out("ana-debug> ");
        int64_t len = sys::raw_read(0, line_buf, sizeof(line_buf) - 1);
        if (len <= 0) break;
        line_buf[len] = '\0';

        // Strip trailing newline
        for (int i = 0; i < len; ++i) {
            if (line_buf[i] == '\r' || line_buf[i] == '\n') {
                line_buf[i] = '\0';
                break;
            }
        }

        if (streq_impl(line_buf, "s") || streq_impl(line_buf, "step") || streq_impl(line_buf, "next") || line_buf[0] == '\0') {
            step();
            print_current_instruction();
        } else if (streq_impl(line_buf, "c") || streq_impl(line_buf, "continue") || streq_impl(line_buf, "run")) {
            continue_execution();
        } else if (streq_impl(line_buf, "r") || streq_impl(line_buf, "regs") || streq_impl(line_buf, "registers")) {
            print_registers();
        } else if (streq_impl(line_buf, "b") || streq_impl(line_buf, "break")) {
            list_breakpoints();
        } else if (streq_impl(line_buf, "q") || streq_impl(line_buf, "quit") || streq_impl(line_buf, "exit")) {
            print_out("Exiting debugger.\n");
            break;
        } else if (streq_impl(line_buf, "help") || streq_impl(line_buf, "h")) {
            print_out("\nAvailable Commands:\n");
            print_out("  step (s), next (n)  : Execute next instruction\n");
            print_out("  continue (c), run   : Run until breakpoint\n");
            print_out("  regs (r)            : Display p0..p7 and v0..v31 register state\n");
            print_out("  break (b)           : List breakpoints\n");
            print_out("  help (h)            : Show this help menu\n");
            print_out("  quit (q)            : Exit debugger\n\n");
        } else {
            print_out("Unknown command. Type 'help' for command list.\n");
        }
    }
}

} // namespace debugger
} // namespace ana
