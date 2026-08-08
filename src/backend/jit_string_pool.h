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

// One mapping per chunk; the header lives at the front of its own mapping.
struct JitPoolChunk {
    JitPoolChunk* next;
    size_t mapping_size;
    size_t capacity;
    size_t cursor;
};

// Interning pool with stable string addresses.
//
// JIT'd code embeds the absolute address of an interned string directly in the
// instruction stream. The previous implementation grew by mmap'ing a bigger
// buffer and memcpy'ing into it, which silently invalidated every address
// already baked into generated code, and unmapped the old pages underneath it.
// Chunks are never moved here; growth only appends.
//
// The entry table is a different matter: nothing outside this class holds a
// pointer into it, so it can be reallocated freely. It used to be a fixed
// 256-entry array that silently stopped deduplicating once full.
class JitStringPool {
public:
    JitStringPool();
    ~JitStringPool();

    const char* get_or_intern(const char* bytes, size_t len, uint64_t hash);
    uint32_t count() const { return entry_count_; }
    uint32_t chunk_count() const;
    size_t bytes_interned() const;

private:
    JitPoolChunk* new_chunk(size_t min_usable);
    bool grow_entries();

    JitPoolChunk* head_;
    JitPoolChunk* current_;

    JitStringEntry* entries_;
    size_t entries_mapping_;
    uint32_t entry_capacity_;
    uint32_t entry_count_;
};

} // namespace backend
} // namespace ana

#endif // ANA_JIT_STRING_POOL_H
