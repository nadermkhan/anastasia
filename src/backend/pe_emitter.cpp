#include "pe_emitter.h"
#include "../sys/sys_raw.h"

namespace ana {
namespace backend {

PeEmitter::PeEmitter() {}
PeEmitter::~PeEmitter() {}

bool PeEmitter::write_pe_executable(const char* output_filename, const uint8_t* text_bytes, size_t text_size) {
    if (!output_filename || !text_bytes || text_size == 0) return false;

    SimpleByteBuffer pe_buf;

    // 1. DOS Header
    ImageDosHeader dos_hdr;
    sys::freestanding_memset(&dos_hdr, 0, sizeof(dos_hdr));
    dos_hdr.e_magic = 0x5A4D; // "MZ"
    dos_hdr.e_lfanew = sizeof(ImageDosHeader);
    pe_buf.write(&dos_hdr, sizeof(dos_hdr));

    // 2. NT Headers (PE Signature + File Header + Optional Header 64)
    ImageNtHeaders64 nt_hdrs;
    sys::freestanding_memset(&nt_hdrs, 0, sizeof(nt_hdrs));

    nt_hdrs.Signature = 0x00004550; // "PE\0\0"

    // File Header
    nt_hdrs.FileHeader.Machine = 0x8664; // AMD64
    nt_hdrs.FileHeader.NumberOfSections = 1; // .text section
    nt_hdrs.FileHeader.SizeOfOptionalHeader = sizeof(ImageOptionalHeader64);
    nt_hdrs.FileHeader.Characteristics = 0x0022; // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

    // Optional Header 64
    nt_hdrs.OptionalHeader.Magic = 0x020B; // PE32+
    nt_hdrs.OptionalHeader.MajorLinkerVersion = 14;
    nt_hdrs.OptionalHeader.MinorLinkerVersion = 0;
    nt_hdrs.OptionalHeader.SizeOfCode = static_cast<uint32_t>((text_size + 0x1FFF) & ~0x1FFF);
    nt_hdrs.OptionalHeader.AddressOfEntryPoint = 0x1000;
    nt_hdrs.OptionalHeader.BaseOfCode = 0x1000;
    nt_hdrs.OptionalHeader.ImageBase = 0x00400000;
    nt_hdrs.OptionalHeader.SectionAlignment = 0x1000;
    nt_hdrs.OptionalHeader.FileAlignment = 0x200;
    nt_hdrs.OptionalHeader.MajorOperatingSystemVersion = 6;
    nt_hdrs.OptionalHeader.MinorOperatingSystemVersion = 0;
    nt_hdrs.OptionalHeader.MajorSubsystemVersion = 6;
    nt_hdrs.OptionalHeader.MinorSubsystemVersion = 0;
    nt_hdrs.OptionalHeader.SizeOfImage = 0x1000 + nt_hdrs.OptionalHeader.SizeOfCode;
    nt_hdrs.OptionalHeader.SizeOfHeaders = 0x200;
    nt_hdrs.OptionalHeader.Subsystem = 3; // IMAGE_SUBSYSTEM_WINDOWS_CUI (Console)
    nt_hdrs.OptionalHeader.DllCharacteristics = 0x8160; // DYNAMIC_BASE | NX_COMPAT | TERMINAL_SERVER_AWARE
    nt_hdrs.OptionalHeader.SizeOfStackReserve = 0x100000;
    nt_hdrs.OptionalHeader.SizeOfStackCommit = 0x1000;
    nt_hdrs.OptionalHeader.SizeOfHeapReserve = 0x100000;
    nt_hdrs.OptionalHeader.SizeOfHeapCommit = 0x1000;
    nt_hdrs.OptionalHeader.NumberOfRvaAndSizes = 16;

    pe_buf.write(&nt_hdrs, sizeof(nt_hdrs));

    // 3. Section Header (.text)
    ImageSectionHeader text_sh;
    sys::freestanding_memset(&text_sh, 0, sizeof(text_sh));
    sys::freestanding_memcpy(text_sh.Name, ".text", 5);
    text_sh.VirtualSize = static_cast<uint32_t>(text_size);
    text_sh.VirtualAddress = 0x1000;
    text_sh.SizeOfRawData = static_cast<uint32_t>((text_size + 0x1FF) & ~0x1FF);
    text_sh.PointerToRawData = 0x200;
    text_sh.Characteristics = 0x60000020; // CODE | EXECUTE | READ

    pe_buf.write(&text_sh, sizeof(text_sh));

    // Pad headers to FileAlignment (0x200)
    while (pe_buf.size() < 0x200) {
        pe_buf.write_u8(0);
    }

    // 4. Append Machine Code (.text raw data)
    pe_buf.write(text_bytes, text_size);
    while (pe_buf.size() % 0x200 != 0) {
        pe_buf.write_u8(0);
    }

    // 5. Write to output binary file
    int fd = sys::raw_open(output_filename, 0100 | 01 /* O_CREAT | O_WRONLY */, 0755);
    if (fd < 0) return false;

    sys::raw_write(fd, pe_buf.data(), pe_buf.size());
    sys::raw_close(fd);

    return true;
}

} // namespace backend
} // namespace ana
