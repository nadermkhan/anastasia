#include "object_heap.h"

namespace ana {
namespace sys {

static ObjectHeap g_heap;

ObjectHeap& ObjectHeap::instance() {
    return g_heap;
}

ObjectHeap::ObjectHeap() : buffer_(nullptr), capacity_(0), offset_(0) {
    ensure_capacity(65536);
}

ObjectHeap::~ObjectHeap() {
    if (buffer_) {
        raw_munmap(buffer_, capacity_);
        buffer_ = nullptr;
    }
}

void ObjectHeap::ensure_capacity(size_t size) {
    if (offset_ + size > capacity_) {
        size_t new_cap = (capacity_ == 0) ? 65536 : capacity_ * 2 + size;
        new_cap = (new_cap + 4095) & ~4095UL;
        void* ptr = raw_mmap(nullptr, new_cap, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
        if (ptr && ptr != reinterpret_cast<void*>(-1)) {
            if (buffer_ && offset_ > 0) {
                freestanding_memcpy(ptr, buffer_, offset_);
                raw_munmap(buffer_, capacity_);
            }
            buffer_ = static_cast<uint8_t*>(ptr);
            capacity_ = new_cap;
        }
    }
}

void* ObjectHeap::allocate_object(uint32_t instance_size, void* vtable_ptr, uint32_t class_id) {
    uint32_t total_size = sizeof(ObjectHeader) + instance_size;
    total_size = (total_size + 7) & ~7U; // Align to 8 bytes

    ensure_capacity(total_size);
    if (!buffer_) return nullptr;

    ObjectHeader* obj = reinterpret_cast<ObjectHeader*>(buffer_ + offset_);
    offset_ += total_size;

    obj->vtable_ptr = vtable_ptr;
    obj->class_id = class_id;
    obj->instance_size = instance_size;

    // Zero out fields area
    freestanding_memset(reinterpret_cast<uint8_t*>(obj) + sizeof(ObjectHeader), 0, instance_size);

    return reinterpret_cast<void*>(obj);
}

void ObjectHeap::reset() {
    offset_ = 0;
}

extern "C" void* ana_alloc_object(uint32_t instance_size, void* vtable_ptr, uint32_t class_id) {
    return ObjectHeap::instance().allocate_object(instance_size, vtable_ptr, class_id);
}

} // namespace sys
} // namespace ana
