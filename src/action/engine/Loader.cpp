#include "../actionhelpers.h"

#include "Loader.h"
#include "parsemap.h"
#include "celglist.h"
#include "../util/hashtable.h"
#include "../ui/MenuManager.h"
#include "Script.h"
#include "psiFile.h"
#include "../memory.h"

#include <stdio.h>

// XBE_GLOBAL(0x00274c98, 0x4)
uint32_t MemType;

// ---- The loader's globals. LoaderLoad was the last original touching any of them (the byte scan of the XBE
// ---- finds each address only in LoaderLoad, LoaderProcess and psiFileLoad, and the last two are ours).

uint *dirFileBuf;
// The end of the whole batch buffer. Written, never read by anything.
// XBE_GLOBAL(0x00279170, 0x4)
static uchar *DirFileBufEnd;
// The size of the current file (the last entry read from the directory). psiFileLoad (ours) reads it too.
// XBE_GLOBAL(0x00279174, 0x4)
uint DirFileLen;
// The end of the current file. Written, never read by anything.
// XBE_GLOBAL(0x00279178, 0x4)
static uchar *DirFileEnd;
// The first batch buffer the loader ever read into. Set once (never cleared, not even at a level change),
// never read by anything.
// XBE_GLOBAL(0x0027917c, 0x4)
static uint *DirFileFirstBuf;
// The type of the current file: LoaderProcess switches on it.
// XBE_GLOBAL(0x00279184, 0x4)
static uint DirFileType;
// The name of the current file, upper-cased, as stored in the directory. It ends in the file's hashcode in
// hex. Nothing but the directory parser touches it; the parser has no length check (the original's buffer is
// the 0x40 bytes up to DirFileHash too).
// XBE_GLOBAL(0x00279188, 0x40)
static char DirFileName[0x40];
// The hashcode of the current file, parsed out of the end of its name.
// XBE_GLOBAL(0x002791c8, 0x4)
static uint DirFileHashValue;
#define DirFileHash ((HASHCODE)DirFileHashValue)

// Still the game's: IsLevelLoaded, LoadableLoad, LoadableReload and ResetMap_Load (none of them ours) use it.
#define bLevelLoaded (*(uchar*)0x00279251)

// The header at the start of an archive: 0x20 bytes (the size the loader reads), of which it uses two fields.
#pragma pack(push, 1)
typedef struct {
    uint dirSize;           // 0x00 bytes of directory entries that follow the header
    uint filesRemaining;    // 0x04 entries not yet loaded, counted down by one batch at a time
    undefined4 unknown[6];  // 0x08 not read by the loader
} LoaderDirHeader;
#pragma pack(pop)
static_assert(sizeof(LoaderDirHeader) == 0x20, "Bad size for LoaderDirHeader");
static_assert(offsetof(LoaderDirHeader, filesRemaining) == 0x04, "Bad offset of LoaderDirHeader.filesRemaining");

// LoaderLoad is called over and over (ResetMap_Load loops until it returns false); each call does one step.
enum {
    LOADER_OPEN = 0,            // open the archive
    LOADER_READ_DIRECTORY = 1,  // read its header and directory
    LOADER_SIZE_BATCH = 2,      // add up the sizes of the next batch of files
    LOADER_READ_BATCH = 3,      // read the batch's data
    LOADER_PROCESS_FILES = 4,   // hand the files to LoaderProcess, one per call (more if it asks to continue)
    LOADER_CLOSE = 5,           // close the archive; LoaderLoad returns false, which ends the loop
};

// XBE_GLOBAL(0x0027927c, 0x4)
static uint LoaderState;
// Zeroed when an archive is opened; nothing reads it.
// XBE_GLOBAL(0x00279278, 0x4)
static uint LoaderUnused_279278;
// XBE_GLOBAL(0x00279274, 0x4)
static LoaderDirHeader *pDirHeader;
// XBE_GLOBAL(0x00279270, 0x4)
static uchar *pDirData;
// The directory entry of the next file to process
// XBE_GLOBAL(0x0027926c, 0x4)
static uchar *pDirDataCurFile;
// Files of the current batch not yet processed
// XBE_GLOBAL(0x00279268, 0x4)
static uint FilesToLoadCount;
// The bytes of data in the current batch
// XBE_GLOBAL(0x00279264, 0x4)
static uint BatchByteSize;
// Set: take the header, directory and data from Euro_Decomp_Buf (an already decompressed archive in memory)
// instead of reading them from the file. Nothing in the Xbox build ever sets it, so that path never runs; it is
// kept because the original has it.
// XBE_GLOBAL(0x00279252, 0x1)
static uchar LoadFromDecompBuf;
// XBE_GLOBAL(0x00279254, 0x4)
static uchar *Euro_Decomp_Buf;
// The most files in one batch. 0xffffffff in the XBE, and nothing writes it: every archive is one batch.
// XBE_GLOBAL(0x00181bb4, 0x4)
static uint LoaderFilesPerBatch = 0xffffffff;

