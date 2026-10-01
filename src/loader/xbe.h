#ifndef LOADER_XBE_H_
#define LOADER_XBE_H_

#include <windows.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// Just enough of the XBE format to map one and find its entry point, kernel imports and TLS.
//
// An XBE is a PE with a different header. The fields below are at fixed offsets from the start of the file, and
// two of them - the entry point and the kernel thunk address - are stored XOR'd with a key that differs between
// retail and debug builds. Trying both keys and keeping whichever lands inside the image doubles as a check
// that the right field is being read.
// ---------------------------------------------------------------------------------------------------------------

#define XBE_MAGIC 0x48454258u   // 'XBEH'

#define XBE_XOR_ENTRY_RETAIL  0xA8FC57ABu
#define XBE_XOR_ENTRY_DEBUG   0x94859D4Bu
#define XBE_XOR_THUNK_RETAIL  0x5B6D40B6u
#define XBE_XOR_THUNK_DEBUG   0xEFB1F152u

#pragma pack(push, 1)

struct XbeSectionHeader {
    uint32_t flags;             // bit 0 writable, bit 2 executable
    uint32_t virtualAddress;
    uint32_t virtualSize;
    uint32_t rawAddress;
    uint32_t rawSize;
    uint32_t sectionNameAddress;
    uint32_t sectionNameRefCount;
    uint32_t headSharedPageRefCount;
    uint32_t tailSharedPageRefCount;
    uint8_t  digest[20];
};

// The XBE's TLS directory, pointed at by the header. Laid out like the PE one.
struct XbeTlsDirectory {
    uint32_t dataStartAddress;
    uint32_t dataEndAddress;
    uint32_t tlsIndexAddress;
    uint32_t tlsCallbackAddress;
    uint32_t sizeOfZeroFill;
    uint32_t characteristics;
};

#pragma pack(pop)

#define XBE_SECTION_FLAG_WRITABLE   0x00000001u
#define XBE_SECTION_FLAG_EXECUTABLE 0x00000004u

struct XbeImage {
    uint8_t  *file;             // the whole file, still owned by us
    size_t    fileSize;

    uint32_t  baseAddress;
    uint32_t  sizeOfImage;
    uint32_t  sizeOfHeaders;    // how much of the first page the XBE's own headers use
    uint32_t  entryPoint;       // already de-XOR'd
    uint32_t  kernelThunkAddress;
    uint32_t  tlsDirectoryAddress;
    uint32_t  certificateAddress;

    const XbeSectionHeader *sections;
    uint32_t  sectionCount;
    bool      isDebugBuild;
};

// Reads the file and fills in image. Returns false and explains on stderr if it is not a usable XBE.
bool Xbe_Load(const char *path, XbeImage *image);

// Reserves the image's address range and copies every section to its virtual address, zeroing the parts that
// have no file data (the .bss equivalents) and applying each section's protection. Must be called before
// anything reaches into the image.
bool Xbe_Map(const XbeImage *image);

// Replaces each entry of the kernel thunk table with the address of an implementation. Each entry starts as
// an ordinal with the top bit set; resolve() is asked for each one and may return NULL, in which case the
// loader substitutes a stub that reports the call.
bool Xbe_ResolveKernelImports(const XbeImage *image, void *(*resolve)(unsigned ordinal));

#endif // LOADER_XBE_H_
