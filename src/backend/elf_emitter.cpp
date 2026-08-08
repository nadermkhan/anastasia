#include "elf_emitter.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

static size_t cstr_len(const char* s) {
    if (!s) return 0;
    size_t l = 0;
    while (s[l] != '\0') ++l;
    return l;
}

SimpleByteBuffer::SimpleByteBuffer() : buffer_(nullptr), capacity_(65536), size_(0) {
    void* ptr = sys::raw_mmap(nullptr, capacity_, ANA_PROT_READ | ANA_PROT_WRITE, ANA_MAP_PRIVATE | ANA_MAP_ANONYMOUS, -1, 0);
    if (ptr == (void*)-1 || !ptr) {
        buffer_ = nullptr;
        capacity_ = 0;
    } else {
        buffer_ = static_cast<uint8_t*>(ptr);
    }
}

SimpleByteBuffer::~SimpleByteBuffer() {
    if (buffer_ && buffer_ != (uint8_t*)-1 && capacity_ > 0) {
        sys::raw_munmap(buffer_, capacity_);
        buffer_ = nullptr;
    }
}

void SimpleByteBuffer::write(const void* data, size_t size) {
    if (!data || size == 0 || !buffer_) return;
    if (size_ + size > capacity_) return;
    const uint8_t* src = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        buffer_[size_ + i] = src[i];
    }
    size_ += size;
}

void SimpleByteBuffer::write_u8(uint8_t val) {
    write(&val, sizeof(val));
}

void SimpleByteBuffer::write_u16(uint16_t val) {
    write(&val, sizeof(val));
}

void SimpleByteBuffer::write_u32(uint32_t val) {
    write(&val, sizeof(val));
}

void SimpleByteBuffer::write_u64(uint64_t val) {
    write(&val, sizeof(val));
}

uint32_t SimpleByteBuffer::write_string(const char* str) {
    if (!str) {
        uint32_t pos = static_cast<uint32_t>(size_);
        write_u8(0);
        return pos;
    }
    uint32_t pos = static_cast<uint32_t>(size_);
    size_t len = cstr_len(str);
    write(str, len);
    write_u8(0);
    return pos;
}

ElfEmitter::ElfEmitter() {
    text_section_ = new SimpleByteBuffer();
    rodata_section_ = new SimpleByteBuffer();
    strtab_section_ = new SimpleByteBuffer();
    shstrtab_section_ = new SimpleByteBuffer();
    symtab_section_ = new SimpleByteBuffer();
    rela_section_ = new SimpleByteBuffer();

    // Initial string table null byte
    if (strtab_section_) strtab_section_->write_u8(0);
    if (shstrtab_section_) shstrtab_section_->write_u8(0);

    // Initial symbol table STN_UNDEF (all zeros)
    Elf64Sym null_sym = {};
    if (symtab_section_) symtab_section_->write(&null_sym, sizeof(null_sym));

    // Section symbol 1: .text
    add_symbol(".text", STB_LOCAL, STT_SECTION, 1, 0, 0);
    // Section symbol 2: .rodata
    add_symbol(".rodata", STB_LOCAL, STT_SECTION, 2, 0, 0);
}

ElfEmitter::~ElfEmitter() {
    if (text_section_) delete text_section_;
    if (rodata_section_) delete rodata_section_;
    if (strtab_section_) delete strtab_section_;
    if (shstrtab_section_) delete shstrtab_section_;
    if (symtab_section_) delete symtab_section_;
    if (rela_section_) delete rela_section_;
}

uint32_t ElfEmitter::add_symbol(const char* name, uint8_t binding, uint8_t type, uint16_t section_idx, uint64_t value, uint64_t size) {
    if (!strtab_section_ || !symtab_section_) return 0;
    uint32_t name_off = strtab_section_->write_string(name);

    Elf64Sym sym;
    sym.st_name = name_off;
    sym.st_info = ELF64_ST_INFO(binding, type);
    sym.st_other = 0;
    sym.st_shndx = section_idx;
    sym.st_value = value;
    sym.st_size = size;

    uint32_t sym_index = static_cast<uint32_t>(symtab_section_->size() / sizeof(Elf64Sym));
    symtab_section_->write(&sym, sizeof(sym));
    return sym_index;
}

void ElfEmitter::add_relocation(uint64_t offset, uint32_t symbol_idx, uint32_t type, int64_t addend) {
    if (!rela_section_) return;
    Elf64Rela rela;
    rela.r_offset = offset;
    rela.r_info = ELF64_R_INFO(symbol_idx, type);
    rela.r_addend = addend;
    rela_section_->write(&rela, sizeof(rela));
}

