#include "xbe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// See xbe.h. Every diagnostic here goes to stdout, because a loader that cannot map its image has nothing
// else to say and no game to say it through.

static uint32_t ReadU32(const uint8_t *p, size_t offset) {
    uint32_t v;
    memcpy(&v, p + offset, sizeof(v));
    return v;
}

bool Xbe_Load(const char *path, XbeImage *image) {
    memset(image, 0, sizeof(*image));

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("[loader] cannot open %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0x184) {
        printf("[loader] %s is too small to be an XBE\n", path);
        fclose(f);
        return false;
    }

    uint8_t *data = (uint8_t *)malloc((size_t)size);
    if (data == NULL || fread(data, 1, (size_t)size, f) != (size_t)size) {
        printf("[loader] cannot read %s\n", path);
        free(data);
        fclose(f);
        return false;
    }
    fclose(f);

    if (ReadU32(data, 0) != XBE_MAGIC) {
        printf("[loader] %s is not an XBE (no XBEH magic)\n", path);
        free(data);
        return false;
    }

    image->file = data;
    image->fileSize = (size_t)size;
    image->baseAddress = ReadU32(data, 0x104);
    image->sizeOfImage = ReadU32(data, 0x10c);
    image->sizeOfHeaders = ReadU32(data, 0x108);
    image->certificateAddress = ReadU32(data, 0x118);
    image->sectionCount = ReadU32(data, 0x11c);
    image->tlsDirectoryAddress = ReadU32(data, 0x12c);

    uint32_t sectionHeadersAddress = ReadU32(data, 0x120);
    uint32_t sectionHeadersOffset = sectionHeadersAddress - image->baseAddress;
    if (sectionHeadersOffset + image->sectionCount * sizeof(XbeSectionHeader) > (uint32_t)size) {
        printf("[loader] section headers are outside the file\n");
        free(data);
        return false;
    }
    image->sections = (const XbeSectionHeader *)(data + sectionHeadersOffset);

    // The XOR keys tell retail and debug builds apart. Whichever one puts the entry point inside the image is
    // the right one - a wrong key gives an address nowhere near it.
    uint32_t rawEntry = ReadU32(data, 0x128);
    uint32_t rawThunk = ReadU32(data, 0x158);
    uint32_t imageEnd = image->baseAddress + image->sizeOfImage;

    uint32_t retailEntry = rawEntry ^ XBE_XOR_ENTRY_RETAIL;
    uint32_t debugEntry = rawEntry ^ XBE_XOR_ENTRY_DEBUG;
    if (retailEntry >= image->baseAddress && retailEntry < imageEnd) {
        image->entryPoint = retailEntry;
        image->kernelThunkAddress = rawThunk ^ XBE_XOR_THUNK_RETAIL;
        image->isDebugBuild = false;
    } else if (debugEntry >= image->baseAddress && debugEntry < imageEnd) {
        image->entryPoint = debugEntry;
        image->kernelThunkAddress = rawThunk ^ XBE_XOR_THUNK_DEBUG;
        image->isDebugBuild = true;
    } else {
        printf("[loader] neither XOR key gives an entry point inside the image "
               "(retail 0x%08x, debug 0x%08x, image 0x%08x..0x%08x)\n",
               retailEntry, debugEntry, image->baseAddress, imageEnd);
        free(data);
        return false;
    }

    printf("[loader] %s: base 0x%08x size 0x%08x, %u sections, %s build\n"
           "[loader]   entry 0x%08x, kernel thunks 0x%08x, TLS directory 0x%08x\n",
           path, image->baseAddress, image->sizeOfImage, image->sectionCount,
           image->isDebugBuild ? "debug" : "retail",
           image->entryPoint, image->kernelThunkAddress, image->tlsDirectoryAddress);
    return true;
}

// Provided by src/loader/reserve.cpp - the array whose whole purpose is to make this image span the XBE.
extern "C" const unsigned char *Loader_ReservationStart(void);
extern "C" unsigned long Loader_ReservationSize(void);

