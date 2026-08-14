#ifndef ANA_TRAP_HANDLER_H
#define ANA_TRAP_HANDLER_H

#include "sys_raw.h"
#include <cstdint>
#include <cstddef>

namespace ana {
namespace sys {

class AnaTrapHandler {
public:
    static bool init();
    static void remove();
    static void set_debugger_fallback(bool enable);
    static bool is_debugger_fallback_enabled();

    static void print_crash_report(int sig, const char* sig_name, void* fault_addr, void* ucontext);
};

bool init_trap_handler();
void remove_trap_handler();

} // namespace sys
} // namespace ana

#endif // ANA_TRAP_HANDLER_H
