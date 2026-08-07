#include "exception_unwinder.h"

namespace ana {
namespace backend {

ExceptionUnwinder& ExceptionUnwinder::instance() {
    static ExceptionUnwinder g_unwinder;
    return g_unwinder;
}

ExceptionUnwinder::ExceptionUnwinder() : table_count_(0) {
    sys::freestanding_memset(tables_, 0, sizeof(tables_));
}

ExceptionUnwinder::~ExceptionUnwinder() {}

void ExceptionUnwinder::register_exception_table(uint8_t* start, uint8_t* end, uint8_t* landing_pad, uint32_t type_id) {
    if (table_count_ < 128) {
        tables_[table_count_].code_start = start;
        tables_[table_count_].code_end = end;
        tables_[table_count_].catch_landing_pad = landing_pad;
        tables_[table_count_].exception_type_id = type_id;
        table_count_++;
    }
}

void ExceptionUnwinder::throw_exception(void* exception_obj, uint32_t type_id) {
#if defined(__x86_64__)
    // Walk %rbp frame-pointer chain
    void** current_rbp;
    __asm__ __volatile__("mov %%rbp, %0" : "=r"(current_rbp));

    while (current_rbp != nullptr && *current_rbp != nullptr) {
        void* return_address = current_rbp[1];
        uint8_t* pc = static_cast<uint8_t*>(return_address);

        // Search exception tables for matching frame range and type
        for (uint32_t i = 0; i < table_count_; ++i) {
            if (pc >= tables_[i].code_start && pc <= tables_[i].code_end &&
                (tables_[i].exception_type_id == 0 || tables_[i].exception_type_id == type_id)) {
                
                uint8_t* landing_pad = tables_[i].catch_landing_pad;
                void* target_rsp = current_rbp;

                // Non-local jump execution to catch block: set rsp = target_rsp, rdi = exception_obj, jmp landing_pad
                __asm__ __volatile__(
                    "mov %0, %%rsp\n\t"
                    "mov %1, %%rdi\n\t"
                    "jmp *%2\n\t"
                    :
                    : "r"(target_rsp), "r"(exception_obj), "r"(landing_pad)
                    : "rdi", "memory"
                );
            }
        }

        current_rbp = static_cast<void**>(*current_rbp);
    }
#endif

    // Uncaught Exception Panic Trace
    sys::raw_write(2, "\n=======================================================\n", 57);
    sys::raw_write(2, "  UNCAUGHT EXCEPTION PANIC: Frame-Pointer Unwind End\n", 53);
    sys::raw_write(2, "=======================================================\n\n", 57);
    sys::raw_exit(1);
}

extern "C" void sys_throw_exception(void* exception_obj, uint32_t type_id) {
    ExceptionUnwinder::instance().throw_exception(exception_obj, type_id);
    __builtin_unreachable();
}

} // namespace backend
} // namespace ana
