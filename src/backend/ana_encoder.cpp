#include "ana_encoder.h"

namespace ana {
namespace backend {

AnaEncoder::AnaEncoder()
    : failed_(false), buffer_(nullptr), capacity_(0), cursor_(0), label_count_(0), reloc_count_(0) {
    reset();
    ensure_capacity(4096);
}

AnaEncoder::~AnaEncoder() {
    if (buffer_) {
        ana::sys::raw_munmap(buffer_, capacity_);
        buffer_ = nullptr;
    }
}

void AnaEncoder::reset() {
    failed_ = false;
    cursor_ = 0;
    label_count_ = 0;
    reloc_count_ = 0;
    for (uint32_t i = 0; i < kMaxLabels; ++i) {
        labels_[i].id = i;
        labels_[i].offset = -1;
        labels_[i].bound = false;
    }
}

bool AnaEncoder::ensure_capacity(size_t additional) {
    if (cursor_ + additional <= capacity_) return true;

    size_t new_cap = (capacity_ == 0) ? 4096 : (capacity_ * 2 + additional + 4095) & ~4095UL;
    void* new_buf = ana::sys::raw_mmap(nullptr, new_cap, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (!new_buf || new_buf == reinterpret_cast<void*>(-1)) {
        // Out of memory: latch the failure so callers stop trusting the buffer
        // instead of writing past the end of the old mapping.
        failed_ = true;
        return false;
    }
    if (buffer_ && cursor_ > 0) {
        ana::sys::freestanding_memcpy(new_buf, buffer_, cursor_);
    }
    if (buffer_) {
        ana::sys::raw_munmap(buffer_, capacity_);
    }
    buffer_ = static_cast<uint8_t*>(new_buf);
    capacity_ = new_cap;
    return true;
}

uint32_t AnaEncoder::new_label() {
    if (label_count_ < kMaxLabels) {
        uint32_t id = label_count_++;
        labels_[id].id = id;
        labels_[id].offset = -1;
        labels_[id].bound = false;
        return id;
    }
    // Returning 0 here would silently alias label 0 and emit branches to the
    // wrong target, so record the overflow and let resolve_labels() reject it.
    failed_ = true;
    return kInvalidLabel;
}

void AnaEncoder::bind_label(uint32_t label_id) {
    if (label_id < label_count_) {
        labels_[label_id].offset = static_cast<int32_t>(cursor_);
        labels_[label_id].bound = true;
        return;
    }
    failed_ = true;
}

void AnaEncoder::emit8(uint8_t byte) {
    if (!ensure_capacity(1)) return;
    buffer_[cursor_++] = byte;
}

void AnaEncoder::emit32(uint32_t dword) {
    if (!ensure_capacity(4)) return;
    ana::sys::freestanding_memcpy(buffer_ + cursor_, &dword, 4);
    cursor_ += 4;
}

void AnaEncoder::emit64(uint64_t qword) {
    if (!ensure_capacity(8)) return;
    ana::sys::freestanding_memcpy(buffer_ + cursor_, &qword, 8);
    cursor_ += 8;
}

void AnaEncoder::emit_bytes(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    if (!ensure_capacity(len)) return;
    ana::sys::freestanding_memcpy(buffer_ + cursor_, data, len);
    cursor_ += len;
}

void AnaEncoder::emit_rex(bool w, uint8_t reg_idx, uint8_t index_idx, uint8_t base_idx) {
    uint8_t r = (reg_idx >= 8) ? 1 : 0;
    uint8_t x = (index_idx >= 8) ? 1 : 0;
    uint8_t b = (base_idx >= 8) ? 1 : 0;
    uint8_t rex = 0x40 | ((w ? 1 : 0) << 3) | (r << 2) | (x << 1) | b;
    if (w || r || x || b) {
        emit8(rex);
    }
}

void AnaEncoder::emit_modrm(uint8_t mod, uint8_t reg, uint8_t rm) {
    uint8_t modrm = ((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7);
    emit8(modrm);
}

void AnaEncoder::mov_reg_reg(X86Reg dst, X86Reg src) {
    if (dst == src) return;
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, s, 0, d);
    emit8(0x89);
    emit_modrm(3, s, d);
}

void AnaEncoder::mov_reg_imm64(X86Reg dst, uint64_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    emit8(0xB8 + (d & 7));
    emit64(imm);
}

void AnaEncoder::mov_reg_imm32(X86Reg dst, int32_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    emit8(0xC7);
    emit_modrm(3, 0, d);
    emit32(static_cast<uint32_t>(imm));
}

void AnaEncoder::mov_reg_mem(X86Reg dst, X86Reg base, int32_t disp) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t b = static_cast<uint8_t>(base);
    emit_rex(true, d, 0, b);
    emit8(0x8B);

    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, d, b);
        if ((b & 7) == 4) emit8(0x24); // SIB byte for RSP/R12
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, d, b);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, d, b);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::mov_mem_reg(X86Reg base, int32_t disp, X86Reg src) {
    uint8_t b = static_cast<uint8_t>(base);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, s, 0, b);
    emit8(0x89);

    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, s, b);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::add_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, s, 0, d);
    emit8(0x01);
    emit_modrm(3, s, d);
}