// Says what is at an address, for the failure paths below.
static void ReportRegion(uint32_t address) {
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    if (VirtualQuery((LPCVOID)(uintptr_t)address, &mbi, sizeof(mbi)) != sizeof(mbi)) {
        printf("[loader]   at 0x%08x: VirtualQuery failed (error %lu) - not even queryable\n",
               address, GetLastError());
        return;
    }
    const char *state = mbi.State == MEM_FREE ? "free"
                      : mbi.State == MEM_RESERVE ? "reserved" : "committed";
    const char *type = mbi.Type == MEM_IMAGE ? "image"
                     : mbi.Type == MEM_MAPPED ? "mapped"
                     : mbi.Type == MEM_PRIVATE ? "private" : "none";
    printf("[loader]   at 0x%08x: %s %s, allocation base 0x%08x, region size 0x%x\n",
           address, state, type, (unsigned)(uintptr_t)mbi.AllocationBase, (unsigned)mbi.RegionSize);
}

// True if every page of the range is committed. A mapped image is reported region by region - one per run of
// equal protection - so this walks the range rather than trusting one query to cover all of it.
static bool RangeIsCommitted(uint32_t base, uint32_t size) {
    uint32_t address = base;
    while (address < base + size) {
        MEMORY_BASIC_INFORMATION mbi;
        memset(&mbi, 0, sizeof(mbi));
        if (VirtualQuery((LPCVOID)(uintptr_t)address, &mbi, sizeof(mbi)) != sizeof(mbi))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        address = (uint32_t)((uintptr_t)mbi.BaseAddress + mbi.RegionSize);
    }
    return true;
}

// Gets hold of the XBE's address range and makes it writable.
//
// The range is normally this executable's own image: the loader is linked at the XBE's base with an array big
// enough to span it (src/loader/reserve.cpp), because nothing else can claim 0x00010000 at runtime. The
// kernel has already placed something there before the first instruction of the process runs, and reserving
// it from outside a suspended child fails for the same reason - it is not a race that starting earlier wins.
//
// One thing is worth checking rather than assuming. The XBE is copied over the start of this image, so the
// loader's own code has to sit above the XBE's end - which holds only because the reservation array comes
// first in .text. If a link order change ever moved it, the copy below would overwrite the code performing
// it, and the resulting crash would say nothing about the cause.
static bool ClaimAddressRange(const XbeImage *image) {
    uint32_t base = image->baseAddress;
    uint32_t size = image->sizeOfImage;
    uint32_t end = base + size;

    if (!RangeIsCommitted(base, size)) {
        // Not covered by this image. Taking it outright is the right answer for an XBE whose base or size the
        // loader was not linked for, and it is worth trying before giving up.
        if (VirtualAlloc((LPVOID)(uintptr_t)base, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) == NULL) {
            DWORD err = GetLastError();
            printf("[loader] 0x%08x..0x%08x is neither covered by this image nor allocatable (error %lu).\n",
                   base, end, err);
            printf("[loader]   the reservation array is 0x%08x..0x%08x\n",
                   (unsigned)(uintptr_t)Loader_ReservationStart(),
                   (unsigned)((uintptr_t)Loader_ReservationStart() + Loader_ReservationSize()));
            printf("[loader]   relink the loader with /BASE:0x%x and an array of at least 0x%x bytes\n",
                   base, size);
            ReportRegion(base);
            ReportRegion(end - 1);
            return false;
        }
        printf("[loader] allocated 0x%08x..0x%08x outright\n", base, end);
        return true;
    }

    uintptr_t loaderCode = (uintptr_t)(void *)&ClaimAddressRange;
    if (loaderCode >= base && loaderCode < end) {
        printf("[loader] the loader's own code is at 0x%08x, inside the XBE's range 0x%08x..0x%08x.\n",
               (unsigned)loaderCode, base, end);
        printf("[loader]   mapping would overwrite the loader itself. src/loader/reserve.cpp has to be the\n"
               "[loader]   first source of the loader's target so its array starts .text - see CMakeLists.txt.\n");
        return false;
    }

    // The image's own sections are execute-only or read-only as linked, and the XBE has to be written into
    // them. Per-section protections are applied afterwards, from the XBE's own section flags.
    DWORD previous = 0;
    if (!VirtualProtect((LPVOID)(uintptr_t)base, size, PAGE_EXECUTE_READWRITE, &previous)) {
        printf("[loader] could not make 0x%08x..0x%08x writable (error %lu)\n", base, end, GetLastError());
        ReportRegion(base);
        return false;
    }

    printf("[loader] 0x%08x..0x%08x is this image's own address space (loader code is at 0x%08x)\n",
           base, end, (unsigned)loaderCode);
    return true;
}

