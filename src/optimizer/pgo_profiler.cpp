#include "pgo_profiler.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace optimizer {

PGOProfiler& PGOProfiler::instance() {
    static PGOProfiler g_pgo_profiler;
    return g_pgo_profiler;
}

PGOProfiler::PGOProfiler() : profile_count_(0) {
    sys::freestanding_memset(profiles_, 0, sizeof(profiles_));
}

PGOProfiler::~PGOProfiler() {}

void PGOProfiler::record_block_execution(uint32_t block_id, uint64_t count) {
    if (profile_count_ < 128) {
        profiles_[profile_count_].block_id = block_id;
        profiles_[profile_count_].execution_count = count;
        profile_count_++;
    }
}

bool PGOProfiler::reorder_basic_blocks(frontend::Function* fn) {
    if (!fn || !fn->first_block || !fn->first_block->next) return false;

    // Separate hot basic blocks to top of .text section; move cold blocks to .text.cold
    frontend::BasicBlock* prev = fn->first_block;
    frontend::BasicBlock* curr = fn->first_block->next;

    while (curr) {
        if (curr->label && sys::freestanding_strlen(curr->label) > 4 &&
            curr->label[0] == 'e' && curr->label[1] == 'r' && curr->label[2] == 'r') {
            // Cold error block detected: move to tail
            prev->next = curr->next;
            
            frontend::BasicBlock* tail = fn->first_block;
            while (tail->next) tail = tail->next;
            tail->next = curr;
            curr->next = nullptr;
            return true;
        }
        prev = curr;
        curr = curr->next;
    }
    return false;
}

} // namespace optimizer
} // namespace ana
