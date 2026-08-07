#ifndef ANA_IO_RING_H
#define ANA_IO_RING_H

#include "sys_raw.h"

namespace ana {
namespace sys {

struct io_uring_sqe_raw {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t ioprio;
    int32_t  fd;
    uint64_t off;
    uint64_t addr;
    uint32_t len;
    uint32_t op_flags;
    uint64_t user_data;
    uint16_t buf_index;
    uint16_t personality;
    int32_t  file_index;
    uint64_t pad2[2];
};

struct io_uring_cqe_raw {
    uint64_t user_data;
    int32_t  res;
    uint32_t flags;
};

class IoRing {
public:
    static IoRing& instance();

    IoRing();
    ~IoRing();

    bool init(uint32_t entries = 32);
    int submit_sqe(uint8_t opcode, int fd, void* addr, uint32_t len, uint64_t user_data);
    int poll_cqe(uint64_t* user_data_out, int32_t* res_out);

    uint64_t total_submissions() const { return total_submissions_; }
    uint64_t total_completions() const { return total_completions_; }

private:
    int ring_fd_;
    io_uring_sqe_raw* sqes_;
    io_uring_cqe_raw* cqes_;
    uint32_t sq_head_;
    uint32_t sq_tail_;
    uint32_t cq_head_;
    uint32_t cq_tail_;
    uint32_t ring_entries_;

    uint64_t total_submissions_;
    uint64_t total_completions_;
};

// C exported entry points for JIT lowered io-submit / io-poll opcodes
extern "C" int sys_io_submit(uint8_t opcode, int fd, void* addr, uint32_t len, uint64_t user_data);
extern "C" int sys_io_poll(uint64_t* user_data_out, int32_t* res_out);

} // namespace sys
} // namespace ana

#endif // ANA_IO_RING_H
