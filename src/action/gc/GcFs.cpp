// The file system on the GameCube's disc (docs/gc-data.md): FS.h's calls over the disc's loose files, in place of
// FS.cpp's filesys.dNN archives. Built only with NIGHTFIRE_ACTION_DATA_GC.
//
// The names the game asks for become paths on the GameCube's disc (GcDisc_Path), read through the game's own file
// layer (XboxFile.cpp, paths on d:, which DiscPath names). A bundle is converted whole to the Xbox's when it is first
// asked for (GcBundle_Load) and kept until another is; other files are served as the disc has them. Every read is
// complete when it returns, as the Xbox build's overlapped reads now are, so the state machine FS.cpp runs is never
// busy.

#include "../engine/FS.h"
#include "../engine/XboxFile.h"
#include "../engine/XboxError.h"
#include "../memory.h"
#include "../game.h"            // timestamp
#include "../gfx/LowLevel.h"    // ShowLoadProgressScreen
#include "GcBundle.h"
#include "GcBytes.h"
#include "GcDisc.h"
#include "GcFs.h"

#include <stdio.h>
#include <string.h>

// The bundle converted last, by the name it was asked for
static char ConvertedName[16];
static GcBytesVec Converted;

// Until the sound is converted (docs/gc-data.md, milestone 5) the game hears none: every sound bank is served empty,
// the music list names no tracks, and the bank list is turned round to the Xbox's byte order
static GcBytesVec SoundStandIn;

