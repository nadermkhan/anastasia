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

class ObjectHeap {
public:
    static ObjectHeap& instance();

    ObjectHeap();
    ~ObjectHeap();

    void* allocate_object(uint32_t instance_size, void* vtable_ptr, uint32_t class_id);
    void reset();

private:
    void ensure_capacity(size_t size);

    uint8_t* buffer_;
    size_t capacity_;
    size_t offset_;
};

extern "C" void* ana_alloc_object(uint32_t instance_size, void* vtable_ptr, uint32_t class_id);

} // namespace sys
} // namespace ana

#endif // OBJECT_HEAP_H