void AnaEncoder::add_reg_imm32(X86Reg dst, int32_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    if (imm >= -128 && imm <= 127) {
        emit8(0x83);
        emit_modrm(3, 0, d);
        emit8(static_cast<uint8_t>(imm));
    } else {
        emit8(0x81);
        emit_modrm(3, 0, d);
        emit32(static_cast<uint32_t>(imm));
    }
}

void AnaEncoder::add_mem_reg(X86Reg base, int32_t disp, X86Reg src) {
    uint8_t b = static_cast<uint8_t>(base);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, s, 0, b);
    emit8(0x01);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, s, b);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::sub_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, s, 0, d);
    emit8(0x29);
    emit_modrm(3, s, d);
}

void AnaEncoder::sub_reg_imm32(X86Reg dst, int32_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    if (imm >= -128 && imm <= 127) {
        emit8(0x83);
        emit_modrm(3, 5, d);
        emit8(static_cast<uint8_t>(imm));
    } else {
        emit8(0x81);
        emit_modrm(3, 5, d);
        emit32(static_cast<uint32_t>(imm));
    }
}

void AnaEncoder::imul_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, d, 0, s);
    emit8(0x0F);
    emit8(0xAF);
    emit_modrm(3, d, s);
}

void AnaEncoder::imul_reg_imm32(X86Reg dst, X86Reg src, int32_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, d, 0, s);
    if (imm >= -128 && imm <= 127) {
        emit8(0x6B);
        emit_modrm(3, d, s);
        emit8(static_cast<uint8_t>(imm));
    } else {
        emit8(0x69);
        emit_modrm(3, d, s);
        emit32(static_cast<uint32_t>(imm));
    }
}

void AnaEncoder::and_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, s, 0, d);
    emit8(0x21);
    emit_modrm(3, s, d);
}

void AnaEncoder::and_reg_imm32(X86Reg dst, int32_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    if (imm >= -128 && imm <= 127) {
        emit8(0x83);
        emit_modrm(3, 4, d);
        emit8(static_cast<uint8_t>(imm));
    } else {
        emit8(0x81);
        emit_modrm(3, 4, d);
        emit32(static_cast<uint32_t>(imm));
    }
}

void AnaEncoder::or_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, s, 0, d);
    emit8(0x09);
    emit_modrm(3, s, d);
}

void AnaEncoder::or_reg_imm32(X86Reg dst, int32_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    if (imm >= -128 && imm <= 127) {
        emit8(0x83);
        emit_modrm(3, 1, d);
        emit8(static_cast<uint8_t>(imm));
    } else {
        emit8(0x81);
        emit_modrm(3, 1, d);
        emit32(static_cast<uint32_t>(imm));
    }
}