static HANDLE OpenOnDisc(const char *path) {
    char discPath[300];
    snprintf(discPath, sizeof(discPath), "d:\\%s", path);
    return createFile(discPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
}

static uint32_t SizeOf(HANDLE handle) {
    LARGE_INTEGER size;
    return getFileSize_LargeInteger(handle, &size) ? (uint32_t)size.LowPart : 0;
}

static bool ReadAt(HANDLE handle, void *buffer, uint32_t len, uint32_t offset) {
    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    overlapped.Offset = offset;
    return readFromFileBlocking(handle, buffer, len, NULL, &overlapped) != 0;
}

// GcBundle_Load's reader: a file of the disc, whole
static bool ReadWhole(void *context, const char *path, GcBytesVec &out) {
    HANDLE handle = OpenOnDisc(path);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    out.resize(SizeOf(handle));
    bool ok = out.empty() || ReadAt(handle, out.data(), (uint32_t)out.size(), 0);
    DoNtClose(handle);
    return ok;
}

static bool EndsWith(const char *name, const char *suffix) {
    size_t n = strlen(name), k = strlen(suffix);
    return n >= k && _stricmp(name + n - k, suffix) == 0;
}

// The silent stand-in for a sound file: false for one the game must not open (the music, which the empty list
// keeps it from asking for)
#define SOUND_LIST_BYTES 0x800
static bool SoundStandIn_Make(const char *name, const char *path) {
    SoundStandIn.clear();
    if (EndsWith(name, ".SFX") || EndsWith(name, ".SHF") || EndsWith(name, ".SBF") || EndsWith(name, ".SHG")) {
        SoundStandIn.assign(4, 0);          // a count of 0
        return true;
    }
    if (EndsWith(name, "MFXINFO.MXI")) {
        SoundStandIn.assign(SOUND_LIST_BYTES, 0xff);
        return true;
    }
    if (EndsWith(name, "SBINFO.SBI") && ReadWhole(NULL, path, SoundStandIn)) {
        // The Xbox's, word for word, which its archive pads with zeros to 2 KB
        GcSwapWords(SoundStandIn.data(), (uint32_t)SoundStandIn.size());
        if (SoundStandIn.size() < SOUND_LIST_BYTES)
            SoundStandIn.resize(SOUND_LIST_BYTES, 0);
        return true;
    }
    return false;
}

// A file the game asked for: a converted bundle or a stand-in in memory, or a file on the disc
struct GcFile {
    const uint8_t *memory;
    HANDLE handle;
    uint32_t size;
};

// The file named, false when the GameCube's disc has none
static bool GcFile_Open(const char *name, GcFile *file) {
    char path[256];
    GcFileKind kind = GcDisc_Path(name, path, sizeof(path));
    if (kind == GC_FILE_NONE)
        return false;
    if (kind == GC_FILE_BUNDLE) {
        if (_stricmp(name, ConvertedName) != 0) {
            GcBundleStats stats;
            ConvertedName[0] = '\0';
            if (!GcBundle_Load(GcDisc_BundleHash(name), ReadWhole, NULL, Converted, &stats))
                return false;
            snprintf(ConvertedName, sizeof(ConvertedName), "%s", name);
            printf("[gc] %s: %u bytes as the Xbox's, %d entries (%d sequences added), %d textures, %d meshes (%d "
                   "skinned)\n", name, (unsigned)Converted.size(), stats.entries, stats.sequencesAdded,
                   stats.texturesConverted, stats.meshesConverted, stats.meshesSkinned);
        }
        file->memory = Converted.data();
        file->handle = INVALID_HANDLE_VALUE;
        file->size = (uint32_t)Converted.size();
        return true;
    }
    if (kind == GC_FILE_SOUND) {
        if (!SoundStandIn_Make(name, path))
            return false;
        file->memory = SoundStandIn.data();
        file->handle = INVALID_HANDLE_VALUE;
        file->size = (uint32_t)SoundStandIn.size();
        return true;
    }
    file->memory = NULL;
    file->handle = OpenOnDisc(path);
    if (file->handle == INVALID_HANDLE_VALUE)
        return false;
    file->size = SizeOf(file->handle);
    return true;
}

static void GcFile_Close(GcFile *file) {
    if (file->handle != INVALID_HANDLE_VALUE)
        DoNtClose(file->handle);
    file->handle = INVALID_HANDLE_VALUE;
}

// Reads len bytes from offset in the file, no further than its end
static void GcFile_Read(const GcFile *file, void *buffer, uint32_t len, uint32_t offset) {
    if (offset >= file->size)
        return;
    if (len > file->size - offset)
        len = file->size - offset;
    if (file->memory != NULL)
        memcpy(buffer, file->memory + offset, len);
    else if (!ReadAt(file->handle, buffer, len, offset))
        FS_FatalErrorHandler();
}

// A missing file is fatal, as the Xbox's archive reader has it
static void GcFile_OpenOrDie(const char *name, GcFile *file) {
    if (!GcFile_Open(name, file)) {
        printf("[gc] %s is not on the GameCube's disc\n", name);
        FS_FatalErrorHandler();
    }
}

int FS_MatchFilenameToHeader(char *filename) {
    char path[256];
    return GcDisc_Path(filename, path, sizeof(path)) != GC_FILE_NONE ? 0 : -1;
}

void FS_Init(void) {
    ConvertedName[0] = '\0';
    Converted.clear();
    GcBytesVec probe;
    if (!ReadWhole(NULL, "gc\\common.bin", probe))
        XboxError_Halt("the GameCube disc has no gc\\common.bin (DiscPath in settings.ini)", "d:\\gc\\common.bin");
}

// Every read is complete when it returns
bool FS_StateMachineIterate(void) {
    return false;
}

bool FS_OpInProgressWithCleanup(void) {
    return false;
}

int FS_GetFileSize(char *filename) {
    GcFile file;
    GcFile_OpenOrDie(filename, &file);
    int size = (int)file.size;
    GcFile_Close(&file);
    return size;
}

// Level bundles (0x0700xxxx) loaded so far: each starts the level's heap again
static uint32_t LevelLoads;

uint32_t GcFs_LevelLoads(void) {
    return LevelLoads;
}

void FS_LoadFileIfReady(char *fileName, void *buffer) {
    if ((GcDisc_BundleHash(fileName) & 0xfff00000) == 0x07000000)
        LevelLoads++;
    GcFile file;
    GcFile_OpenOrDie(fileName, &file);
    GcFile_Read(&file, buffer, file.size, 0);
    GcFile_Close(&file);
}

double TimeSpentLoadingFiles;

// The buffer the next whole file goes into, when it fits (GcFs_LoadNextInto), and the last one loaded
static void *LoadInto;
static uint32_t LoadIntoBytes;
static void *LastLoad;
static uint32_t LastLoadBytes;

void GcFs_LoadNextInto(void *buffer, uint32_t bytes) {
    LoadInto = buffer;
    LoadIntoBytes = bytes;
}

void *GcFs_LastLoad(uint32_t *bytes) {
    *bytes = LastLoadBytes;
    return LastLoad;
}

// As FS.cpp's (0x000dc990): the whole file, in memory of its own - or in the buffer GcFs_LoadNextInto gave, as the
// GameCube's LoaderLoad reads a loadable into the buffer its caller passes
void* FS_AllocateAndLoadBlocking(char *filename, MallocFlags allocType, int *sizeOut) {
    TimeSpentLoadingFiles = TimeSpentLoadingFiles - timestamp();
    int size = FS_GetFileSize(filename);
    if (size < 1) {
        logError("File missing..\n\n%s", filename);
        ShowFatalErrorScreen(true);
        XboxError_Halt("file missing from the GameCube disc", filename);
    }
    bool into = LoadInto != NULL && (uint32_t)size <= LoadIntoBytes;
    void *data = into ? LoadInto : Mem_Malloc(size, allocType, 0);
    LastLoad = data;
    LastLoadBytes = into ? LoadIntoBytes : (uint32_t)size;    // the buffer's whole size stays usable
    LoadInto = NULL;
    FS_LoadFileIfReady(filename, data);
    while (FS_StateMachineIterate())
        ShowLoadProgressScreen();
    if (sizeOut != NULL)
        *sizeOut = size;
    TimeSpentLoadingFiles = timestamp() + TimeSpentLoadingFiles;
    return data;
}

static char EurocomPath[256];
char* FS_EurocomPath(const char *name) {
    strcpy(EurocomPath, "d:\\gc\\");
    strcat(EurocomPath, name);
    return EurocomPath;
}

// The sound code's streams. Slot 0 is never used: 0 is "no file" to the callers.
#define MAX_ASYNC_STREAMS 16
struct AsyncStream {
    bool open;
    GcFile file;
};
static AsyncStream AsyncStreams[MAX_ASYNC_STREAMS];

int psiAsyncOpenFile(char *fileName) {
    int stream = 1;
    while (stream < MAX_ASYNC_STREAMS && AsyncStreams[stream].open)
        stream++;
    if (stream >= MAX_ASYNC_STREAMS)
        FS_FatalErrorHandler();
    // The GameCube has no streamed speech: the caller gives up on the stream at -1
    if (!GcFile_Open(fileName, &AsyncStreams[stream].file))
        return -1;
    AsyncStreams[stream].open = true;
    return stream;
}

void psiAsyncCloseFile(int fileHandle) {
    GcFile_Close(&AsyncStreams[fileHandle].file);
    AsyncStreams[fileHandle].open = false;
}

// As FS.cpp's: byteSize bytes at byteOffset into destBuffer, or the whole file for -1, the read widened to whole
// 2 KB sectors (so it lands up to 2 KB before destBuffer and runs past its end: psiAsyncCreateBuffer leaves room)
#define SECTOR_BYTES 0x800
void psiAsyncReadFile(int fileHandle, int byteOffset, int byteSize, void *destBuffer) {
    if (fileHandle < 1)
        return;
    const GcFile *file = &AsyncStreams[fileHandle].file;
    if (byteSize == -1) {
        GcFile_Read(file, destBuffer, file->size, 0);
        return;
    }
    uint32_t start = byteOffset & ~(uint32_t)(SECTOR_BYTES - 1);
    uint32_t end = (byteOffset + (SECTOR_BYTES - 1) + byteSize) & ~(uint32_t)(SECTOR_BYTES - 1);
    GcFile_Read(file, (char *)destBuffer + (int32_t)(start - byteOffset), end - start, start);
}

int psiAsyncHasReadFinished(int fileHandle) {
    return 1;
}
