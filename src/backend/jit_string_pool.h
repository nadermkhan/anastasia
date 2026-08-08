#ifndef ANA_JIT_STRING_POOL_H
#define ANA_JIT_STRING_POOL_H

#include <cstdint>
#include <cstddef>
#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

struct JitStringEntry {
    uint64_t hash;
    size_t len;
    const char* ptr;
};

class JitStringPool {
public:
    JitStringPool();
    ~JitStringPool();

    const char* get_or_intern(const char* bytes, size_t len, uint64_t hash);
    uint32_t count() const { return entry_count_; }

private:
    void ensure_capacity(size_t size);

    uint8_t* pool_buffer_;
    size_t pool_capacity_;
    size_t pool_cursor_;

    JitStringEntry entries_[256];
    uint32_t entry_count_;
};

} // namespace backend
} // namespace ana

#endif // ANA_JIT_STRING_POOL_H