bool ElfEmitter::write_elf_object(const char* output_filename, const uint8_t* text_bytes, size_t text_size) {
    if (!output_filename || !text_section_ || !shstrtab_section_ || !strtab_section_ || !symtab_section_ || !rela_section_) return false;

    // Fill text section
    text_section_->write(text_bytes, text_size);

    // Build shstrtab string entries
    uint32_t name_null = 0;
    uint32_t name_text = shstrtab_section_->write_string(".text");
    bool has_rodata = (rodata_section_ && rodata_section_->size() > 0);
    bool has_rela = (rela_section_ && rela_section_->size() > 0);
    uint32_t name_rodata = has_rodata ? shstrtab_section_->write_string(".rodata") : 0;
    uint32_t name_shstrtab = shstrtab_section_->write_string(".shstrtab");
    uint32_t name_strtab = shstrtab_section_->write_string(".strtab");
    uint32_t name_symtab = shstrtab_section_->write_string(".symtab");
    uint32_t name_rela_text = has_rela ? shstrtab_section_->write_string(".rela.text") : 0;

    uint16_t num_sections = 5 + (has_rodata ? 1 : 0) + (has_rela ? 1 : 0);

    size_t header_size = sizeof(Elf64Ehdr);
    size_t text_offset = header_size;
    size_t rodata_offset = text_offset + text_section_->size();
    size_t shstrtab_offset = rodata_offset + (has_rodata ? rodata_section_->size() : 0);
    size_t strtab_offset = shstrtab_offset + shstrtab_section_->size();
    size_t symtab_offset = strtab_offset + strtab_section_->size();
    size_t rela_offset = symtab_offset + symtab_section_->size();
    size_t shoff = ((has_rela ? rela_offset + rela_section_->size() : symtab_offset + symtab_section_->size()) + 7) & ~7UL;

    // Construct ELF Header
    alignas(16) Elf64Ehdr ehdr;
    sys::freestanding_memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = 0x7f;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2; // ELFCLASS64
    ehdr.e_ident[5] = 1; // ELFDATA2LSB
    ehdr.e_ident[6] = 1; // EV_CURRENT
    ehdr.e_ident[7] = 0; // ELFOSABI_SYSV

    ehdr.e_type = ET_REL;
    ehdr.e_machine = machine_arch_;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = 0;
    ehdr.e_phoff = 0;
    ehdr.e_shoff = shoff;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf64Ehdr);
    ehdr.e_phentsize = 0;
    ehdr.e_phnum = 0;
    ehdr.e_shentsize = sizeof(Elf64Shdr);
    ehdr.e_shnum = num_sections;
    ehdr.e_shstrndx = has_rodata ? 3 : 2; // .shstrtab section index

    // Section Headers
    Elf64Shdr* shdrs = static_cast<Elf64Shdr*>(malloc(sizeof(Elf64Shdr) * num_sections));
    if (!shdrs) return false;
    sys::freestanding_memset(shdrs, 0, sizeof(Elf64Shdr) * num_sections);

    // 0: NULL Section
    shdrs[0].sh_name = name_null;
    shdrs[0].sh_type = SHT_NULL;

    // 1: .text Section
    shdrs[1].sh_name = name_text;
    shdrs[1].sh_type = SHT_PROGBITS;
    shdrs[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[1].sh_addr = 0;
    shdrs[1].sh_offset = text_offset;
    shdrs[1].sh_size = text_section_->size();
    shdrs[1].sh_addralign = 16;

    uint16_t cur_sec = 2;
    if (has_rodata) {
        // 2: .rodata Section
        shdrs[cur_sec].sh_name = name_rodata;
        shdrs[cur_sec].sh_type = SHT_PROGBITS;
        shdrs[cur_sec].sh_flags = SHF_ALLOC;
        shdrs[cur_sec].sh_addr = 0;
        shdrs[cur_sec].sh_offset = rodata_offset;
        shdrs[cur_sec].sh_size = rodata_section_->size();
        shdrs[cur_sec].sh_addralign = 8;
        cur_sec++;
    }

    // .shstrtab Section
    shdrs[cur_sec].sh_name = name_shstrtab;
    shdrs[cur_sec].sh_type = SHT_STRTAB;
    shdrs[cur_sec].sh_offset = shstrtab_offset;
    shdrs[cur_sec].sh_size = shstrtab_section_->size();
    shdrs[cur_sec].sh_addralign = 1;
    cur_sec++;

    // .strtab Section
    shdrs[cur_sec].sh_name = name_strtab;
    shdrs[cur_sec].sh_type = SHT_STRTAB;
    shdrs[cur_sec].sh_offset = strtab_offset;
    shdrs[cur_sec].sh_size = strtab_section_->size();
    shdrs[cur_sec].sh_addralign = 1;
    uint16_t strtab_idx = cur_sec;
    cur_sec++;

    // .symtab Section
    shdrs[cur_sec].sh_name = name_symtab;
    shdrs[cur_sec].sh_type = SHT_SYMTAB;
    shdrs[cur_sec].sh_offset = symtab_offset;
    shdrs[cur_sec].sh_size = symtab_section_ ? symtab_section_->size() : 0;
    shdrs[cur_sec].sh_link = strtab_idx; // Link to .strtab
    shdrs[cur_sec].sh_info = 3; // Number of local symbols (.null, .text, .rodata)
    shdrs[cur_sec].sh_addralign = 8;
    shdrs[cur_sec].sh_entsize = sizeof(Elf64Sym);
    uint16_t symtab_idx = cur_sec;
    cur_sec++;

    // .rela.text Section (if any)
    if (has_rela) {
        shdrs[cur_sec].sh_name = name_rela_text;
        shdrs[cur_sec].sh_type = SHT_RELA;
        shdrs[cur_sec].sh_flags = 0;
        shdrs[cur_sec].sh_offset = rela_offset;
        shdrs[cur_sec].sh_size = rela_section_->size();
        shdrs[cur_sec].sh_link = symtab_idx; // Link to .symtab
        shdrs[cur_sec].sh_info = 1; // Applies to section 1 (.text)
        shdrs[cur_sec].sh_addralign = 8;
        shdrs[cur_sec].sh_entsize = sizeof(Elf64Rela);
    }

    // Open file using raw system call
    int fd = sys::raw_open(output_filename, 577 /* O_WRONLY|O_CREAT|O_TRUNC */, 0666);
    if (fd < 0) return false;

    // Stream write ELF parts sequentially to file
    sys::raw_write(fd, &ehdr, sizeof(ehdr));

    if (text_section_->size() > 0) {
        sys::raw_write(fd, text_section_->data(), text_section_->size());
    }

    if (shstrtab_section_->size() > 0) {
        sys::raw_write(fd, shstrtab_section_->data(), shstrtab_section_->size());
    }

    if (strtab_section_->size() > 0) {
        sys::raw_write(fd, strtab_section_->data(), strtab_section_->size());
    }

    if (symtab_section_->size() > 0) {
        sys::raw_write(fd, symtab_section_->data(), symtab_section_->size());
    }

    if (has_rela && rela_section_->size() > 0) {
        sys::raw_write(fd, rela_section_->data(), rela_section_->size());
    }

    // Pad file payload to shoff alignment
    size_t current_len = header_size + text_section_->size() + shstrtab_section_->size() + strtab_section_->size() + symtab_section_->size() + (has_rela ? rela_section_->size() : 0);
    uint8_t zero_byte = 0;
    while (current_len < shoff) {
        sys::raw_write(fd, &zero_byte, 1);
        current_len++;
    }

    // Write section headers
    sys::raw_write(fd, shdrs, sizeof(Elf64Shdr) * num_sections);
    free(shdrs);

    sys::raw_close(fd);
    return true;
}

