#ifndef ANA_PGO_PROFILER_H
#define ANA_PGO_PROFILER_H

#include "../frontend/ana_ast.h"

namespace ana {
namespace optimizer {

struct BlockProfileEntry {
    uint32_t block_id;
    uint64_t execution_count;
};

class PGOProfiler {
public:
    static PGOProfiler& instance();

    PGOProfiler();
    ~PGOProfiler();

    void record_block_execution(uint32_t block_id, uint64_t count);
    bool reorder_basic_blocks(frontend::Function* fn);

    uint32_t profiled_blocks() const { return profile_count_; }

private:
    BlockProfileEntry profiles_[128];
    uint32_t profile_count_;
};

} // namespace optimizer
} // namespace ana

#endif // ANA_PGO_PROFILER_H
