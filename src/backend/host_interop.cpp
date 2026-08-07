#include "host_interop.h"
#include "vmem_provider.h"

namespace ana {
namespace backend {

HostInterop& HostInterop::instance() {
    static HostInterop g_host_interop;
    return g_host_interop;
}

HostInterop::HostInterop() : func_count_(0) {
    sys::freestanding_memset(funcs_, 0, sizeof(funcs_));
}

HostInterop::~HostInterop() {}

void* HostInterop::register_host_function(const char* name, void* fn_ptr, bool requires_unboxing) {
    if (!name || !fn_ptr || func_count_ >= 128) return nullptr;

    // Allocate 64 bytes of W^X executable memory for JIT trampoline stub
    uint8_t* stub = static_cast<uint8_t*>(sys::raw_mmap(nullptr, 4096, ANA_PROT_READ | ANA_PROT_WRITE | ANA_PROT_EXEC, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0));
    if (!stub || stub == (uint8_t*)-1) return nullptr;

    size_t cursor = 0;
    if (requires_unboxing) {
        // Emit 2-instruction unbox sequence: sar rdi, 3; movq xmm0, rdi
        stub[cursor++] = 0x48; stub[cursor++] = 0xc1; stub[cursor++] = 0xff; stub[cursor++] = 0x03; // sar rdi, 3
        stub[cursor++] = 0x66; stub[cursor++] = 0x48; stub[cursor++] = 0x0f; stub[cursor++] = 0x6e; stub[cursor++] = 0xc7; // movq xmm0, rdi
    }

    // Emit 64-bit absolute indirect jump: movabs rax, fn_ptr; jmp rax
    stub[cursor++] = 0x48; stub[cursor++] = 0xb8; // movabs rax, imm64
    *reinterpret_cast<uint64_t*>(&stub[cursor]) = reinterpret_cast<uint64_t>(fn_ptr);
    cursor += 8;
    stub[cursor++] = 0xff; stub[cursor++] = 0xe0; // jmp rax

    sys::clear_icache(stub, cursor);

    funcs_[func_count_].name = name;
    funcs_[func_count_].host_fn_ptr = fn_ptr;
    funcs_[func_count_].JIT_trampoline_ptr = stub;
    funcs_[func_count_].requires_unboxing = requires_unboxing;
    func_count_++;

    return stub;
}

void* HostInterop::get_trampoline(const char* name) {
    if (!name) return nullptr;
    for (uint32_t i = 0; i < func_count_; ++i) {
        if (funcs_[i].name && sys::freestanding_memcmp(funcs_[i].name, name, sys::freestanding_strlen(name)) == 0) {
            return funcs_[i].JIT_trampoline_ptr;
        }
    }
    return nullptr;
}

extern "C" void* ana_register_host_func(const char* name, void* fn_ptr) {
    return HostInterop::instance().register_host_function(name, fn_ptr, false);
}

} // namespace backend
} // namespace ana