uint32_t ElfEmitter::add_symbol(const char* name, uint64_t value, uint64_t size, bool is_global) {
    uint8_t binding = is_global ? STB_GLOBAL : STB_LOCAL;
    return add_symbol(name, binding, STT_FUNC, 1, value, size);
}

void ElfEmitter::append_text(const void* bytes, size_t size) {
    if (text_section_ && bytes && size > 0) {
        text_section_->write(bytes, size);
    }
}

uint64_t ElfEmitter::append_rodata(const void* bytes, size_t size) {
    if (!rodata_section_ || !bytes || size == 0) return 0;
    uint64_t offset = rodata_section_->size();
    rodata_section_->write(bytes, size);
    return offset;
}

uint8_t* ElfEmitter::finalize(size_t* out_size) {
    if (!text_section_ || !shstrtab_section_ || !strtab_section_ || !symtab_section_) return nullptr;

    uint32_t name_null = 0;
    uint32_t name_text = shstrtab_section_->write_string(".text");
    uint32_t name_rodata = shstrtab_section_->write_string(".rodata");
    uint32_t name_shstrtab = shstrtab_section_->write_string(".shstrtab");
    uint32_t name_strtab = shstrtab_section_->write_string(".strtab");
    uint32_t name_symtab = shstrtab_section_->write_string(".symtab");

    bool has_rodata = (rodata_section_ && rodata_section_->size() > 0);
    bool has_rela = (rela_section_ && rela_section_->size() > 0);
    uint32_t name_rela_text = has_rela ? shstrtab_section_->write_string(".rela.text") : 0;
    (void)name_null; (void)name_text; (void)name_rodata; (void)name_shstrtab; (void)name_strtab; (void)name_symtab; (void)name_rela_text;

    uint16_t num_sections = 5 + (has_rodata ? 1 : 0) + (has_rela ? 1 : 0);
    size_t header_size = sizeof(Elf64Ehdr);
    size_t text_offset = header_size;
    size_t rodata_offset = text_offset + text_section_->size();
    size_t shstrtab_offset = rodata_offset + (has_rodata ? rodata_section_->size() : 0);
    size_t strtab_offset = shstrtab_offset + shstrtab_section_->size();
    size_t symtab_offset = strtab_offset + strtab_section_->size();
    size_t rela_offset = symtab_offset + symtab_section_->size();
    size_t shoff = ((has_rela ? rela_offset + rela_section_->size() : symtab_offset + symtab_section_->size()) + 7) & ~7UL;

    size_t total_elf_size = shoff + sizeof(Elf64Shdr) * num_sections;
    uint8_t* buffer = static_cast<uint8_t*>(malloc(total_elf_size));
    if (!buffer) return nullptr;
    sys::freestanding_memset(buffer, 0, total_elf_size);

    alignas(16) Elf64Ehdr ehdr;
    sys::freestanding_memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = 0x7f; ehdr.e_ident[1] = 'E'; ehdr.e_ident[2] = 'L'; ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = 2; ehdr.e_ident[5] = 1; ehdr.e_ident[6] = 1; ehdr.e_ident[7] = 0;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = machine_arch_;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_shoff = shoff;
    ehdr.e_ehsize = sizeof(Elf64Ehdr);
    ehdr.e_shentsize = sizeof(Elf64Shdr);
    ehdr.e_shnum = num_sections;
    ehdr.e_shstrndx = 2;

    sys::freestanding_memcpy(buffer, &ehdr, sizeof(ehdr));
    size_t cur = sizeof(ehdr);

    if (text_section_->size() > 0) {
        sys::freestanding_memcpy(buffer + cur, text_section_->data(), text_section_->size());
        cur += text_section_->size();
    }
    if (shstrtab_section_->size() > 0) {
        sys::freestanding_memcpy(buffer + cur, shstrtab_section_->data(), shstrtab_section_->size());
        cur += shstrtab_section_->size();
    }
    if (strtab_section_->size() > 0) {
        sys::freestanding_memcpy(buffer + cur, strtab_section_->data(), strtab_section_->size());
        cur += strtab_section_->size();
    }
    if (symtab_section_->size() > 0) {
        sys::freestanding_memcpy(buffer + cur, symtab_section_->data(), symtab_section_->size());
        cur += symtab_section_->size();
    }

    Elf64Shdr* shdrs = reinterpret_cast<Elf64Shdr*>(buffer + shoff);
    shdrs[0].sh_name = name_null; shdrs[0].sh_type = SHT_NULL;
    shdrs[1].sh_name = name_text; shdrs[1].sh_type = SHT_PROGBITS; shdrs[1].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[1].sh_offset = text_offset; shdrs[1].sh_size = text_section_->size(); shdrs[1].sh_addralign = 16;
    shdrs[2].sh_name = name_shstrtab; shdrs[2].sh_type = SHT_STRTAB; shdrs[2].sh_offset = shstrtab_offset; shdrs[2].sh_size = shstrtab_section_->size(); shdrs[2].sh_addralign = 1;
    shdrs[3].sh_name = name_strtab; shdrs[3].sh_type = SHT_STRTAB; shdrs[3].sh_offset = strtab_offset; shdrs[3].sh_size = strtab_section_->size(); shdrs[3].sh_addralign = 1;
    shdrs[4].sh_name = name_symtab; shdrs[4].sh_type = SHT_SYMTAB; shdrs[4].sh_offset = symtab_offset; shdrs[4].sh_size = symtab_section_->size(); shdrs[4].sh_link = 3; shdrs[4].sh_info = 1; shdrs[4].sh_addralign = 8; shdrs[4].sh_entsize = sizeof(Elf64Sym);

    if (out_size) *out_size = total_elf_size;
    return buffer;
}

} // namespace backend
} // namespace ana
