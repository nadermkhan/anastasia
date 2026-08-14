#ifndef SYS_COALESCER_H
#define SYS_COALESCER_H

#include "../frontend/ana_ast.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace optimizer {

class SyscallCoalescer {
public:
    static size_t coalesce_program_syscalls(frontend::Program* prog);
    static bool optimize_basic_block(frontend::BasicBlock* bb);
};

} // namespace optimizer
} // namespace ana

#endif // SYS_COALESCER_H
