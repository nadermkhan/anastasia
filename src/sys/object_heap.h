#ifndef OBJECT_HEAP_H
#define OBJECT_HEAP_H

#include "sys_raw.h"

namespace ana {
namespace sys {

struct ObjectHeader {
    void* vtable_ptr;
    uint32_t class_id;
    uint32_t instance_size;
};

// One mapping per chunk. The header lives at the front of its own mapping so
// the heap needs no secondary allocator to track itself.
struct HeapChunk {
    HeapChunk* next;
    size_t mapping_size; // total bytes to hand back to munmap
    size_t capacity;     // usable bytes after the padded header
    size_t offset;       // bump cursor within the usable area
};

// Chunked bump allocator.
//
// The previous implementation grew by mmap'ing a larger buffer and memcpy'ing
// the old contents into it. Every object pointer previously handed out (and
// every such pointer already baked into JIT'd code) became dangling the moment
// the heap grew. Chunks are never moved or copied here: growth only appends.
class ObjectHeap {
public:
    static ObjectHeap& instance();

    ObjectHeap();
    ~ObjectHeap();

    void* allocate_object(uint32_t instance_size, void* vtable_ptr, uint32_t class_id);
    void reset();

    size_t chunk_count() const;
    size_t bytes_allocated() const;

private:
    HeapChunk* new_chunk(size_t min_usable);

    HeapChunk* head_;
    HeapChunk* current_;
};

extern "C" void* ana_alloc_object(uint32_t instance_size, void* vtable_ptr, uint32_t class_id);

} // namespace sys
} // namespace ana

#endif // OBJECT_HEAP_H