// The path the archive names are relative to: the game's string at 0x0015d308 is empty.
#define LOADER_ARCHIVE_PREFIX ""

// Bits 20-23 of an archive's hashcode are replaced by variant + 7 when a variant is asked for
#define LOADER_VARIANT_MASK 0xff0fffff
#define LOADER_VARIANT_SHIFT 20
#define LOADER_VARIANT_BASE 7

// MALLOC_LOW_END (memory.cpp): the batch goes at the low end of a free block
#define LOADER_BATCH_MALLOC_METHOD 2

// Reads the file's current position and advances it; there is no allocation or copy, since psiFileOpen has
// already loaded the whole archive. The original passes three more arguments (0x20 or the size, 0x4004 or
// 0x4104, 0x80, and 0 or LoaderLoad's fourth argument - allocation flags, from the look of them), which this
// function never reads.
// AUTOGEN
int __cdecl maybePsiFileRead(int param_1);

// AUTOGEN
undefined4 __stdcall psiFileClose(void);

// The game's CRT toupper (locale-aware, through _pctype). Called rather than our CRT's so the names come out
// byte for byte as the original's, even for bytes above 0x7f (passed sign-extended, as the original does).
// AUTOGEN
int __cdecl maybeToUpper(int param_1);

// Reads one directory entry at *cursor and advances *cursor past it: a 32-bit little-endian size, a type byte
// and a NUL-terminated name whose last component is the file's hashcode in hex.
//
// FUN_000be630. Not injected: it takes cursor in EAX, and LoaderLoad (ours now) is its only caller, so the
// original is never reached.
static void LoaderReadDirEntry(uint *len, uint *type, uint *hash, char *name, uchar **cursor) {
    uchar *p = *cursor;

    // Byte by byte, as the original does: entries are not aligned
    *len = (uint)p[0] | ((uint)p[1] << 8) | ((uint)p[2] << 16) | ((uint)p[3] << 24);
    p += 4;
    *type = *p++;

    int i = 0;
    name[0] = (char)maybeToUpper((signed char)*p++);
    while (name[i] != '\0') {
        i++;
        name[i] = (char)maybeToUpper((signed char)*p++);
    }
    *cursor = p;

    // Back to the start of the last path component
    while (i != 0 && name[i - 1] != '\\' && name[i - 1] != '/')
        i--;

    // Up to eight hex digits (upper case, since the name is upper-cased). The test is the original's, which
    // also takes ':' to '@' as the digits 3 to 9.
    uint value = 0;
    const uchar *digits = (const uchar *)&name[i];
    for (int n = 0; n < 8; n++) {
        uint digit = (uint)digits[n] - '0';
        if (digit > 9)
            digit -= 'A' - '0' - 10;
        if (digit > 0xf)
            break;
        value = (value << 4) | digit;
    }
    *hash = value;
}

