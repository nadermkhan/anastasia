#ifndef ANA_GC_COLLECTOR_H
#define ANA_GC_COLLECTOR_H

#include "sys_raw.h"

namespace ana {
namespace sys {

struct StackMapEntry {
    uint32_t code_offset;
    uint32_t root_mask; // Bitmask of physical registers holding live ptr refs
};

class GCCollector {
public:
    static GCCollector& instance();

    GCCollector();
    ~GCCollector();

    void register_stack_map(uint32_t code_offset, uint32_t root_mask);
    void record_remset_entry(void* slot_addr);
    size_t collect_nursery();

    uint64_t total_collections() const { return total_collections_; }
    uint64_t bytes_reclaimed() const { return bytes_reclaimed_; }

private:
    StackMapEntry stack_maps_[256];
    uint32_t stack_map_count_;

    void* remset_entries_[1024];
    uint32_t remset_count_;

    uint64_t total_collections_;
    uint64_t bytes_reclaimed_;
};

} // namespace sys
} // namespace ana

#endif // ANA_GC_COLLECTOR_H