void AnaEncoder::xor_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, s, 0, d);
    emit8(0x31);
    emit_modrm(3, s, d);
}

void AnaEncoder::xor_reg_imm32(X86Reg dst, int32_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    if (imm >= -128 && imm <= 127) {
        emit8(0x83);
        emit_modrm(3, 6, d);
        emit8(static_cast<uint8_t>(imm));
    } else {
        emit8(0x81);
        emit_modrm(3, 6, d);
        emit32(static_cast<uint32_t>(imm));
    }
}

void AnaEncoder::shl_reg_cl(X86Reg dst) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    emit8(0xD3);
    emit_modrm(3, 4, d);
}

void AnaEncoder::shl_reg_imm8(X86Reg dst, uint8_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    if (imm == 1) {
        emit8(0xD1);
        emit_modrm(3, 4, d);
    } else {
        emit8(0xC1);
        emit_modrm(3, 4, d);
        emit8(imm);
    }
}

void AnaEncoder::sar_reg_cl(X86Reg dst) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    emit8(0xD3);
    emit_modrm(3, 7, d);
}

void AnaEncoder::sar_reg_imm8(X86Reg dst, uint8_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    if (imm == 1) {
        emit8(0xD1);
        emit_modrm(3, 7, d);
    } else {
        emit8(0xC1);
        emit_modrm(3, 7, d);
        emit8(imm);
    }
}

void AnaEncoder::shr_reg_cl(X86Reg dst) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    emit8(0xD3);
    emit_modrm(3, 5, d);
}

void AnaEncoder::shr_reg_imm8(X86Reg dst, uint8_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    if (imm == 1) {
        emit8(0xD1);
        emit_modrm(3, 5, d);
    } else {
        emit8(0xC1);
        emit_modrm(3, 5, d);
        emit8(imm);
    }
}

void AnaEncoder::bts_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, s, 0, d);
    emit8(0x0F);
    emit8(0xAB);
    emit_modrm(3, s, d);
}

void AnaEncoder::bts_reg_imm8(X86Reg dst, uint8_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    emit8(0x0F);
    emit8(0xBA);
    emit_modrm(3, 5, d);
    emit8(imm);
}

void AnaEncoder::btr_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, s, 0, d);
    emit8(0x0F);
    emit8(0xB3);
    emit_modrm(3, s, d);
}

void AnaEncoder::btr_reg_imm8(X86Reg dst, uint8_t imm) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, 0, 0, d);
    emit8(0x0F);
    emit8(0xBA);
    emit_modrm(3, 6, d);
    emit8(imm);
}

void AnaEncoder::popcnt_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit8(0xF3);
    emit_rex(true, d, 0, s);
    emit8(0x0F);
    emit8(0xB8);
    emit_modrm(3, d, s);
}

void AnaEncoder::lzcnt_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit8(0xF3);
    emit_rex(true, d, 0, s);
    emit8(0x0F);
    emit8(0xBD);
    emit_modrm(3, d, s);
}

void AnaEncoder::cmp_reg_reg(X86Reg src1, X86Reg src2) {
    uint8_t s1 = static_cast<uint8_t>(src1);
    uint8_t s2 = static_cast<uint8_t>(src2);
    emit_rex(true, s2, 0, s1);
    emit8(0x39);
    emit_modrm(3, s2, s1);
}

void AnaEncoder::cmp_reg_imm32(X86Reg src1, int32_t imm) {
    uint8_t s1 = static_cast<uint8_t>(src1);
    emit_rex(true, 0, 0, s1);
    if (imm >= -128 && imm <= 127) {
        emit8(0x83);
        emit_modrm(3, 7, s1);
        emit8(static_cast<uint8_t>(imm));
    } else {
        emit8(0x81);
        emit_modrm(3, 7, s1);
        emit32(static_cast<uint32_t>(imm));
    }
}

