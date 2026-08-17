#ifndef ELF_EMITTER_H
#define ELF_EMITTER_H

#include <cstdint>
#include <cstddef>

namespace ana {
namespace backend {

// Freestanding 32-bit & 64-bit ELF Constants & Standard Definitions
#define EI_NIDENT 16
#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

// ELF Program Header (32-bit) - 32 bytes
struct Elf32Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

// ELF File Header (32-bit) - 52 bytes
struct Elf32Ehdr {
    unsigned char e_ident[EI_NIDENT];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint32_t      e_entry;
    uint32_t      e_phoff;
    uint32_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
};

// Section Header (32-bit) - 40 bytes
struct Elf32Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
};

// Symbol Table Entry (32-bit) - 16 bytes
struct Elf32Sym {
    uint32_t      st_name;
    uint32_t      st_value;
    uint32_t      st_size;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t      st_shndx;
};

// Relocation Entry with Addend (32-bit) - 12 bytes
struct Elf32Rela {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
};

// ELF File Header (64-bit) - 64 bytes
struct Elf64Ehdr {
    unsigned char e_ident[EI_NIDENT];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;
    uint64_t      e_phoff;
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
};

// Section Header (64-bit) - 64 bytes
struct Elf64Shdr {
    uint32_t   sh_name;
    uint32_t   sh_type;
    uint64_t   sh_flags;
    uint64_t   sh_addr;
    uint64_t   sh_offset;
    uint64_t   sh_size;
    uint32_t   sh_link;
    uint32_t   sh_info;
    uint64_t   sh_addralign;
    uint64_t   sh_entsize;
};

// Symbol Table Entry (64-bit) - 24 bytes
struct Elf64Sym {
    uint32_t      st_name;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t      st_shndx;
    uint64_t      st_value;
    uint64_t      st_size;
};

// Relocation Entry with Addend (64-bit) - 24 bytes
struct Elf64Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};

// ELF Types
#define ET_REL 1
#define EM_X86_64 62
#define EM_ARM 40
#define EM_AARCH64 183
#define EM_RISCV 243
#define EM_XTENSA 94
#define EV_CURRENT 1

// Section Types
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8

// Section Flags
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4

// Symbol Info Helpers
#define ELF64_ST_INFO(b,t) (((b)<<4)+((t)&0xf))
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3

// Relocation Info Helpers
#define ELF32_R_INFO(s,t) (((uint32_t)(s)<<8)+((uint32_t)(t)&0xff))
#define ELF64_R_INFO(s,t) (((uint64_t)(s)<<32)+((uint64_t)(t)&0xffffffffUL))
#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_PLT32 4

class SimpleByteBuffer {
public:
    SimpleByteBuffer();
    ~SimpleByteBuffer();

    void write(const void* data, size_t size);
    void write_u8(uint8_t val);
    void write_u16(uint16_t val);
    void write_u32(uint32_t val);
    void write_u64(uint64_t val);
    uint32_t write_string(const char* str);

    const uint8_t* data() const { return buffer_; }
    size_t size() const { return size_; }

private:
    uint8_t* buffer_;
    size_t capacity_;
    size_t size_;
};

class ElfEmitter {
public:
    ElfEmitter();
    ~ElfEmitter();

    uint32_t add_symbol(const char* name, uint8_t binding, uint8_t type, uint16_t section_idx, uint64_t value, uint64_t size);
    uint32_t add_symbol(const char* name, uint64_t value, uint64_t size, bool is_global = true);
    void append_text(const void* bytes, size_t size);
    uint64_t append_rodata(const void* bytes, size_t size);
    void add_relocation(uint64_t offset, uint32_t symbol_idx, uint32_t type, int64_t addend);

    SimpleByteBuffer* rodata_section() { return rodata_section_; }
    SimpleByteBuffer* text_section() { return text_section_; }

    bool write_elf_object(const char* output_filename, const uint8_t* text_bytes, size_t text_size);
    uint8_t* finalize(size_t* out_size);
    void set_machine_arch(uint16_t arch) { machine_arch_ = arch; }

private:
    uint16_t machine_arch_{62}; // EM_X86_64 by default
    SimpleByteBuffer* text_section_;
    SimpleByteBuffer* rodata_section_;
    SimpleByteBuffer* strtab_section_;
    SimpleByteBuffer* shstrtab_section_;
    SimpleByteBuffer* symtab_section_;
    SimpleByteBuffer* rela_section_;
};

} // namespace backend
} // namespace ana

#endif // ELF_EMITTER_H
