#include "object_heap.h"

namespace ana {
namespace sys {

static ObjectHeap g_heap;

// Padded to 64 bytes so the first object in every chunk starts on a cache
// line, matching the 64-byte object alignment used below.
static const size_t kChunkHeaderBytes = 64;
static const size_t kDefaultChunkBytes = 65536;
static const size_t kPageBytes = 4096;
static const uint64_t kMaxSingleObject = 0x40000000ULL; // 1 GiB

ObjectHeap& ObjectHeap::instance() {
    return g_heap;
}

// Note: this build has no .init_array walker, so global constructors never
// run. Everything here is therefore lazy and safe against a zeroed BSS state.
ObjectHeap::ObjectHeap() : head_(nullptr), current_(nullptr) {}

ObjectHeap::~ObjectHeap() {
    HeapChunk* c = head_;
    while (c) {
        HeapChunk* next = c->next;
        size_t bytes = c->mapping_size;
        raw_munmap(reinterpret_cast<void*>(c), bytes);
        c = next;
    }
    head_ = nullptr;
    current_ = nullptr;
}

HeapChunk* ObjectHeap::new_chunk(size_t min_usable) {
    size_t needed = min_usable + kChunkHeaderBytes;
    if (needed < min_usable) return nullptr; // overflow

    size_t mapping = (needed > kDefaultChunkBytes) ? needed : kDefaultChunkBytes;
    size_t rounded = (mapping + (kPageBytes - 1)) & ~(kPageBytes - 1);
    if (rounded < mapping) return nullptr; // overflow on rounding

    void* ptr = raw_mmap(nullptr, rounded, ANA_PROT_READ | ANA_PROT_WRITE,
                         ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (!ptr || ptr == reinterpret_cast<void*>(-1)) return nullptr;

    HeapChunk* c = static_cast<HeapChunk*>(ptr);
    c->next = nullptr;
    c->mapping_size = rounded;
    c->capacity = rounded - kChunkHeaderBytes;
    c->offset = 0;

    if (current_) {
        current_->next = c;
    } else {
        head_ = c;
    }
    current_ = c;
    return c;
}

void* ObjectHeap::allocate_object(uint32_t instance_size, void* vtable_ptr, uint32_t class_id) {
    // This total was computed in uint32_t, so an instance_size near UINT32_MAX
    // wrapped and produced a tiny allocation for a huge object, which the
    // memset below then happily overran.
    uint64_t total = static_cast<uint64_t>(sizeof(ObjectHeader)) +
                     static_cast<uint64_t>(instance_size);
    total = (total + 63ULL) & ~63ULL; // align to a 64-byte cache line
    if (total > kMaxSingleObject) return nullptr;

    size_t need = static_cast<size_t>(total);

    HeapChunk* c = current_;
    if (!c || need > (c->capacity - c->offset)) {
        c = new_chunk(need);
        if (!c) return nullptr;
    }

    uint8_t* base = reinterpret_cast<uint8_t*>(c) + kChunkHeaderBytes;
    ObjectHeader* obj = reinterpret_cast<ObjectHeader*>(base + c->offset);
    c->offset += need;

    obj->vtable_ptr = vtable_ptr;
    obj->class_id = class_id;
    obj->instance_size = instance_size;

    // Zero the field area only; the header above is fully initialised.
    freestanding_memset(reinterpret_cast<uint8_t*>(obj) + sizeof(ObjectHeader), 0, instance_size);

    return reinterpret_cast<void*>(obj);
}

void ObjectHeap::reset() {
    if (!head_) return;

    // reset() already invalidates every object, so releasing the overflow
    // chunks here is safe and keeps steady-state memory bounded.
    HeapChunk* c = head_->next;
    while (c) {
        HeapChunk* next = c->next;
        raw_munmap(reinterpret_cast<void*>(c), c->mapping_size);
        c = next;
    }

    head_->next = nullptr;
    head_->offset = 0;
    current_ = head_;
}

size_t ObjectHeap::chunk_count() const {
    size_t n = 0;
    for (const HeapChunk* c = head_; c; c = c->next) ++n;
    return n;
}

size_t ObjectHeap::bytes_allocated() const {
    size_t n = 0;
    for (const HeapChunk* c = head_; c; c = c->next) n += c->offset;
    return n;
}

extern "C" void* ana_alloc_object(uint32_t instance_size, void* vtable_ptr, uint32_t class_id) {
    return ObjectHeap::instance().allocate_object(instance_size, vtable_ptr, class_id);
}

} // namespace sys
} // namespace ana
