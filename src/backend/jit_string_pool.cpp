#include "jit_string_pool.h"

namespace ana {
namespace backend {

JitStringPool::JitStringPool()
    : pool_buffer_(nullptr), pool_capacity_(0), pool_cursor_(0), entry_count_(0) {
    sys::freestanding_memset(entries_, 0, sizeof(entries_));
}

JitStringPool::~JitStringPool() {
    if (pool_buffer_ && pool_capacity_ > 0) {
        sys::raw_munmap(pool_buffer_, pool_capacity_);
    }
}

void JitStringPool::ensure_capacity(size_t additional) {
    if (pool_cursor_ + additional > pool_capacity_) {
        size_t new_cap = (pool_capacity_ == 0) ? 65536 : (pool_capacity_ * 2 + additional + 4095) & ~4095UL;
        void* new_buf = sys::raw_mmap(nullptr, new_cap, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
        if (new_buf && new_buf != reinterpret_cast<void*>(-1)) {
            if (pool_buffer_ && pool_cursor_ > 0) {
                sys::freestanding_memcpy(new_buf, pool_buffer_, pool_cursor_);
                sys::raw_munmap(pool_buffer_, pool_capacity_);
            }
            pool_buffer_ = static_cast<uint8_t*>(new_buf);
            pool_capacity_ = new_cap;
        }
    }
}

const char* JitStringPool::get_or_intern(const char* bytes, size_t len, uint64_t hash) {
    if (!bytes) return "";

    // 1. Query interning pool
    for (uint32_t i = 0; i < entry_count_; ++i) {
        if (entries_[i].hash == hash && entries_[i].len == len) {
            if (sys::freestanding_memcmp(entries_[i].ptr, bytes, len) == 0) {
                return entries_[i].ptr; // Deduplicated pointer match!
            }
        }
    }

    // 2. Branchlessly intern new string literal into bump pool
    ensure_capacity(len + 1);
    char* dst = reinterpret_cast<char*>(pool_buffer_ + pool_cursor_);
    sys::freestanding_memcpy(dst, bytes, len);
    dst[len] = '\0';
    pool_cursor_ += (len + 1);

    if (entry_count_ < 256) {
        entries_[entry_count_++] = { hash, len, dst };
    }

    return dst;
}

} // namespace backend
} // namespace ana
