#include "pic_dispatcher.h"

namespace ana {
namespace backend {

PICDispatcher& PICDispatcher::instance() {
    static PICDispatcher g_pic_dispatcher;
    return g_pic_dispatcher;
}

PICDispatcher::PICDispatcher() {}
PICDispatcher::~PICDispatcher() {}

void* PICDispatcher::resolve_and_patch(PolymorphicICSite* site, void* receiver_vtable) {
    if (!site || !receiver_vtable) return nullptr;

    // Look up method target from receiver vtable
    void** vtable = static_cast<void**>(receiver_vtable);
    void* target_fn = vtable[site->vtable_slot];

    // Record receiver type in PIC site
    if (site->type_count < 8) {
        site->receiver_types[site->type_count] = receiver_vtable;
        site->method_targets[site->type_count] = target_fn;
        site->type_count++;
    }

    // State transition logic: Monomorphic -> Polymorphic -> Megamorphic
    if (site->type_count == 1) {
        site->state = PICState::MONOMORPHIC;
    } else if (site->type_count <= 3) {
        site->state = PICState::POLYMORPHIC;
    } else {
        site->state = PICState::MEGAMORPHIC;
    }

    return target_fn;
}

extern "C" void* handle_pic_miss(PolymorphicICSite* site, void* receiver_vtable) {
    return PICDispatcher::instance().resolve_and_patch(site, receiver_vtable);
}

} // namespace backend
} // namespace ana