void AnaEncoder::test_reg_reg(X86Reg src1, X86Reg src2) {
    uint8_t s1 = static_cast<uint8_t>(src1);
    uint8_t s2 = static_cast<uint8_t>(src2);
    emit_rex(true, s2, 0, s1);
    emit8(0x85);
    emit_modrm(3, s2, s1);
}

void AnaEncoder::jmp_label(uint32_t label_id) {
    emit8(0xE9);
    if (reloc_count_ >= kMaxRelocs) failed_ = true;
    if (reloc_count_ < kMaxRelocs) {
        relocs_[reloc_count_++] = { label_id, cursor_, false };
    }
    emit32(0);
}

void AnaEncoder::je_label(uint32_t label_id) {
    emit8(0x0F); emit8(0x84);
    if (reloc_count_ >= kMaxRelocs) failed_ = true;
    if (reloc_count_ < kMaxRelocs) {
        relocs_[reloc_count_++] = { label_id, cursor_, true };
    }
    emit32(0);
}

void AnaEncoder::jne_label(uint32_t label_id) {
    emit8(0x0F); emit8(0x85);
    if (reloc_count_ >= kMaxRelocs) failed_ = true;
    if (reloc_count_ < kMaxRelocs) {
        relocs_[reloc_count_++] = { label_id, cursor_, true };
    }
    emit32(0);
}

void AnaEncoder::jl_label(uint32_t label_id) {
    emit8(0x0F); emit8(0x8C);
    if (reloc_count_ >= kMaxRelocs) failed_ = true;
    if (reloc_count_ < kMaxRelocs) {
        relocs_[reloc_count_++] = { label_id, cursor_, true };
    }
    emit32(0);
}

void AnaEncoder::jge_label(uint32_t label_id) {
    emit8(0x0F); emit8(0x8D);
    if (reloc_count_ >= kMaxRelocs) failed_ = true;
    if (reloc_count_ < kMaxRelocs) {
        relocs_[reloc_count_++] = { label_id, cursor_, true };
    }
    emit32(0);
}

void AnaEncoder::jz_label(uint32_t label_id) {
    je_label(label_id);
}

void AnaEncoder::jnz_label(uint32_t label_id) {
    jne_label(label_id);
}

void AnaEncoder::call_reg(X86Reg target) {
    uint8_t t = static_cast<uint8_t>(target);
    emit_rex(false, 0, 0, t);
    emit8(0xFF);
    emit_modrm(3, 2, t);
}

void AnaEncoder::call_rel32_disp(int32_t disp) {
    emit8(0xE8);
    emit32(static_cast<uint32_t>(disp));
}

void AnaEncoder::push_reg(X86Reg reg) {
    uint8_t r = static_cast<uint8_t>(reg);
    if (r >= 8) emit8(0x41);
    emit8(0x50 + (r & 7));
}

void AnaEncoder::pop_reg(X86Reg reg) {
    uint8_t r = static_cast<uint8_t>(reg);
    if (r >= 8) emit8(0x41);
    emit8(0x58 + (r & 7));
}

void AnaEncoder::ret() {
    emit8(0xC3);
}

void AnaEncoder::lock_cmpxchg_mem_reg(X86Reg base, int32_t disp, X86Reg src) {
    uint8_t b = static_cast<uint8_t>(base);
    uint8_t s = static_cast<uint8_t>(src);
    emit8(0xF0); // LOCK
    emit_rex(true, s, 0, b);
    emit8(0x0F); emit8(0xB1);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, s, b);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::cqo() {
    emit8(0x48); emit8(0x99); // CQO (sign extend RAX to RDX:RAX)
}

void AnaEncoder::idiv_reg(X86Reg reg) {
    uint8_t r = static_cast<uint8_t>(reg);
    emit_rex(true, 7 /* /7 for idiv */, 0, r);
    emit8(0xF7);
    emit_modrm(3, 7, r);
}

