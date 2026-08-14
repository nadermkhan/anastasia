#include "ana_trap_handler.h"
#include <cstdlib>

#ifndef _WIN32
struct siginfo_t_dummy;
#else
#include <windows.h>
#endif

namespace ana {
namespace sys {

static bool g_debugger_fallback = true;
static bool g_trap_handler_installed = false;

static void print_str(const char* str) {
    if (str) raw_write(2, str, freestanding_strlen(str));
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
    print_str(&buf[pos + 1]);
}

static void print_hex(uint64_t val) {
    print_str("0x");
    char hex_digits[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; --i) {
        uint8_t nibble = (val >> (i * 4)) & 0xF;
        char c[2] = { hex_digits[nibble], '\0' };
        print_str(c);
    }
}

void AnaTrapHandler::print_crash_report(int sig, const char* sig_name, void* fault_addr, void* uctx) {
    (void)uctx;
    print_str("\n=======================================================\n");
    print_str("  [ANASTASIA ENGINE RUNTIME TRAP / CRASH DETECTED]\n");
    print_str("=======================================================\n");
    print_str("  Fault Signal : "); print_str(sig_name ? sig_name : "UNKNOWN SIGNAL");
    print_str(" (Signal "); print_num(sig); print_str(")\n");
    print_str("  Fault Address: "); print_hex(reinterpret_cast<uint64_t>(fault_addr));
    if (fault_addr == nullptr) {
        print_str(" (NULL Pointer Dereference)\n");
    } else {
        print_str("\n");
    }

#if !defined(_WIN32) && defined(__x86_64__)
    struct kernel_mcontext_t {
        uint64_t gregs[23];
    };
    struct kernel_ucontext_t {
        unsigned long uc_flags;
        kernel_ucontext_t* uc_link;
        uint64_t uc_stack[3];
        kernel_mcontext_t uc_mcontext;
    };
    if (uctx) {
        kernel_ucontext_t* uc = reinterpret_cast<kernel_ucontext_t*>(uctx);
        // gregs indices: R8=0, R9=1, R10=2, R11=3, R12=4, R13=5, R14=6, R15=7, RDI=8, RSI=9, RBP=10, RBX=11, RDX=12, RAX=13, RCX=14, RSP=15, RIP=16
        print_str("\n  CPU Architecture: x86_64 Register State\n");
        print_str("    RIP = "); print_hex(uc->uc_mcontext.gregs[16]); print_str("\n");
        print_str("    RSP = "); print_hex(uc->uc_mcontext.gregs[15]); print_str("\n");
        print_str("    RBP = "); print_hex(uc->uc_mcontext.gregs[10]); print_str("\n");
        print_str("    RAX = "); print_hex(uc->uc_mcontext.gregs[13]); print_str("  ");
        print_str("    RBX = "); print_hex(uc->uc_mcontext.gregs[11]); print_str("\n");
        print_str("    RCX = "); print_hex(uc->uc_mcontext.gregs[14]); print_str("  ");
        print_str("    RDX = "); print_hex(uc->uc_mcontext.gregs[12]); print_str("\n");
        print_str("    RDI = "); print_hex(uc->uc_mcontext.gregs[8]);  print_str("  ");
        print_str("    RSI = "); print_hex(uc->uc_mcontext.gregs[9]);  print_str("\n");
    }
#endif

    print_str("=======================================================\n");
    print_str("  Execution Halted Safely to Prevent Data Corruption\n");
    print_str("=======================================================\n\n");
}

#if defined(__linux__) && defined(__x86_64__)
struct kernel_sigaction {
    void (*sa_sigaction)(int, void*, void*);
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    unsigned long sa_mask;
};

static inline int raw_rt_sigaction(int signum, const kernel_sigaction* act, kernel_sigaction* oact) {
    int64_t ret;
    register long r10 __asm__("r10") = 8; // sizeof(kernel_sigset_t)
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(13), "D"(signum), "S"(act), "d"(oact), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return static_cast<int>(ret);
}
#endif

#ifndef _WIN32
struct kernel_siginfo_t {
    int si_signo;
    int si_errno;
    int si_code;
    int pad;
    void* si_addr;
};

static void posix_signal_handler(int sig, kernel_siginfo_t* info, void* ucontext) {
    const char* sig_name = "SIGSEGV (Segmentation Fault)";
    if (sig == 8) sig_name = "SIGFPE (Floating-Point Exception / Divide-by-Zero)";
    else if (sig == 4) sig_name = "SIGILL (Illegal Instruction)";
    else if (sig == 7) sig_name = "SIGBUS (Bus Error / Unaligned Access)";

    AnaTrapHandler::print_crash_report(sig, sig_name, info ? info->si_addr : nullptr, ucontext);
    raw_exit(128 + sig);
}
#endif

bool AnaTrapHandler::init() {
    if (g_trap_handler_installed) return true;

#if defined(__linux__) && defined(__x86_64__)
    kernel_sigaction ksa;
    freestanding_memset(&ksa, 0, sizeof(ksa));
    ksa.sa_sigaction = reinterpret_cast<void (*)(int, void*, void*)>(posix_signal_handler);
    ksa.sa_flags = 0x00000004 | 0x40000000; // SA_SIGINFO | SA_NODEFER

    raw_rt_sigaction(11 /* SIGSEGV */, &ksa, nullptr);
    raw_rt_sigaction(8  /* SIGFPE */,  &ksa, nullptr);
    raw_rt_sigaction(4  /* SIGILL */,  &ksa, nullptr);
    raw_rt_sigaction(7  /* SIGBUS */,  &ksa, nullptr);
#endif

    g_trap_handler_installed = true;
    return true;
}

void AnaTrapHandler::remove() {
    if (!g_trap_handler_installed) return;
#if defined(__linux__) && defined(__x86_64__)
    kernel_sigaction ksa;
    freestanding_memset(&ksa, 0, sizeof(ksa));
    ksa.sa_sigaction = nullptr;
    raw_rt_sigaction(11, &ksa, nullptr);
    raw_rt_sigaction(8,  &ksa, nullptr);
    raw_rt_sigaction(4,  &ksa, nullptr);
    raw_rt_sigaction(7,  &ksa, nullptr);
#endif
    g_trap_handler_installed = false;
}

void AnaTrapHandler::set_debugger_fallback(bool enable) {
    g_debugger_fallback = enable;
}

bool AnaTrapHandler::is_debugger_fallback_enabled() {
    return g_debugger_fallback;
}

bool init_trap_handler() {
    return AnaTrapHandler::init();
}

void remove_trap_handler() {
    AnaTrapHandler::remove();
}

} // namespace sys
} // namespace ana
