#include "dwarf_emitter.h"

namespace ana {
namespace backend {

DwarfEmitter::DwarfEmitter()
    : buffer_(nullptr), capacity_(1024), cursor_(0), first_entry_(nullptr), last_entry_(nullptr) {
    buffer_ = static_cast<uint8_t*>(malloc(capacity_));
}

DwarfEmitter::~DwarfEmitter() {
    if (buffer_) free(buffer_);
    SourceLineEntry* curr = first_entry_;
    while (curr) {
        SourceLineEntry* next = curr->next;
        free(curr);
        curr = next;
    }
}

void DwarfEmitter::add_line_entry(uint32_t line_number, uint32_t pc_offset) {
    SourceLineEntry* entry = static_cast<SourceLineEntry*>(malloc(sizeof(SourceLineEntry)));
    if (!entry) return;
    entry->line_number = line_number;
    entry->pc_offset = pc_offset;
    entry->next = nullptr;

    if (!first_entry_) {
        first_entry_ = entry;
        last_entry_ = entry;
    } else {
        last_entry_->next = entry;
        last_entry_ = entry;
    }
}

void DwarfEmitter::emit8(uint8_t b) {
    if (cursor_ >= capacity_) {
        capacity_ *= 2;
        buffer_ = static_cast<uint8_t*>(realloc(buffer_, capacity_));
    }
    buffer_[cursor_++] = b;
}

void DwarfEmitter::emit16(uint16_t v) {
    emit8(static_cast<uint8_t>(v & 0xFF));
    emit8(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void DwarfEmitter::emit32(uint32_t v) {
    emit8(static_cast<uint8_t>(v & 0xFF));
    emit8(static_cast<uint8_t>((v >> 8) & 0xFF));
    emit8(static_cast<uint8_t>((v >> 16) & 0xFF));
    emit8(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void DwarfEmitter::emit64(uint64_t v) {
    emit32(static_cast<uint32_t>(v & 0xFFFFFFFFUL));
    emit32(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFUL));
}

void DwarfEmitter::emit_uleb128(uint64_t val) {
    do {
        uint8_t b = static_cast<uint8_t>(val & 0x7F);
        val >>= 7;
        if (val != 0) b |= 0x80;
        emit8(b);
    } while (val != 0);
}

void DwarfEmitter::emit_sleb128(int64_t val) {
    bool more = true;
    while (more) {
        uint8_t byte = val & 0x7f;
        val >>= 7;
        if ((val == 0 && (byte & 0x40) == 0) || (val == -1 && (byte & 0x40) != 0)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        emit8(byte);
    }
}

void DwarfEmitter::emit_string(const char* str) {
    if (!str) {
        emit8(0);
        return;
    }
    while (*str) {
        emit8(static_cast<uint8_t>(*str++));
    }
    emit8(0);
}

uint8_t* DwarfEmitter::build_debug_line_section(const char* filename, size_t* out_size) {
    cursor_ = 0;
    const char* file_name = (filename && *filename) ? filename : "anastasia_module.ana";

    // Standard DWARF 4 .debug_line Header
    size_t length_patch_pos = cursor_;
    emit32(0); // Total length placeholder
    emit16(4); // DWARF Version 4

    size_t prologue_length_patch_pos = cursor_;
    emit32(0); // Prologue length placeholder
    size_t prologue_start = cursor_;

    emit8(1);  // minimum_instruction_length
    emit8(1);  // maximum_ops_per_instruction
    emit8(1);  // default_is_stmt
    emit8(-5); // line_base
    emit8(14); // line_range
    emit8(13); // opcode_base

    // Standard opcode lengths for opcodes 1..12
    uint8_t opcode_lengths[] = { 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1 };
    for (size_t i = 0; i < 12; ++i) emit8(opcode_lengths[i]);

    // Include directories (empty list end)
    emit8(0);

    // File names table
    emit_string(file_name);
    emit_uleb128(0); // Dir index
    emit_uleb128(0); // Mod time
    emit_uleb128(0); // File size
    emit8(0);        // End of file table

    size_t prologue_end = cursor_;
    uint32_t prologue_len = static_cast<uint32_t>(prologue_end - prologue_start);
    *reinterpret_cast<uint32_t*>(&buffer_[prologue_length_patch_pos]) = prologue_len;

    // DW_LNE_set_address (Extended opcode 0x00, len 9, op 0x02)
    emit8(0);
    emit_uleb128(9);
    emit8(2); // DW_LNE_set_address
    emit64(0); // Relocated against .text start address

    uint32_t cur_pc = 0;
    uint32_t cur_line = 1;

    for (SourceLineEntry* e = first_entry_; e != nullptr; e = e->next) {
        int32_t line_delta = static_cast<int32_t>(e->line_number) - static_cast<int32_t>(cur_line);
        uint32_t pc_delta = e->pc_offset - cur_pc;

        if (pc_delta > 0) {
            emit8(2); // DW_LNS_advance_pc
            emit_uleb128(pc_delta);
            cur_pc = e->pc_offset;
        }

        if (line_delta != 0) {
            emit8(3); // DW_LNS_advance_line
            emit_sleb128(line_delta);
            cur_line = e->line_number;
        }

        emit8(1); // DW_LNS_copy
    }

    // DW_LNE_end_sequence (Extended opcode 0x00, len 1, op 0x01)
    emit8(0);
    emit_uleb128(1);
    emit8(1);

    uint32_t total_len = static_cast<uint32_t>(cursor_ - length_patch_pos - 4);
    *reinterpret_cast<uint32_t*>(&buffer_[length_patch_pos]) = total_len;

    if (out_size) *out_size = cursor_;
    uint8_t* result = static_cast<uint8_t*>(malloc(cursor_));
    sys::freestanding_memcpy(result, buffer_, cursor_);
    return result;
}

uint8_t* DwarfEmitter::build_debug_info_section(const char* compile_unit_name, size_t* out_size) {
    cursor_ = 0;
    const char* cu_name = compile_unit_name ? compile_unit_name : "anastasia_unit";

    emit32(0); // Unit Length placeholder
    emit16(4); // DWARF Version 4
    emit32(0); // .debug_abbrev offset
    emit8(8);  // Address size (64-bit)

    emit_uleb128(1); // Abbrev code 1 (DW_TAG_compile_unit)
    emit_string(cu_name); // DW_AT_name
    emit_string("Anastasia bare-metal JIT/AOT v3.0"); // DW_AT_producer
    emit16(0x8001); // DW_AT_language (DW_LANG_C99)

    uint32_t total_len = static_cast<uint32_t>(cursor_ - 4);
    *reinterpret_cast<uint32_t*>(&buffer_[0]) = total_len;

    if (out_size) *out_size = cursor_;
    uint8_t* result = static_cast<uint8_t*>(malloc(cursor_));
    sys::freestanding_memcpy(result, buffer_, cursor_);
    return result;
}

uint8_t* DwarfEmitter::build_debug_abbrev_section(size_t* out_size) {
    cursor_ = 0;

    emit_uleb128(1); // Abbrev code 1
    emit_uleb128(0x11); // DW_TAG_compile_unit
    emit8(1); // DW_CHILDREN_yes

    emit_uleb128(0x03); emit_uleb128(0x08); // DW_AT_name, DW_FORM_string
    emit_uleb128(0x25); emit_uleb128(0x08); // DW_AT_producer, DW_FORM_string
    emit_uleb128(0x13); emit_uleb128(0x05); // DW_AT_language, DW_FORM_data2
    emit_uleb128(0);    emit_uleb128(0);

    emit_uleb128(0); // End abbrev table

    if (out_size) *out_size = cursor_;
    uint8_t* result = static_cast<uint8_t*>(malloc(cursor_));
    sys::freestanding_memcpy(result, buffer_, cursor_);
    return result;
}

} // namespace backend
} // namespace ana
