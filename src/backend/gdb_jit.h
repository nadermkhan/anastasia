#ifndef ANA_GDB_JIT_H
#define ANA_GDB_JIT_H

#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

enum jit_actions_t {
    JIT_NOACTION = 0,
    JIT_REGISTER_FN,
    JIT_UNREGISTER_FN
};

struct jit_code_entry {
    struct jit_code_entry* next_entry;
    struct jit_code_entry* prev_entry;
    const char* symfile_addr;
    uint64_t symfile_size;
};

struct jit_descriptor {
    uint32_t version;
    uint32_t action_flag;
    struct jit_code_entry* relevant_entry;
    struct jit_code_entry* first_entry;
};

// Global GDB JIT interface symbol queried by GDB/LLDB
extern "C" {
    extern struct jit_descriptor __jit_debug_descriptor;
    void __attribute__((noinline)) __jit_debug_register_code();
}

// Helper to construct an in-memory ELF symbol header and register with GDB JIT runtime
jit_code_entry* register_jit_code(const void* code_ptr, size_t code_size, const char* fn_name);

// Helper to unregister JIT allocation upon deallocation
void unregister_jit_code(jit_code_entry* entry);

} // namespace backend
} // namespace ana

#endif // ANA_GDB_JIT_H
