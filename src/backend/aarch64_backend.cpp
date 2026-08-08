#include "aarch64_backend.h"
#include "elf_emitter.h"

namespace ana {
namespace backend {

static uint8_t arm64_u8(Arm64Reg r) {
    return static_cast<uint8_t>(r) & 31;
}

AArch64Encoder::AArch64Encoder() : buffer_(nullptr), capacity_(16384), size_(0) {
    size_t alloc_bytes = capacity_ * sizeof(uint32_t);
    void* ptr = sys::raw_mmap(nullptr, alloc_bytes, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (ptr == (void*)-1 || !ptr) {
        buffer_ = nullptr;
        capacity_ = 0;
    } else {
        buffer_ = static_cast<uint32_t*>(ptr);
    }
}

AArch64Encoder::~AArch64Encoder() {
    if (buffer_ && buffer_ != (uint32_t*)-1 && capacity_ > 0) {
        sys::raw_munmap(buffer_, capacity_ * sizeof(uint32_t));
        buffer_ = nullptr;
    }
}

void AArch64Encoder::reset() {
    size_ = 0;
}

void AArch64Encoder::emit32(uint32_t code) {
    if (!buffer_ || size_ >= capacity_) return;
    buffer_[size_++] = code;
}

void AArch64Encoder::add_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // ADD Xd, Xn, Xm: 0x8B000000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x8B000000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::sub_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // SUB Xd, Xn, Xm: 0xCB000000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0xCB000000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::mul_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // MUL Xd, Xn, Xm: 0x9B007C00 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x9B007C00UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::sdiv_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // SDIV Xd, Xn, Xm: 0x9AC00C00 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x9AC00C00UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::and_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // AND Xd, Xn, Xm: 0x8A000000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x8A000000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::orr_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // ORR Xd, Xn, Xm: 0xAA000000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0xAA000000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::eor_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // EOR Xd, Xn, Xm: 0xCA000000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0xCA000000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::lsl_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // LSLV Xd, Xn, Xm: 0x9AC02000 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x9AC02000UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::lsr_reg_reg(Arm64Reg rd, Arm64Reg rn, Arm64Reg rm) {
    // LSRV Xd, Xn, Xm: 0x9AC02400 | (Rm << 16) | (Rn << 5) | Rd
    uint32_t code = 0x9AC02400UL | (static_cast<uint32_t>(arm64_u8(rm)) << 16)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::mov_reg_imm64(Arm64Reg rd, uint64_t val) {
    uint8_t d = arm64_u8(rd);
    uint16_t c0 = static_cast<uint16_t>(val & 0xFFFF);
    uint16_t c1 = static_cast<uint16_t>((val >> 16) & 0xFFFF);
    uint16_t c2 = static_cast<uint16_t>((val >> 32) & 0xFFFF);
    uint16_t c3 = static_cast<uint16_t>((val >> 48) & 0xFFFF);

    // MOVZ Xd, #c0, LSL #0
    uint32_t code = 0xD2800000UL | (static_cast<uint32_t>(c0) << 5) | static_cast<uint32_t>(d);
    emit32(code);

    if (c1 != 0) {
        // MOVK Xd, #c1, LSL #16
        emit32(0xF2A00000UL | (static_cast<uint32_t>(c1) << 5) | static_cast<uint32_t>(d));
    }
    if (c2 != 0) {
        // MOVK Xd, #c2, LSL #32
        emit32(0xF2C00000UL | (static_cast<uint32_t>(c2) << 5) | static_cast<uint32_t>(d));
    }
    if (c3 != 0) {
        // MOVK Xd, #c3, LSL #48
        emit32(0xF2E00000UL | (static_cast<uint32_t>(c3) << 5) | static_cast<uint32_t>(d));
    }
}

void AArch64Encoder::mov_reg_reg(Arm64Reg rd, Arm64Reg rm) {
    // MOV Xd, Xm -> ORR Xd, XZR, Xm
    orr_reg_reg(rd, Arm64Reg::XZR, rm);
}

void AArch64Encoder::str_reg_mem(Arm64Reg rt, Arm64Reg rn, uint32_t offset_bytes) {
    // STR Xt, [Xn, #imm12]: 0xF9000000 | ((imm12 / 8) << 10) | (Rn << 5) | Rt
    uint32_t imm12 = (offset_bytes / 8) & 0xFFF;
    uint32_t code = 0xF9000000UL | (imm12 << 10)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rt));
    emit32(code);
}

void AArch64Encoder::ldr_reg_mem(Arm64Reg rt, Arm64Reg rn, uint32_t offset_bytes) {
    // LDR Xt, [Xn, #imm12]: 0xF9400000 | ((imm12 / 8) << 10) | (Rn << 5) | Rt
    uint32_t imm12 = (offset_bytes / 8) & 0xFFF;
    uint32_t code = 0xF9400000UL | (imm12 << 10)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rt));
    emit32(code);
}