void AnaEncoder::xchg_mem_reg(X86Reg base, int32_t disp, X86Reg src) {
    uint8_t b = static_cast<uint8_t>(base);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, s, 0, b);
    emit8(0x87);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, s, b);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::lock_add_mem_reg(X86Reg base, int32_t disp, X86Reg src) {
    uint8_t b = static_cast<uint8_t>(base);
    uint8_t s = static_cast<uint8_t>(src);
    emit8(0xF0); // LOCK
    emit_rex(true, s, 0, b);
    emit8(0x01);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, s, b);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::lock_and_mem_reg(X86Reg base, int32_t disp, X86Reg src) {
    uint8_t b = static_cast<uint8_t>(base);
    uint8_t s = static_cast<uint8_t>(src);
    emit8(0xF0); // LOCK
    emit_rex(true, s, 0, b);
    emit8(0x21);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, s, b);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::lock_or_mem_reg(X86Reg base, int32_t disp, X86Reg src) {
    uint8_t b = static_cast<uint8_t>(base);
    uint8_t s = static_cast<uint8_t>(src);
    emit8(0xF0); // LOCK
    emit_rex(true, s, 0, b);
    emit8(0x09);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, s, b);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, s, b);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::mfence() {
    emit8(0x0F); emit8(0xAE); emit8(0xF0);
}

void AnaEncoder::clflush(X86Reg base, int32_t disp) {
    uint8_t b = static_cast<uint8_t>(base);
    emit_rex(false, 0, 0, b);
    emit8(0x0F); emit8(0xAE);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, 7, b);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, 7, b);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, 7, b);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::addss_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm) {
    emit8(0xF3);
    if (dst_xmm >= 8 || src_xmm >= 8) emit_rex(false, dst_xmm, 0, src_xmm);
    emit8(0x0F); emit8(0x58);
    emit_modrm(3, dst_xmm & 7, src_xmm & 7);
}

void AnaEncoder::addsd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm) {
    emit8(0xF2);
    if (dst_xmm >= 8 || src_xmm >= 8) emit_rex(false, dst_xmm, 0, src_xmm);
    emit8(0x0F); emit8(0x58);
    emit_modrm(3, dst_xmm & 7, src_xmm & 7);
}

void AnaEncoder::subsd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm) {
    emit8(0xF2);
    if (dst_xmm >= 8 || src_xmm >= 8) emit_rex(false, dst_xmm, 0, src_xmm);
    emit8(0x0F); emit8(0x5C);
    emit_modrm(3, dst_xmm & 7, src_xmm & 7);
}

void AnaEncoder::mulsd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm) {
    emit8(0xF2);
    if (dst_xmm >= 8 || src_xmm >= 8) emit_rex(false, dst_xmm, 0, src_xmm);
    emit8(0x0F); emit8(0x5E);
    emit_modrm(3, dst_xmm & 7, src_xmm & 7);
}

void AnaEncoder::divsd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm) {
    emit8(0xF2);
    if (dst_xmm >= 8 || src_xmm >= 8) emit_rex(false, dst_xmm, 0, src_xmm);
    emit8(0x0F); emit8(0x5F);
    emit_modrm(3, dst_xmm & 7, src_xmm & 7);
}

void AnaEncoder::paddd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm) {
    emit8(0x66);
    if (dst_xmm >= 8 || src_xmm >= 8) emit_rex(false, dst_xmm, 0, src_xmm);
    emit8(0x0F); emit8(0xFE);
    emit_modrm(3, dst_xmm & 7, src_xmm & 7);
}

void AnaEncoder::psubd_xmm_xmm(uint8_t dst_xmm, uint8_t src_xmm) {
    emit8(0x66);
    if (dst_xmm >= 8 || src_xmm >= 8) emit_rex(false, dst_xmm, 0, src_xmm);
    emit8(0x0F); emit8(0xFA);
    emit_modrm(3, dst_xmm & 7, src_xmm & 7);
}

