#include "jit_string_pool.h"

namespace ana {
namespace backend {

static const size_t kPoolHeaderBytes = 64;
static const size_t kPoolDefaultChunk = 65536;
static const size_t kPoolPage = 4096;
static const uint32_t kInitialEntries = 256;

JitStringPool::JitStringPool()
    : head_(nullptr), current_(nullptr),
      entries_(nullptr), entries_mapping_(0),
      entry_capacity_(0), entry_count_(0) {}

JitStringPool::~JitStringPool() {
    JitPoolChunk* c = head_;
    while (c) {
        JitPoolChunk* next = c->next;
        size_t bytes = c->mapping_size;
        sys::raw_munmap(reinterpret_cast<void*>(c), bytes);
        c = next;
    }
    head_ = nullptr;
    current_ = nullptr;

    if (entries_ && entries_mapping_) {
        sys::raw_munmap(reinterpret_cast<void*>(entries_), entries_mapping_);
    }
    entries_ = nullptr;
    entries_mapping_ = 0;
    entry_capacity_ = 0;
    entry_count_ = 0;
}

JitPoolChunk* JitStringPool::new_chunk(size_t min_usable) {
    size_t needed = min_usable + kPoolHeaderBytes;
    if (needed < min_usable) return nullptr; // overflow

    size_t mapping = (needed > kPoolDefaultChunk) ? needed : kPoolDefaultChunk;
    size_t rounded = (mapping + (kPoolPage - 1)) & ~(kPoolPage - 1);
    if (rounded < mapping) return nullptr;

    void* ptr = sys::raw_mmap(nullptr, rounded, ANA_PROT_READ | ANA_PROT_WRITE,
                              ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (!ptr || ptr == reinterpret_cast<void*>(-1)) return nullptr;

    JitPoolChunk* c = static_cast<JitPoolChunk*>(ptr);
    c->next = nullptr;
    c->mapping_size = rounded;
    c->capacity = rounded - kPoolHeaderBytes;
    c->cursor = 0;

    if (current_) {
        current_->next = c;
    } else {
        head_ = c;
    }
    current_ = c;
    return c;
}

bool JitStringPool::grow_entries() {
    uint32_t new_cap = entry_capacity_ ? (entry_capacity_ * 2) : kInitialEntries;
    if (new_cap <= entry_capacity_) return false; // overflow

    size_t bytes = static_cast<size_t>(new_cap) * sizeof(JitStringEntry);
    size_t rounded = (bytes + (kPoolPage - 1)) & ~(kPoolPage - 1);
    if (rounded < bytes) return false;

    void* ptr = sys::raw_mmap(nullptr, rounded, ANA_PROT_READ | ANA_PROT_WRITE,
                              ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (!ptr || ptr == reinterpret_cast<void*>(-1)) return false;

    JitStringEntry* fresh = static_cast<JitStringEntry*>(ptr);
    if (entries_ && entry_count_) {
        sys::freestanding_memcpy(fresh, entries_,
                                 static_cast<size_t>(entry_count_) * sizeof(JitStringEntry));
    }
    if (entries_ && entries_mapping_) {
        sys::raw_munmap(reinterpret_cast<void*>(entries_), entries_mapping_);
    }

    entries_ = fresh;
    entries_mapping_ = rounded;
    entry_capacity_ = new_cap;
    return true;
}

const char* JitStringPool::get_or_intern(const char* bytes, size_t len, uint64_t hash) {
    if (!bytes) return "";

    // 1. Query the interning table.
    for (uint32_t i = 0; i < entry_count_; ++i) {
        if (entries_[i].hash == hash && entries_[i].len == len) {
            if (sys::freestanding_memcmp(entries_[i].ptr, bytes, len) == 0) {
                return entries_[i].ptr; // deduplicated pointer match
            }
        }
    }

    // 2. Bump-allocate into a chunk whose address will never change.
    size_t need = len + 1;
    if (need < len) return "";

    JitPoolChunk* c = current_;
    if (!c || need > (c->capacity - c->cursor)) {
        c = new_chunk(need);
        if (!c) return ""; // out of memory: never hand back a bad pointer
    }

    char* dst = reinterpret_cast<char*>(
        reinterpret_cast<uint8_t*>(c) + kPoolHeaderBytes + c->cursor);
    sys::freestanding_memcpy(dst, bytes, len);
    dst[len] = '\0';
    c->cursor += need;

    // 3. Record it for future deduplication.
    if (entry_count_ == entry_capacity_) {
        if (!grow_entries()) {
            // Interning still succeeded; only deduplication is degraded.
            return dst;
        }
    }
    entries_[entry_count_].hash = hash;
    entries_[entry_count_].len = len;
    entries_[entry_count_].ptr = dst;
    ++entry_count_;

    return dst;
}

uint32_t JitStringPool::chunk_count() const {
    uint32_t n = 0;
    for (const JitPoolChunk* c = head_; c; c = c->next) ++n;
    return n;
}

size_t JitStringPool::bytes_interned() const {
    size_t n = 0;
    for (const JitPoolChunk* c = head_; c; c = c->next) n += c->cursor;
    return n;
}

} // namespace backend
} // namespace ana
