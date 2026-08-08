#ifndef ANA_PE_EMITTER_H
#define ANA_PE_EMITTER_H

#include <cstdint>
#include <cstddef>
#include "elf_emitter.h"

namespace ana {
namespace backend {

#pragma pack(push, 1)
struct ImageDosHeader {
    uint16_t e_magic;    // "MZ" (0x5A4D)
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    int32_t  e_lfanew;   // Offset to PE Header (0x80)
};

struct ImageFileHeader {
    uint16_t Machine;              // 0x8664 (AMD64)
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct ImageDataDirectory {
    uint32_t VirtualAddress;
    uint32_t Size;
};

struct ImageOptionalHeader64 {
    uint16_t Magic;                // 0x020B (PE32+)
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    ImageDataDirectory DataDirectory[16];
};

struct ImageNtHeaders64 {
    uint32_t Signature;            // "PE\0\0" (0x00004550)
    ImageFileHeader FileHeader;
    ImageOptionalHeader64 OptionalHeader;
};

struct ImageSectionHeader {
    uint8_t  Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};
#pragma pack(pop)

class PeEmitter {
public:
    PeEmitter();
    ~PeEmitter();

    bool write_pe_executable(const char* output_filename, const uint8_t* text_bytes, size_t text_size, const uint8_t* rdata_bytes = nullptr, size_t rdata_size = 0);
};

} // namespace backend
} // namespace ana

#endif // ANA_PE_EMITTER_H
