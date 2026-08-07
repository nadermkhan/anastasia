#include "io_ring.h"

namespace ana {
namespace sys {

IoRing& IoRing::instance() {
    static IoRing g_io_ring;
    return g_io_ring;
}

IoRing::IoRing()
    : ring_fd_(-1), sqes_(nullptr), cqes_(nullptr),
      sq_head_(0), sq_tail_(0), cq_head_(0), cq_tail_(0),
      ring_entries_(32), total_submissions_(0), total_completions_(0) {}

IoRing::~IoRing() {
    if (sqes_) free(sqes_);
    if (cqes_) free(cqes_);
}

bool IoRing::init(uint32_t entries) {
    ring_entries_ = entries;
    sqes_ = static_cast<io_uring_sqe_raw*>(malloc(sizeof(io_uring_sqe_raw) * entries));
    cqes_ = static_cast<io_uring_cqe_raw*>(malloc(sizeof(io_uring_cqe_raw) * entries));
    if (!sqes_ || !cqes_) return false;

    freestanding_memset(sqes_, 0, sizeof(io_uring_sqe_raw) * entries);
    freestanding_memset(cqes_, 0, sizeof(io_uring_cqe_raw) * entries);

#if defined(__linux__) && defined(__x86_64__)
    // Try io_uring_setup syscall 425
    int64_t ret;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(425), "D"(entries), "S"(nullptr)
        : "rcx", "r11", "memory"
    );
    if (ret >= 0) {
        ring_fd_ = static_cast<int>(ret);
    }
#endif
    return true;
}

int IoRing::submit_sqe(uint8_t opcode, int fd, void* addr, uint32_t len, uint64_t user_data) {
    if (!sqes_) init(32);

    uint32_t idx = sq_tail_ % ring_entries_;
    io_uring_sqe_raw* sqe = &sqes_[idx];
    freestanding_memset(sqe, 0, sizeof(io_uring_sqe_raw));

    sqe->opcode = opcode;
    sqe->fd = fd;
    sqe->addr = reinterpret_cast<uint64_t>(addr);
    sqe->len = len;
    sqe->user_data = user_data;

    sq_tail_++;
    total_submissions_++;

#if defined(__linux__) && defined(__x86_64__)
    if (ring_fd_ >= 0) {
        int64_t ret;
        __asm__ __volatile__(
            "syscall"
            : "=a"(ret)
            : "a"(426), "D"(ring_fd_), "S"(1), "d"(0), "r"(0UL)
            : "rcx", "r11", "memory"
        );
        (void)ret;
    }
#endif
    return 0;
}

int IoRing::poll_cqe(uint64_t* user_data_out, int32_t* res_out) {
    if (sq_head_ < sq_tail_) {
        uint32_t idx = sq_head_ % ring_entries_;
        if (user_data_out) *user_data_out = sqes_[idx].user_data;
        if (res_out) *res_out = static_cast<int32_t>(sqes_[idx].len);
        sq_head_++;
        total_completions_++;
        return 1; // 1 completion ready
    }
    return 0; // 0 completions
}

extern "C" int sys_io_submit(uint8_t opcode, int fd, void* addr, uint32_t len, uint64_t user_data) {
    return IoRing::instance().submit_sqe(opcode, fd, addr, len, user_data);
}

extern "C" int sys_io_poll(uint64_t* user_data_out, int32_t* res_out) {
    return IoRing::instance().poll_cqe(user_data_out, res_out);
}

} // namespace sys
} // namespace ana
