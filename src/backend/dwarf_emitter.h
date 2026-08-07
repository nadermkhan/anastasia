#ifndef ANA_DWARF_EMITTER_H
#define ANA_DWARF_EMITTER_H

#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

struct SourceLineEntry {
    uint32_t line_number;
    uint32_t pc_offset;
    SourceLineEntry* next;
};

class DwarfEmitter {
public:
    DwarfEmitter();
    ~DwarfEmitter();

    void add_line_entry(uint32_t line_number, uint32_t pc_offset);
    
    // Generates freestanding DWARF 4 .debug_line section byte payload
    uint8_t* build_debug_line_section(const char* filename, size_t* out_size);

    // Generates freestanding DWARF 4 .debug_info section payload
    uint8_t* build_debug_info_section(const char* compile_unit_name, size_t* out_size);

    // Generates freestanding DWARF 4 .debug_abbrev section payload
    uint8_t* build_debug_abbrev_section(size_t* out_size);

private:
    void emit8(uint8_t b);
    void emit16(uint16_t v);
    void emit32(uint32_t v);
    void emit64(uint64_t v);
    void emit_sleb128(int64_t val);
    void emit_uleb128(uint64_t val);
    void emit_string(const char* str);

    uint8_t* buffer_;
    size_t capacity_;
    size_t cursor_;

    SourceLineEntry* first_entry_;
    SourceLineEntry* last_entry_;
};

} // namespace backend
} // namespace ana

#endif // ANA_DWARF_EMITTER_H
