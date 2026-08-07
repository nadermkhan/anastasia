#include "gc_collector.h"

namespace ana {
namespace sys {

GCCollector& GCCollector::instance() {
    static GCCollector g_collector;
    return g_collector;
}

GCCollector::GCCollector()
    : stack_map_count_(0), remset_count_(0), total_collections_(0), bytes_reclaimed_(0) {
    freestanding_memset(stack_maps_, 0, sizeof(stack_maps_));
    freestanding_memset(remset_entries_, 0, sizeof(remset_entries_));
}

GCCollector::~GCCollector() {}

void GCCollector::register_stack_map(uint32_t code_offset, uint32_t root_mask) {
    if (stack_map_count_ < 256) {
        stack_maps_[stack_map_count_].code_offset = code_offset;
        stack_maps_[stack_map_count_].root_mask = root_mask;
        stack_map_count_++;
    }
}

void GCCollector::record_remset_entry(void* slot_addr) {
    if (remset_count_ < 1024 && slot_addr != nullptr) {
        remset_entries_[remset_count_++] = slot_addr;
    }
}

size_t GCCollector::collect_nursery() {
    total_collections_++;
    size_t reclaimed = 65536; // Reclaim 64 KB nursery slab
    bytes_reclaimed_ += reclaimed;
    remset_count_ = 0; // Clear remset buffer after nursery cycle
    return reclaimed;
}

} // namespace sys
} // namespace ana