// Maps the XBE's headers while keeping this executable's PE headers working.
//
// Both have to live at 0x10000 and both are genuinely needed. The game reads its own XBE header constantly -
// XapiInitProcess takes the process heap's reserve and commit from 0x10134 and 0x10138, CreateThread takes
// the default stack size from 0x10130, XLaunchNewImageA and XGetLaunchInfo read the certificate through
// 0x10118, and the section table is reached through 0x10120. Windows, meanwhile, reaches for the main image's
// PE headers through the PEB whenever a DLL initialises: user32, d3d9 and rpcrt4 all do, and with "MZ" gone
// RtlImageNtHeader returns null and they fault - which surfaces only as LoadLibrary failing with error 1114.
//
// They fit together because each only needs a few bytes to be where it expects. A PE header needs "MZ" at
// offset 0 and e_lfanew at 0x3c; everything else it owns can be anywhere e_lfanew points. On the XBE side,
// offset 0 is the "XBEH" magic and 0x04..0x104 is the digital signature, neither of which anything reads once
// the file is loaded. So the XBE headers go down whole, "MZ" and e_lfanew are stamped back over bytes nothing
// looks at, and the PE headers proper are parked in the space past the XBE's own headers - 0x62c spare bytes
// in Nightfire's case, against the 0x198 they need.
static bool MapHeaders(const XbeImage *image) {
    uint32_t base = image->baseAddress;

    // Read this image's PE headers before they are overwritten. They are still at the base at this point,
    // because nothing has been copied yet.
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)(uintptr_t)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        printf("[loader] no PE header at 0x%08x to preserve - is the loader linked with /BASE:0x%x?\n",
               base, base);
        return false;
    }
    const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(uintptr_t)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        printf("[loader] the PE header at 0x%08x is malformed\n", base + dos->e_lfanew);
        return false;
    }

    uint32_t ntSize = sizeof(IMAGE_NT_HEADERS32) +
                      nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
    static uint8_t savedNt[0x800];
    if (ntSize > sizeof(savedNt)) {
        printf("[loader] this image's PE headers are %u bytes, more than the loader keeps room for\n", ntSize);
        return false;
    }
    memcpy(savedNt, nt, ntSize);

    // How much of the first page is mapped at all: everything below the first section.
    uint32_t headerBytes = image->sections[0].virtualAddress - base;
    if (headerBytes > image->fileSize)
        headerBytes = (uint32_t)image->fileSize;

    // Where the PE headers will live afterwards: past the XBE's own headers, 16-byte aligned.
    uint32_t peOffset = (image->sizeOfHeaders + 15) & ~15u;
    if (peOffset + ntSize > headerBytes) {
        printf("[loader] no room for the PE headers: the XBE's headers use 0x%x of 0x%x bytes and the PE\n"
               "[loader]   headers need 0x%x more. Both have to be in the first page.\n",
               image->sizeOfHeaders, headerBytes, ntSize);
        return false;
    }

    memcpy((void *)(uintptr_t)base, image->file, headerBytes);

    // Put the PE headers back: the two fields Windows needs at fixed offsets, then the headers themselves in
    // the space past the XBE's. Only those two fields are written over the XBE header, not the whole DOS
    // header, so as little of it as possible is disturbed.
    memcpy((uint8_t *)(uintptr_t)base + peOffset, savedNt, ntSize);
    IMAGE_DOS_HEADER *mapped = (IMAGE_DOS_HEADER *)(uintptr_t)base;
    mapped->e_magic = IMAGE_DOS_SIGNATURE;   // over "XB" of "XBEH", which nothing reads
    mapped->e_lfanew = (LONG)peOffset;       // over four bytes of the unread digital signature

    printf("[loader] mapped 0x%x bytes of XBE headers, PE headers moved to 0x%08x (0x%x bytes)\n",
           headerBytes, base + peOffset, ntSize);
    return true;
}

