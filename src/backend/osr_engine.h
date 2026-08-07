#ifndef ANA_OSR_ENGINE_H
#define ANA_OSR_ENGINE_H

#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

struct CPURegisterState {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    uint64_t r8,  r9,  r10, r11, r12, r13, r14, r15;
};

class OSREngine {
public:
    static OSREngine& instance();

    OSREngine();
    ~OSREngine();

    void record_loop_iteration(void* fn_key, uint32_t loop_id);
    void* trigger_osr(void* fn_key, uint32_t loop_id, CPURegisterState* regs);

    uint32_t total_osr_transitions() const { return total_osr_transitions_; }

private:
    uint32_t loop_counters_[64];
    uint32_t total_osr_transitions_;
};

extern "C" void* handle_osr_trigger(void* fn_key, uint32_t loop_id, CPURegisterState* regs);

} // namespace backend
} // namespace ana

#endif // ANA_OSR_ENGINE_H
