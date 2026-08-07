#ifndef ANA_PIC_DISPATCHER_H
#define ANA_PIC_DISPATCHER_H

#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

enum class PICState {
    MONOMORPHIC,
    POLYMORPHIC,
    MEGAMORPHIC
};

struct PolymorphicICSite {
    uint8_t* patch_addr;
    int32_t vtable_slot;
    PICState state;
    uint32_t type_count;
    void* receiver_types[8];
    void* method_targets[8];
};

class PICDispatcher {
public:
    static PICDispatcher& instance();

    PICDispatcher();
    ~PICDispatcher();

    void* resolve_and_patch(PolymorphicICSite* site, void* receiver_vtable);
};

// C export for JIT miss trampoline
extern "C" void* handle_pic_miss(PolymorphicICSite* site, void* receiver_vtable);

} // namespace backend
} // namespace ana

#endif // ANA_PIC_DISPATCHER_H
