#include "ana_encoder.h"

namespace ana {
namespace backend {

AnaEncoder::AnaEncoder()
    : buffer_(nullptr), capacity_(0), cursor_(0), label_count_(0), reloc_count_(0) {
    ensure_capacity(4096);
}

AnaEncoder::~AnaEncoder() {
    if (buffer_) {
        ana::sys::raw_munmap(buffer_, capacity_);
        buffer_ = nullptr;
    }
}

void AnaEncoder::reset() {
    cursor_ = 0;
    label_count_ = 0;
    reloc_count_ = 0;
    for (uint32_t i = 0; i < 64; ++i) {
        labels_[i].id = i;
        labels_[i].offset = -1;
        labels_[i].bound = false;
    }
}

void AnaEncoder::ensure_capacity(size_t additional) {
    if (cursor_ + additional > capacity_) {
        size_t new_cap = (capacity_ == 0) ? 4096 : (capacity_ * 2 + additional + 4095) & ~4095UL;
        void* new_buf = ana::sys::raw_mmap(nullptr, new_cap, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
        if (new_buf && new_buf != reinterpret_cast<void*>(-1)) {
            if (buffer_ && cursor_ > 0) {
                ana::sys::freestanding_memcpy(new_buf, buffer_, cursor_);
                ana::sys::raw_munmap(buffer_, capacity_);
            }
            buffer_ = static_cast<uint8_t*>(new_buf);
            capacity_ = new_cap;
        }
    }
}

uint32_t AnaEncoder::new_label() {
    if (label_count_ < 64) {
        uint32_t id = label_count_++;
        labels_[id].id = id;
        labels_[id].offset = -1;
        labels_[id].bound = false;
        return id;
    }
    return 0;
}

void AnaEncoder::bind_label(uint32_t label_id) {
    if (label_id < label_count_) {
        labels_[label_id].offset = static_cast<int32_t>(cursor_);
        labels_[label_id].bound = true;
    }
}

void AnaEncoder::emit8(uint8_t byte) {
    ensure_capacity(1);
    buffer_[cursor_++] = byte;
}

void AnaEncoder::emit32(uint32_t dword) {
    ensure_capacity(4);
    ana::sys::freestanding_memcpy(buffer_ + cursor_, &dword, 4);
    cursor_ += 4;
}

void AnaEncoder::emit64(uint64_t qword) {
    ensure_capacity(8);
    ana::sys::freestanding_memcpy(buffer_ + cursor_, &qword, 8);
    cursor_ += 8;
}

void AnaEncoder::emit_bytes(const uint8_t* data, size_t len) {
    ensure_capacity(len);
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
    if (reloc_count_ < 128) {
        relocs_[reloc_count_++] = { label_id, cursor_, false };
    }
    emit32(0);
}

void AnaEncoder::je_label(uint32_t label_id) {
    emit8(0x0F); emit8(0x84);
    if (reloc_count_ < 128) {
        relocs_[reloc_count_++] = { label_id, cursor_, true };
    }
    emit32(0);
}

void AnaEncoder::jne_label(uint32_t label_id) {
    emit8(0x0F); emit8(0x85);
    if (reloc_count_ < 128) {
        relocs_[reloc_count_++] = { label_id, cursor_, true };
    }
    emit32(0);
}

void AnaEncoder::jl_label(uint32_t label_id) {
    emit8(0x0F); emit8(0x8C);
    if (reloc_count_ < 128) {
        relocs_[reloc_count_++] = { label_id, cursor_, true };
    }
    emit32(0);
}

void AnaEncoder::jge_label(uint32_t label_id) {
    emit8(0x0F); emit8(0x8D);
    if (reloc_count_ < 128) {
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

bool AnaEncoder::resolve_labels() {
    for (uint32_t i = 0; i < reloc_count_; ++i) {
        const LabelReloc& r = relocs_[i];
        if (r.label_id >= label_count_ || !labels_[r.label_id].bound) return false;

        int32_t target_off = labels_[r.label_id].offset;
        int32_t rel = target_off - static_cast<int32_t>(r.patch_offset + 4);
        uint32_t rel_u32 = static_cast<uint32_t>(rel);
        ana::sys::freestanding_memcpy(buffer_ + r.patch_offset, &rel_u32, 4);
    }
    return true;
}

} // namespace backend
} // namespace ana