void AArch64Encoder::push_fp_lr() {
    // STP X29, X30, [SP, #-16]!
    emit32(0xA9BF7BFDUL);
}

void AArch64Encoder::pop_fp_lr() {
    // LDP X29, X30, [SP], #16
    emit32(0xA8C17BFDUL);
}

void AArch64Encoder::mov_fp_sp() {
    // ADD X29, SP, #0 -> 0x910007FD
    emit32(0x910007FDUL);
}

void AArch64Encoder::sub_sp_imm32(uint32_t imm) {
    // SUB SP, SP, #imm12: 0xD10003FF | ((imm12 & 0xFFF) << 10)
    uint32_t imm12 = imm & 0xFFF;
    emit32(0xD10003FFUL | (imm12 << 10));
}

void AArch64Encoder::add_sp_imm32(uint32_t imm) {
    // ADD SP, SP, #imm12: 0x910003FF | ((imm12 & 0xFFF) << 10)
    uint32_t imm12 = imm & 0xFFF;
    emit32(0x910003FFUL | (imm12 << 10));
}

void AArch64Encoder::adrp(Arm64Reg rd, int32_t imm21_pages) {
    uint32_t imm21 = static_cast<uint32_t>(imm21_pages) & 0x1FFFFF;
    uint32_t immlo = imm21 & 3;
    uint32_t immhi = (imm21 >> 2) & 0x7FFFF;
    uint32_t code = 0x90000000UL | (immlo << 29) | (immhi << 5) | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::add_imm12(Arm64Reg rd, Arm64Reg rn, uint16_t imm12) {
    uint32_t code = 0x91000000UL | ((static_cast<uint32_t>(imm12) & 0xFFF) << 10)
                                 | (static_cast<uint32_t>(arm64_u8(rn)) << 5)
                                 | static_cast<uint32_t>(arm64_u8(rd));
    emit32(code);
}

void AArch64Encoder::ret() {
    // RET (X30)
    emit32(0xD65F03C0UL);
}

void AArch64Encoder::nop() {
    // NOP: 0xD503201F
    emit32(0xD503201FUL);
}

AArch64TargetBackend::AArch64TargetBackend() {}
AArch64TargetBackend::~AArch64TargetBackend() {}

void* AArch64TargetBackend::compile_function(frontend::Function* fn, frontend::Program* prog) {
    (void)prog;
    if (!fn) return nullptr;

    AArch64Encoder enc;
    enc.push_fp_lr();
    enc.mov_fp_sp();

    // Map AAPCS64 parameter registers X0..X7
    // Lower arithmetic instructions
    for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
        for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
            switch (insn->op) {
                case frontend::Opcode::ADD_I64:
                case frontend::Opcode::ADD_I32:
                    enc.add_reg_reg(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1);
                    break;
                case frontend::Opcode::SUB_I64:
                case frontend::Opcode::SUB_I32:
                    enc.sub_reg_reg(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1);
                    break;
                case frontend::Opcode::MUL_I32:
                    enc.mul_reg_reg(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1);
                    break;
                case frontend::Opcode::RETURN_VAL:
                    enc.pop_fp_lr();
                    enc.ret();
                    break;
                case frontend::Opcode::RETURN_VOID:
                    enc.pop_fp_lr();
                    enc.ret();
                    break;
                default:
                    break;
            }
        }
    }

    size_t sz = enc.code_size();
    if (sz == 0) return nullptr;

    void* exec_mem = CustomVMemProvider::alloc_rw(sz);
    if (!exec_mem) return nullptr;

    sys::freestanding_memcpy(exec_mem, enc.code_bytes(), sz);
    CustomVMemProvider::make_rx(exec_mem, sz);
    sys::clear_icache(exec_mem, sz);

    return exec_mem;
}

