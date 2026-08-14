#ifndef ANA_DEBUGGER_H
#define ANA_DEBUGGER_H

#include "../frontend/ana_ast.h"
#include "../frontend/ana_parser.h"
#include "../backend/ana_lowerer.h"
#include "../sys/sys_raw.h"
#include <cstdint>
#include <cstddef>

namespace ana {
namespace debugger {

struct Breakpoint {
    uint32_t id;
    const char* label;
    uint32_t line_number;
    bool enabled;
};

class AnaDebugger {
public:
    AnaDebugger();
    ~AnaDebugger();

    bool load_program_from_file(const char* filepath);
    bool load_program_from_source(const char* source);

    void set_breakpoint_label(const char* label);
    void set_breakpoint_line(uint32_t line);
    void remove_breakpoint(uint32_t id);
    void list_breakpoints() const;

    void print_registers() const;
    void print_memory(uint64_t addr, size_t count) const;
    void print_current_instruction() const;
    void print_disassembly() const;

    bool step();
    bool continue_execution();
    void run_interactive_repl();

    void set_param(size_t index, int64_t val);
    int64_t get_register(size_t index) const;

private:
    frontend::ArenaAllocator arena_;
    frontend::Program* program_;
    frontend::Function* current_function_;
    frontend::BasicBlock* current_block_;
    frontend::Instruction* current_instruction_;

    int64_t params_[8];
    int64_t registers_[32];

    Breakpoint breakpoints_[64];
    size_t breakpoint_count_;
    uint32_t current_line_;
    bool is_finished_;

    bool check_breakpoint_hit();
    void execute_single_instruction(frontend::Instruction* insn);
};

} // namespace debugger
} // namespace ana

#endif // ANA_DEBUGGER_H
