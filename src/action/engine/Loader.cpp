#include "../actionhelpers.h"

#include "Loader.h"
#include "parsemap.h"
#include "celglist.h"
#include "../util/hashtable.h"
#include "../ui/MenuManager.h"
#include "Script.h"
#include "psiFile.h"
#include "CRuntime.h"
#include "../memory.h"

#include <stdio.h>

// XBE_GLOBAL(0x00274c98, 0x4)
uint32_t MemType;

// ---- The loader's globals. LoaderLoad was the last original touching any of them (the byte scan of the XBE
// ---- finds each address only in LoaderLoad, LoaderProcess and psiFileLoad, and the last two are ours).

// Where the next file starts in the batch buffer. psiFileLoad reads it too.
// XBE_GLOBAL(0x00279168, 0x4)
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
static uint DirFileType;     // DirFileType_tag
// The name of the current file, upper-cased, as stored in the directory. It ends in the file's hashcode in
// hex. Nothing but the directory parser touches it; the parser has no length check (the original's buffer is
// the 0x40 bytes up to DirFileHash too).
// XBE_GLOBAL(0x00279188, 0x40)
static char DirFileName[0x40];
// The hashcode of the current file, parsed out of the end of its name.
// XBE_GLOBAL(0x002791c8, 0x4)
static uint DirFileHashValue;
#define DirFileHash ((HASHCODE)DirFileHashValue)

// LoadableLoad, LoadableReload and ResetMap_Load (the game's still) use it too.
extern uchar bLevelLoaded;

// The flag's byte as it is
// AUTOINJECT
uchar IsLevelLoaded(void) {
    return bLevelLoaded;
}

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

// The bytes the header takes in the archive: on the PS2's disc a whole sector, the directory after it rounded to
// sectors too (PS2 LoaderLoad 0x1d51a0 reads 0x800; docs/ps2-data.md)
#if NIGHTFIRE_DATA_PS2
#define LOADER_HEADER_BYTES 0x800
#else
#define LOADER_HEADER_BYTES sizeof(LoaderDirHeader)
#endif

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