bool AArch64TargetBackend::compile_to_elf(frontend::Program* prog, const char* out_filename) {
    if (!prog || !out_filename) return false;

    ElfEmitter* elf = new ElfEmitter();
    elf->set_machine_arch(183); // EM_AARCH64
    SimpleByteBuffer* text_buf = new SimpleByteBuffer();

    for (frontend::Function* fn = prog->functions; fn != nullptr; fn = fn->next) {
        uint64_t func_offset = text_buf->size();

        AArch64Encoder enc;
        enc.push_fp_lr();
        enc.mov_fp_sp();

        for (frontend::BasicBlock* bb = fn->first_block; bb != nullptr; bb = bb->next) {
            for (frontend::Instruction* insn = bb->first_insn; insn != nullptr; insn = insn->next) {
                switch (insn->op) {
                    case frontend::Opcode::ADD_I64:
                    case frontend::Opcode::ADD_I32:
                        enc.add_reg_reg(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1);
                        break;
                    case frontend::Opcode::SUB_I64:
                    case frontend::Opcode::SUB_I32:
                        enc.sub_reg_reg(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1);
                        break;
                    case frontend::Opcode::MUL_I32:
                        enc.mul_reg_reg(Arm64Reg::X0, Arm64Reg::X0, Arm64Reg::X1);
                        break;
                    case frontend::Opcode::CONST_STRING: {
                        uint64_t ro_off = elf->append_rodata(insn->string_val, insn->string_len + 1);
                        uint64_t insn_off = func_offset + enc.code_size();

                        int32_t text_page = static_cast<int32_t>(insn_off >> 12);
                        int32_t rodata_page = static_cast<int32_t>((text_buf->size() + enc.code_size() + ro_off) >> 12);
                        int32_t page_disp = rodata_page - text_page;
                        uint16_t page_offset = static_cast<uint16_t>(ro_off & 0xFFF);

                        enc.adrp(Arm64Reg::X0, page_disp);
                        enc.add_imm12(Arm64Reg::X0, Arm64Reg::X0, page_offset);

                        elf->add_relocation(insn_off, 2 /* .rodata */, 275 /* R_AARCH64_ADR_PREL_PG_HI21 */, static_cast<int64_t>(ro_off));
                        elf->add_relocation(insn_off + 4, 2 /* .rodata */, 277 /* R_AARCH64_ADD_ABS_LO12_NC */, static_cast<int64_t>(ro_off));
                        break;
                    }
                    case frontend::Opcode::RETURN_VAL:
                    case frontend::Opcode::RETURN_VOID:
                        enc.pop_fp_lr();
                        enc.ret();
                        break;
                    default:
                        break;
                }
            }
        }

        uint64_t func_size = enc.code_size();
        text_buf->write(enc.code_bytes(), func_size);

        elf->add_symbol(fn->name ? fn->name : "anon_func", STB_GLOBAL, STT_FUNC, 1, func_offset, func_size);
    }

    // Write ELF object using EM_AARCH64 (183) machine type
    bool success = elf->write_elf_object(out_filename, text_buf->data(), text_buf->size());
    delete text_buf;
    delete elf;
    return success;
}

} // namespace backend
} // namespace ana