// Loads the archive <fileHash>.bin, one step per call: returns true while there is more to do, false once the
// archive is closed (or could not be opened). mode must be 1. variant, when non-zero, picks another archive
// by replacing bits 20-23 of the hashcode. The third argument is not used.
//
// AUTOINJECT
bool LoaderLoad(int mode, uint fileHash, undefined4 unused, int variant) {

    // Cleared on every call, even one with the wrong mode
    bLevelLoaded = 0;

    if (mode != 1)
        return false;

    switch (LoaderState) {
    case LOADER_OPEN: {
        uint hash = fileHash & LOADER_VARIANT_MASK;
        if (variant != 0)
            hash |= (uint)(variant + LOADER_VARIANT_BASE) << LOADER_VARIANT_SHIFT; // may spill past bit 23, as in the original

        char fileName[256];
        snprintf(fileName, sizeof(fileName), "%s%8.8x.bin", LOADER_ARCHIVE_PREFIX, hash);

        psiFileSetSingleFileMode(1);
        LoaderUnused_279278 = 0;
        // The original tests only the low byte of the result (ours returns an int whose low byte is always 1)
        if ((uchar)psiFileOpen(fileName)) {
            pDirHeader = NULL;
            pDirData = NULL;
            LoaderState = LOADER_READ_DIRECTORY;
            return true;
        }
        // The state stays LOADER_OPEN: the next call tries again
        return false;
    }

    case LOADER_READ_DIRECTORY:
        if (!LoadFromDecompBuf) {
            pDirHeader = (LoaderDirHeader *)maybePsiFileRead(sizeof(LoaderDirHeader));
        } else {
            pDirHeader = (LoaderDirHeader *)Euro_Decomp_Buf;
            Euro_Decomp_Buf += sizeof(LoaderDirHeader);
        }
        // With no files, the directory is not read and pDirData keeps its value (NULL, from LOADER_OPEN)
        if (pDirHeader->filesRemaining != 0) {
            if (!LoadFromDecompBuf) {
                pDirData = (uchar *)maybePsiFileRead(pDirHeader->dirSize);
            } else {
                pDirData = Euro_Decomp_Buf;
                Euro_Decomp_Buf += pDirHeader->dirSize;
            }
        }
        pDirDataCurFile = pDirData;
        LoaderState = LOADER_SIZE_BATCH;
        return true;

    case LOADER_SIZE_BATCH: {
        uint remaining = pDirHeader->filesRemaining;
        FilesToLoadCount = LoaderFilesPerBatch;
        if (remaining < FilesToLoadCount)
            FilesToLoadCount = remaining;
        pDirHeader->filesRemaining = remaining - FilesToLoadCount;

        // A dry run over the batch's entries to add up their sizes. It leaves pDirDataCurFile where it was, but
        // the DirFile* globals hold the batch's last entry afterwards, as in the original.
        BatchByteSize = 0;
        uchar *cursor = pDirDataCurFile;
        for (uint i = 0; i < FilesToLoadCount; i++) {
            LoaderReadDirEntry(&DirFileLen, &DirFileType, &DirFileHashValue, DirFileName, &cursor);
            BatchByteSize += DirFileLen;
        }
        LoaderState = LOADER_READ_BATCH;
        return true;
    }

    case LOADER_READ_BATCH:
        if (FilesToLoadCount != 0) {
            if (!LoadFromDecompBuf) {
                uint previousMethod = Mem_SetMallocMethod(LOADER_BATCH_MALLOC_METHOD);
                dirFileBuf = (uint *)maybePsiFileRead(BatchByteSize);
                Mem_SetMallocMethod(previousMethod);
            } else {
                dirFileBuf = (uint *)Euro_Decomp_Buf;
                Euro_Decomp_Buf += BatchByteSize;
            }
            DirFileBufEnd = (uchar *)dirFileBuf + BatchByteSize;
            if (DirFileFirstBuf == NULL)
                DirFileFirstBuf = dirFileBuf;
        }
        LoaderState = LOADER_PROCESS_FILES;
        return true;

    case LOADER_PROCESS_FILES: {
        if (FilesToLoadCount == 0) {
            // Batch done: another batch if the archive has more files, else close it
            LoaderState = (pDirHeader->filesRemaining != 0) ? LOADER_SIZE_BATCH : LOADER_CLOSE;
            return true;
        }

        uchar *cursor = pDirDataCurFile;
        LoaderReadDirEntry(&DirFileLen, &DirFileType, &DirFileHashValue, DirFileName, &cursor);
        DirFileEnd = (uchar *)dirFileBuf + DirFileLen;

        // False means the file is not finished with (parsemap works through a map over several calls): the
        // same entry is read again next call, and dirFileBuf stays on this file.
        if (!LoaderProcess())
            return true;

        pDirDataCurFile = cursor;
        FilesToLoadCount--;
        dirFileBuf = (uint *)((uchar *)dirFileBuf + DirFileLen);
        return true;
    }

    case LOADER_CLOSE:
        psiFileClose();
        psiFileSetSingleFileMode(0);
        LoaderState = LOADER_OPEN;
        return false;

    default:
        return true;
    }
}

#define LoadableIndex U8_AT(0x00279180)


#pragma pack(push, 1)
typedef struct {
  HASHCODE hashcode;
  char loadableIdx;
  char _pad[3];
} LoadableFile;
#pragma pack(pop)

// XBE_GLOBAL(0x002791d0, 0x80)
static LoadableFile LoadableFiles[16];

// AUTOINJECT
bool LoaderProcess(void) {

    MemType = 0;

    switch(DirFileType) {
        case 1:
          AnimPostLoadInit();
          if (parsemap_parsemap(DirFileHash,'\x01')) {
            return false;
          }
          break;
        case 3:
        case 4:
        case 5:
          AnimLoadFile(DirFileHash,0);
          return true;
        case 6:
          AnimSkeletonProcess((char *)dirFileBuf);
          return true;
        case 7:
          Script_Load(DirFileHash,&CONST_ZERO_VECTOR,&CONST_ZERO_VECTOR,dirFileBuf,NULL, NULL, NULL);
          return true;
        case 8:
          MenuManager_Load(DirFileHash,dirFileBuf);
          return true;
        case 0xb:
          MemType = 1;
        case 0:
        case 2:
          if (parsemap_parsemap(DirFileHash,'\0')) { // Returns fasle on completion, true if there's more to parse
            return false;
          }
          break;
        case 0xc:
          if (LoadableIndex < ARRAY_SIZE(LoadableFiles)) {
            LoadableFiles[LoadableIndex].hashcode = (HASHCODE) *dirFileBuf;
            LoadableFiles[LoadableIndex].loadableIdx = (char)dirFileBuf[1];
            LoadableIndex++;
          }
        }
        return true;

}

// AUTOINJECT
bool isLoadable(HASHCODE param_1) {
  for (int i = 0; i < LoadableIndex; i++) {
    if (LoadableFiles[i].hashcode == param_1)
      return LoadableFiles[i].loadableIdx != 0;
  }
  return false;
}

// AUTOGEN
bool LoadableReload(HASHCODE hashcode);