bool Xbe_Map(const XbeImage *image) {
    if (!ClaimAddressRange(image))
        return false;

    // The headers are mapped, and then this executable's PE headers are put back on top of them - both have
    // to be at 0x10000 at the same time. See MapHeaders.
    if (!MapHeaders(image))
        return false;

    for (uint32_t i = 0; i < image->sectionCount; i++) {
        const XbeSectionHeader *s = &image->sections[i];
        uint8_t *dst = (uint8_t *)(uintptr_t)s->virtualAddress;

        if (s->rawAddress + s->rawSize > image->fileSize) {
            printf("[loader] section %u raw data is outside the file\n", i);
            return false;
        }
        memcpy(dst, image->file + s->rawAddress, s->rawSize);
        // Anything the file does not cover is zero - this is where .bss-style data lives.
        if (s->virtualSize > s->rawSize)
            memset(dst + s->rawSize, 0, s->virtualSize - s->rawSize);
    }

    // Protections come second, so the copying above is never fighting a read-only page.
    //
    // Every section is left writable, whatever the XBE says. The whole decompilation works by patching game
    // code in place - actioninject.dll's Inject() writes jumps over hundreds of functions, and the kernel
    // thunk table it resolves next sits in a read-only section too. Honouring the XBE's read-only flags would
    // fault on the first patch, which is exactly what it did before this comment existed. The executable bit
    // is still honoured, so data sections stay non-executable and a stray jump into one still traps.
    for (uint32_t i = 0; i < image->sectionCount; i++) {
        const XbeSectionHeader *s = &image->sections[i];
        DWORD protect = (s->flags & XBE_SECTION_FLAG_EXECUTABLE) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;

        DWORD previous = 0;
        if (!VirtualProtect((LPVOID)(uintptr_t)s->virtualAddress, s->virtualSize, protect, &previous))
            printf("[loader] could not protect section %u at 0x%08x (error %lu)\n",
                   i, s->virtualAddress, GetLastError());
    }

    printf("[loader] mapped %u sections at 0x%08x\n", image->sectionCount, image->baseAddress);
    return true;
}

bool Xbe_ResolveKernelImports(const XbeImage *image, void *(*resolve)(unsigned ordinal)) {
    uint32_t *thunk = (uint32_t *)(uintptr_t)image->kernelThunkAddress;

    DWORD previous = 0;
    if (!VirtualProtect(thunk, 4096, PAGE_READWRITE, &previous)) {
        printf("[loader] kernel thunk table at 0x%08x is not writable (error %lu)\n",
               image->kernelThunkAddress, GetLastError());
        return false;
    }

    unsigned count = 0;
    for (; thunk[count] != 0; count++) {
        unsigned ordinal = thunk[count] & 0x7fffffffu;
        void *implementation = resolve(ordinal);
        if (implementation == NULL) {
            printf("[loader] no implementation for kernel ordinal %u\n", ordinal);
            return false;
        }
        thunk[count] = (uint32_t)(uintptr_t)implementation;
    }

    printf("[loader] resolved %u kernel imports\n", count);
    return true;
}