void AnaEncoder::movss_xmm_mem(uint8_t dst_xmm, X86Reg base, int32_t disp) {
    uint8_t b = static_cast<uint8_t>(base);
    emit8(0xF3);
    if (dst_xmm >= 8 || b >= 8) emit_rex(false, dst_xmm, 0, b);
    emit8(0x0F); emit8(0x10);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, dst_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, dst_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, dst_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::movss_mem_xmm(X86Reg base, int32_t disp, uint8_t src_xmm) {
    uint8_t b = static_cast<uint8_t>(base);
    emit8(0xF3);
    if (src_xmm >= 8 || b >= 8) emit_rex(false, src_xmm, 0, b);
    emit8(0x0F); emit8(0x11);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::movsd_xmm_mem(uint8_t dst_xmm, X86Reg base, int32_t disp) {
    uint8_t b = static_cast<uint8_t>(base);
    emit8(0xF2);
    if (dst_xmm >= 8 || b >= 8) emit_rex(false, dst_xmm, 0, b);
    emit8(0x0F); emit8(0x10);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, dst_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, dst_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, dst_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::movsd_mem_xmm(X86Reg base, int32_t disp, uint8_t src_xmm) {
    uint8_t b = static_cast<uint8_t>(base);
    emit8(0xF2);
    if (src_xmm >= 8 || b >= 8) emit_rex(false, src_xmm, 0, b);
    emit8(0x0F); emit8(0x11);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::movdqu_xmm_mem(uint8_t dst_xmm, X86Reg base, int32_t disp) {
    uint8_t b = static_cast<uint8_t>(base);
    emit8(0xF3);
    if (dst_xmm >= 8 || b >= 8) emit_rex(false, dst_xmm, 0, b);
    emit8(0x0F); emit8(0x6F);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, dst_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, dst_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, dst_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::movdqu_mem_xmm(X86Reg base, int32_t disp, uint8_t src_xmm) {
    uint8_t b = static_cast<uint8_t>(base);
    emit8(0xF3);
    if (src_xmm >= 8 || b >= 8) emit_rex(false, src_xmm, 0, b);
    emit8(0x0F); emit8(0x7F);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::emit_vex3(uint8_t m_mmmm, uint8_t pp, bool w, uint8_t vvvv, bool l, uint8_t r, uint8_t x, uint8_t b) {
    // NOTE: the compact two-byte VEX (0xC5) form would be legal here whenever
    // map==0F && X==0 && B==0 && !W, and decodes identically. We deliberately
    // always emit the three-byte 0xC4 form instead: it is valid for every case,
    // keeps one code path, and the engine's own encoding tests assert a 0xC4
    // lead byte. Switching to 0xC5 is a pure size optimisation, not a
    // correctness fix, and it regressed those tests.
    emit8(0xC4);
    uint8_t byte1 = ((~r & 1) << 7) | ((~x & 1) << 6) | ((~b & 1) << 5) | (m_mmmm & 0x1F);
    uint8_t byte2 = ((w ? 1 : 0) << 7) | ((~vvvv & 0x0F) << 3) | ((l ? 1 : 0) << 2) | (pp & 0x03);
    emit8(byte1);
    emit8(byte2);
}

void AnaEncoder::emit_evex(uint8_t mm, uint8_t pp, bool w, uint8_t vvvv, uint8_t lll, uint8_t r, uint8_t x, uint8_t b, uint8_t aaa) {
    emit8(0x62);
    // P0 = R X B R' 0 0 m m. R/X/B/R' are stored inverted; R'=1 selects the
    // low 16 registers. Leaving R'=0 silently addressed zmm16-31.
    uint8_t byte1 = static_cast<uint8_t>(((~r & 1) << 7) | ((~x & 1) << 6) |
                                         ((~b & 1) << 5) | (1 << 4) | (mm & 0x03));
    // P1 = W vvvv 1 pp, vvvv inverted.
    uint8_t byte2 = static_cast<uint8_t>(((w ? 1 : 0) << 7) | ((~vvvv & 0x0F) << 3) |
                                         0x04 | (pp & 0x03));
    // P2 = z L'L b V' aaa. z must be 0 with aaa=0 (z=1 without a mask is #UD)
    // and V'=1 selects the low 16 registers.
    uint8_t byte3 = static_cast<uint8_t>(((lll & 0x03) << 5) | (1 << 3) | (aaa & 0x07));
    emit8(byte1);
    emit8(byte2);
    emit8(byte3);
}

void AnaEncoder::vpaddd_ymm_ymm(uint8_t dst_ymm, uint8_t src1_ymm, uint8_t src2_ymm) {
    // VPADDD is VEX.256.66.0F.WIG FE /r - opcode map 0F, not 0F38.
    emit_vex3(0x01 /* 0F */, 0x01 /* 66 */, false, src1_ymm, true /* 256-bit */, dst_ymm >> 3, 0, src2_ymm >> 3);
    emit8(0xFE); // VPADDD
    emit_modrm(3, dst_ymm & 7, src2_ymm & 7);
}

void AnaEncoder::vpaddd_zmm_zmm(uint8_t dst_zmm, uint8_t src1_zmm, uint8_t src2_zmm) {
    // EVEX.512.66.0F.W0 FE /r - opcode map 0F, not 0F38.
    emit_evex(0x01 /* 0F */, 0x01 /* 66 */, false, src1_zmm, 0x02 /* 512-bit */, dst_zmm >> 3, 0, src2_zmm >> 3, 0);
    emit8(0xFE); // VPADDD
    emit_modrm(3, dst_zmm & 7, src2_zmm & 7);
}

void AnaEncoder::vpmulld_ymm_ymm(uint8_t dst_ymm, uint8_t src1_ymm, uint8_t src2_ymm) {
    emit_vex3(0x02 /* 0F38 */, 0x01 /* 66 */, false, src1_ymm, true /* 256-bit */, dst_ymm >> 3, 0, src2_ymm >> 3);
    emit8(0x40); // VPMULLD
    emit_modrm(3, dst_ymm & 7, src2_ymm & 7);
}

void AnaEncoder::vmovdqu_ymm_mem(uint8_t dst_ymm, X86Reg base, int32_t disp) {
    uint8_t b = static_cast<uint8_t>(base);
    emit_vex3(0x01 /* 0F */, 0x02 /* F3 */, false, 0, true /* 256-bit */, dst_ymm >> 3, 0, b >> 3);
    emit8(0x6F); // VMOVDQU
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, dst_ymm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, dst_ymm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, dst_ymm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::vmovdqu_mem_ymm(X86Reg base, int32_t disp, uint8_t src_ymm) {
    uint8_t b = static_cast<uint8_t>(base);
    emit_vex3(0x01 /* 0F */, 0x02 /* F3 */, false, 0, true /* 256-bit */, src_ymm >> 3, 0, b >> 3);
    emit8(0x7F); // VMOVDQU
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, src_ymm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, src_ymm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, src_ymm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::vmovdqu_zmm_mem(uint8_t dst_zmm, X86Reg base, int32_t disp) {
    uint8_t b = static_cast<uint8_t>(base);
    emit_evex(0x01 /* 0F */, 0x02 /* F3 */, false, 0, 0x02 /* 512-bit */, dst_zmm >> 3, 0, b >> 3, 0);
    emit8(0x6F); // VMOVDQU64
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, dst_zmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, dst_zmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, dst_zmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::vmovdqu_mem_zmm(X86Reg base, int32_t disp, uint8_t src_zmm) {
    uint8_t b = static_cast<uint8_t>(base);
    emit_evex(0x01 /* 0F */, 0x02 /* F3 */, false, 0, 0x02 /* 512-bit */, src_zmm >> 3, 0, b >> 3, 0);
    emit8(0x7F); // VMOVDQU64
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, src_zmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, src_zmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, src_zmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::vmovntdq_ymm_mem(X86Reg base, int32_t disp, uint8_t src_ymm) {
    uint8_t b = static_cast<uint8_t>(base);
    emit_vex3(0x01 /* 0F */, 0x01 /* 66 */, false, 0, true /* 256-bit */, src_ymm >> 3, 0, b >> 3);
    emit8(0xE7); // VMOVNTDQ
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, src_ymm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, src_ymm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, src_ymm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::vmovntdq_zmm_mem(X86Reg base, int32_t disp, uint8_t src_zmm) {
    uint8_t b = static_cast<uint8_t>(base);
    emit_evex(0x01 /* 0F */, 0x01 /* 66 */, false, 0, 0x02 /* 512-bit */, src_zmm >> 3, 0, b >> 3, 0);
    emit8(0xE7); // VMOVNTDQ
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, src_zmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, src_zmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, src_zmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::movntdq_mem_xmm(X86Reg base, int32_t disp, uint8_t src_xmm) {
    uint8_t b = static_cast<uint8_t>(base);
    emit8(0x66);
    if (src_xmm >= 8 || b >= 8) emit_rex(false, src_xmm, 0, b);
    emit8(0x0F); emit8(0xE7); // MOVNTDQ
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, src_xmm & 7, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::sfence() {
    emit8(0x0F); emit8(0xAE); emit8(0xF8); // SFENCE
}

void AnaEncoder::prefetcht0(X86Reg base, int32_t disp) {
    uint8_t b = static_cast<uint8_t>(base);
    if (b >= 8) emit_rex(false, 1, 0, b);
    emit8(0x0F); emit8(0x18);
    if (disp == 0 && (b & 7) != 5) {
        emit_modrm(0, 1 /* PREFETCHT0 */, b & 7);
        if ((b & 7) == 4) emit8(0x24);
    } else if (disp >= -128 && disp <= 127) {
        emit_modrm(1, 1, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit8(static_cast<uint8_t>(disp));
    } else {
        emit_modrm(2, 1, b & 7);
        if ((b & 7) == 4) emit8(0x24);
        emit32(static_cast<uint32_t>(disp));
    }
}

void AnaEncoder::lea_reg_rip_disp32(X86Reg dst, int32_t disp32) {
    uint8_t d = static_cast<uint8_t>(dst);
    emit_rex(true, d, 0, 0);
    emit8(0x8D);
    emit_modrm(0, d & 7, 5);
    emit32(static_cast<uint32_t>(disp32));
}

bool AnaEncoder::resolve_labels() {
    // Any latched emission/label/relocation overflow makes the buffer
    // untrustworthy, so refuse to hand it back as runnable code.
    if (failed_ || !buffer_) return false;

    for (uint32_t i = 0; i < reloc_count_; ++i) {
        const LabelReloc& r = relocs_[i];
        if (r.label_id >= label_count_ || !labels_[r.label_id].bound) return false;
        if (r.patch_offset + 4 > cursor_) return false;

        int64_t target_off = labels_[r.label_id].offset;
        int64_t rel = target_off - static_cast<int64_t>(r.patch_offset + 4);
        if (rel < -2147483648LL || rel > 2147483647LL) return false; // rel32 range
        uint32_t rel_u32 = static_cast<uint32_t>(static_cast<int32_t>(rel));
        ana::sys::freestanding_memcpy(buffer_ + r.patch_offset, &rel_u32, 4);
    }
    return true;
}

void AnaEncoder::movsxd_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(true, d, 0, s);
    emit8(0x63);
    emit_modrm(3, d & 7, s & 7);
}

void AnaEncoder::movzxd_reg_reg(X86Reg dst, X86Reg src) {
    uint8_t d = static_cast<uint8_t>(dst);
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(false, d, 0, s);
    emit8(0x8B);
    emit_modrm(3, d & 7, s & 7);
}

void AnaEncoder::cdq() {
    emit8(0x99);
}

void AnaEncoder::idiv_reg32(X86Reg src) {
    uint8_t s = static_cast<uint8_t>(src);
    emit_rex(false, 7, 0, s);
    emit8(0xF7);
    emit_modrm(3, 7, s & 7);
}

} // namespace backend
} // namespace ana