// A directory entry's type byte: the loader LoaderProcess hands the file to
enum DirFileType_tag {
    DIRFILE_MAP = 0,                // parsemap_parsemap
    DIRFILE_MAP_SECOND_PASS = 1,    // parsemap_parsemap's second pass, after AnimPostLoadInit
    DIRFILE_MAP_2 = 2,
    DIRFILE_ANIM_3 = 3,             // AnimLoadFile
    DIRFILE_ANIM_4 = 4,
    DIRFILE_ANIM_5 = 5,
    DIRFILE_SKELETON = 6,           // AnimSkeletonProcess
    DIRFILE_SCRIPT = 7,             // Script_Load
    DIRFILE_MENU = 8,               // MenuManager_Load
    DIRFILE_MAP_B = 0xb,            // parsemap_parsemap, with MemType 1
    DIRFILE_LOADABLE = 0xc,         // a LoadableFiles entry
};

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
    name[0] = (char)Crt::toupper((signed char)*p++);
    while (name[i] != '\0') {
        i++;
        name[i] = (char)Crt::toupper((signed char)*p++);
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
        // GC check (0x8005c5ec): levels are 0x07xxxxxx (it loads the archive anyway)
        NF_WARN_IF((fileHash & 0xff000000) != HT_Level_HASHCODE_BASE, "Loader : Invalid level 0x%x\n", fileHash);
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
        NF_WARN("File %s not found\n", fileName); // GC check (0x8005c5ec)
        return false;
    }

    case LOADER_READ_DIRECTORY:
        if (!LoadFromDecompBuf) {
            pDirHeader = (LoaderDirHeader *)psiFileRead(LOADER_HEADER_BYTES);
        } else {
            pDirHeader = (LoaderDirHeader *)Euro_Decomp_Buf;
            Euro_Decomp_Buf += LOADER_HEADER_BYTES;
        }
        // With no files, the directory is not read and pDirData keeps its value (NULL, from LOADER_OPEN)
        if (pDirHeader->filesRemaining != 0) {
            if (!LoadFromDecompBuf) {
                pDirData = (uchar *)psiFileRead(pDirHeader->dirSize);
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
                // the batch goes at the low end of a free block
                tMallocMethod previousMethod = Mem_SetMallocMethod(MALLOC_LOW_END);
                dirFileBuf = (uint *)psiFileRead(BatchByteSize);
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

extern uint8_t LoadableIndex;

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

    // GC check (0x8005c444): types this build has no loader for are skipped
    NF_WARN_IF(DirFileType == 9 || DirFileType > 0x10, "Loader : Invalid file type %d\n", DirFileType);

    switch(DirFileType) {
        case DIRFILE_MAP_SECOND_PASS:
          AnimPostLoadInit();
          if (parsemap_parsemap(DirFileHash,'\x01')) {
            return false;
          }
          break;
        case DIRFILE_ANIM_3:
        case DIRFILE_ANIM_4:
        case DIRFILE_ANIM_5:
          AnimLoadFile(DirFileHash,0);
          return true;
        case DIRFILE_SKELETON:
          AnimSkeletonProcess((char *)dirFileBuf);
          return true;
        case DIRFILE_SCRIPT:
          Script_Load(DirFileHash,&CONST_ZERO_VECTOR,&CONST_ZERO_VECTOR,dirFileBuf,NULL, NULL, NULL);
          return true;
        case DIRFILE_MENU:
          MenuManager_Load(DirFileHash,dirFileBuf);
          return true;
        case DIRFILE_MAP_B:
          MemType = 1;
        case DIRFILE_MAP:
        case DIRFILE_MAP_2:
          if (parsemap_parsemap(DirFileHash,'\0')) { // false when it is done, true with more to parse
            return false;
          }
          break;
        case DIRFILE_LOADABLE:
          if (LoadableIndex < ARRAY_SIZE(LoadableFiles)) {
            LoadableFiles[LoadableIndex].hashcode = (HASHCODE) *dirFileBuf;
            LoadableFiles[LoadableIndex].loadableIdx = (char)dirFileBuf[1];
            LoadableIndex++;
          } else {
            NF_WARN("Too many loadable files\n"); // GC check (0x8005c13c)
          }
        }
        return true;

}

// The loadable part a file belongs to (its index, which LoadableLoad takes), or 0 when it is in none. Callers
// test the whole of EAX and SP_LoadScript passes it on, so this returns the index, not a flag.
// AUTOINJECT
uint isLoadable(HASHCODE hashcode) {
  for (int i = 0; i < LoadableIndex; i++) {
    if (LoadableFiles[i].hashcode == hashcode)
      return (uchar)LoadableFiles[i].loadableIdx;
  }
  return 0;
}

#if !NIGHTFIRE_DATA_PS2   // the PS2's load the loadable bundles: ps2/Ps2Loader.cpp

// A loadable part of the level (isLoadable's index). The Xbox build loads nothing for it: the hashtable's loadable
// items (type 0x04000000) are dropped and its sections made again, under MALLOC_BEST_FIT. Both arguments are
// unread.
// AUTOINJECT
bool LoadableLoad(HASHCODE hashcode, uint loadable) {
    __profiling_or_debugging_hook_point();
    tMallocMethod method = Mem_SetMallocMethod(MALLOC_BEST_FIT);
    hashtable_delete_hashtype(HT_AnimSeq_HASHCODE_BASE);
    bLevelLoaded = 0;
    hashtable_create_sections();
    Mem_SetMallocMethod(method);
    return true;
}

// The same for a script reloaded: the script freed first, and the anims' preload started again
// AUTOINJECT
bool LoadableReload(HASHCODE hashcode) {
    tMallocMethod method = Mem_SetMallocMethod(MALLOC_BEST_FIT);
    Script_FreeFromHashTable(hashcode);
    __profiling_or_debugging_hook_point();
    hashtable_delete_hashtype(HT_AnimSeq_HASHCODE_BASE);
    bLevelLoaded = 0;
    hashtable_create_sections();
    AnimPreLoadInit(1);
    Mem_SetMallocMethod(method);
    return true;
}

#endif // !NIGHTFIRE_DATA_PS2
