#ifndef ANA_ARENA_ALLOCATOR_H
#define ANA_ARENA_ALLOCATOR_H

#include "../sys/sys_raw.h"

namespace ana {
namespace frontend {

class ArenaAllocator {
private:
    struct Chunk {
        char* data;
        size_t capacity;
        size_t used;
        Chunk* next;
    };

    Chunk* head_;
    size_t default_chunk_size_;

    Chunk* allocate_chunk(size_t size) {
        size_t alloc_size = size > default_chunk_size_ ? size : default_chunk_size_;
        void* mem = ana::sys::raw_mmap(
            nullptr, alloc_size,
            ANA_PROT_READ | ANA_PROT_WRITE,
            ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS,
            -1, 0
        );

        if (mem == (void*)-1 || mem == nullptr) {
            return nullptr;
        }

        Chunk* chunk = reinterpret_cast<Chunk*>(mem);
        chunk->data = reinterpret_cast<char*>(chunk) + sizeof(Chunk);
        chunk->capacity = alloc_size - sizeof(Chunk);
        chunk->used = 0;
        chunk->next = nullptr;
        return chunk;
    }

public:
    explicit ArenaAllocator(size_t default_chunk_size = 65536)
        : head_(nullptr), default_chunk_size_(default_chunk_size) {}

    ~ArenaAllocator() {
        reset();
    }

    void* alloc(size_t bytes, size_t alignment = 8) {
        if (bytes == 0) return nullptr;

        if (!head_) {
            head_ = allocate_chunk(bytes + alignment);
            if (!head_) return nullptr;
        }

        Chunk* curr = head_;
        uintptr_t current_ptr = reinterpret_cast<uintptr_t>(curr->data + curr->used);
        uintptr_t aligned_ptr = (current_ptr + (alignment - 1)) & ~(alignment - 1);
        size_t padding = aligned_ptr - current_ptr;

        if (curr->used + padding + bytes > curr->capacity) {
            Chunk* new_chunk = allocate_chunk(bytes + alignment);
            if (!new_chunk) return nullptr;
            new_chunk->next = head_;
            head_ = new_chunk;
            curr = head_;

            current_ptr = reinterpret_cast<uintptr_t>(curr->data);
            aligned_ptr = (current_ptr + (alignment - 1)) & ~(alignment - 1);
            padding = aligned_ptr - current_ptr;
        }

        curr->used += padding + bytes;
        return reinterpret_cast<void*>(aligned_ptr);
    }

    template<typename T, typename... Args>
    T* create(Args&&... args) {
        void* mem = alloc(sizeof(T), alignof(T));
        if (!mem) return nullptr;
        return new (mem) T(static_cast<Args&&>(args)...);
    }

    void reset() {
        Chunk* curr = head_;
        while (curr) {
            Chunk* next = curr->next;
            size_t total_size = curr->capacity + sizeof(Chunk);
            ana::sys::raw_munmap(curr, total_size);
            curr = next;
        }
        head_ = nullptr;
    }
};

} // namespace frontend
} // namespace ana

// Placement new operator override for freestanding compilation
inline void* operator new(size_t, void* ptr) noexcept {
    return ptr;
}

#endif // ANA_ARENA_ALLOCATOR_H
