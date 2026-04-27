#ifndef PE_H
#define PE_H

#include <stdint.h>

#define MZ_MAGIC 0x5A4D
#define PE_MAGIC 0x00004550

typedef struct {
  uint16_t magic; // MZ_MAGIC
  uint16_t cblp;
  uint16_t cp;
  uint16_t crlc;
  uint16_t cparhdr;
  uint16_t minalloc;
  uint16_t maxalloc;
  uint16_t ss;
  uint16_t sp;
  uint16_t csum;
  uint16_t ip;
  uint16_t cs;
  uint16_t lfarlc;
  uint16_t ovno;
  uint16_t res[4];
  uint16_t oemid;
  uint16_t oeminfo;
  uint16_t res2[10];
  uint32_t lfanew; // Offset to PE Header
} IMAGE_DOS_HEADER;

typedef struct {
  uint16_t Machine;
  uint16_t NumberOfSections;
  uint32_t TimeDateStamp;
  uint32_t PointerToSymbolTable;
  uint32_t NumberOfSymbols;
  uint16_t SizeOfOptionalHeader;
  uint16_t Characteristics;
} IMAGE_FILE_HEADER;

typedef struct {
  uint32_t VirtualAddress;
  uint32_t Size;
} IMAGE_DATA_DIRECTORY;

typedef struct {
  uint16_t Magic;
  uint8_t MajorLinkerVersion;
  uint8_t MinorLinkerVersion;
  uint32_t SizeOfCode;
  uint32_t SizeOfInitializedData;
  uint32_t SizeOfUninitializedData;
  uint32_t AddressOfEntryPoint;
  uint32_t BaseOfCode;
  uint32_t BaseOfData;
  uint32_t ImageBase;
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
  uint32_t SizeOfStackReserve;
  uint32_t SizeOfStackCommit;
  uint32_t SizeOfHeapReserve;
  uint32_t SizeOfHeapCommit;
  uint32_t LoaderFlags;
  uint32_t NumberOfRvaAndSizes;
  IMAGE_DATA_DIRECTORY DataDirectory[16];
} IMAGE_OPTIONAL_HEADER32;

typedef struct {
  uint32_t Signature; // PE_MAGIC
  IMAGE_FILE_HEADER FileHeader;
  IMAGE_OPTIONAL_HEADER32 OptionalHeader;
} IMAGE_NT_HEADERS32;

typedef struct {
  char Name[8];
  uint32_t VirtualSize;
  uint32_t VirtualAddress;
  uint32_t SizeOfRawData;
  uint32_t PointerToRawData;
  uint32_t PointerToRelocations;
  uint32_t PointerToLinenumbers;
  uint16_t NumberOfRelocations;
  uint16_t NumberOfLinenumbers;
  uint32_t Characteristics;
} IMAGE_SECTION_HEADER;

typedef struct {
  uint32_t OriginalFirstThunk;
  uint32_t TimeDateStamp;
  uint32_t ForwarderChain;
  uint32_t Name;
  uint32_t FirstThunk;
} IMAGE_IMPORT_DESCRIPTOR;

#endif
