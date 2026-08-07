#include "gdb_jit.h"
#include "elf_emitter.h"

namespace ana {
namespace backend {

extern "C" {
    struct jit_descriptor __jit_debug_descriptor = { 1, JIT_NOACTION, nullptr, nullptr };

    void __attribute__((noinline)) __jit_debug_register_code() {
        __asm__ __volatile__("" ::: "memory");
    }
}

jit_code_entry* register_jit_code(const void* code_ptr, size_t code_size, const char* fn_name) {
    if (!code_ptr || code_size == 0) return nullptr;

    jit_code_entry* entry = static_cast<jit_code_entry*>(malloc(sizeof(jit_code_entry)));
    if (!entry) return nullptr;

    sys::freestanding_memset(entry, 0, sizeof(jit_code_entry));

    // Construct lightweight ELF image wrapper in memory for GDB symbol lookup
    ElfEmitter emitter;
    const char* name = fn_name ? fn_name : "jit_fn";
    emitter.add_symbol(name, 0, code_size, true);
    emitter.append_text(code_ptr, code_size);

    size_t elf_size = 0;
    uint8_t* elf_data = emitter.finalize(&elf_size);

    entry->symfile_addr = reinterpret_cast<const char*>(elf_data);
    entry->symfile_size = elf_size;

    // Link entry into __jit_debug_descriptor doubly linked list
    entry->next_entry = __jit_debug_descriptor.first_entry;
    entry->prev_entry = nullptr;
    if (__jit_debug_descriptor.first_entry) {
        __jit_debug_descriptor.first_entry->prev_entry = entry;
    }
    __jit_debug_descriptor.first_entry = entry;
    __jit_debug_descriptor.relevant_entry = entry;
    __jit_debug_descriptor.action_flag = JIT_REGISTER_FN;

    // Trigger GDB breakpoint hook
    __jit_debug_register_code();

    return entry;
}

void unregister_jit_code(jit_code_entry* entry) {
    if (!entry) return;

    if (entry->prev_entry) {
        entry->prev_entry->next_entry = entry->next_entry;
    } else {
        __jit_debug_descriptor.first_entry = entry->next_entry;
    }

    if (entry->next_entry) {
        entry->next_entry->prev_entry = entry->prev_entry;
    }

    __jit_debug_descriptor.relevant_entry = entry;
    __jit_debug_descriptor.action_flag = JIT_UNREGISTER_FN;

    __jit_debug_register_code();

    if (entry->symfile_addr) {
        free(const_cast<char*>(entry->symfile_addr));
    }
    free(entry);
}

} // namespace backend
} // namespace ana
