#ifndef ANA_EXCEPTION_UNWINDER_H
#define ANA_EXCEPTION_UNWINDER_H

#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

struct ExceptionTableEntry {
    uint8_t* code_start;
    uint8_t* code_end;
    uint8_t* catch_landing_pad;
    uint32_t exception_type_id;
};

class ExceptionUnwinder {
public:
    static ExceptionUnwinder& instance();

    ExceptionUnwinder();
    ~ExceptionUnwinder();

    void register_exception_table(uint8_t* start, uint8_t* end, uint8_t* landing_pad, uint32_t type_id);
    void throw_exception(void* exception_obj, uint32_t type_id);

private:
    ExceptionTableEntry tables_[128];
    uint32_t table_count_;
};

#if defined(_MSC_VER)
extern "C" __declspec(noreturn) void sys_throw_exception(void* exception_obj, uint32_t type_id);
#else
extern "C" void sys_throw_exception(void* exception_obj, uint32_t type_id) __attribute__((noreturn));
#endif

} // namespace backend
} // namespace ana

#endif // ANA_EXCEPTION_UNWINDER_H
