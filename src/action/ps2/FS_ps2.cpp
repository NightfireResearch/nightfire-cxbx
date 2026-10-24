// The file system on the PS2's disc (docs/ps2-data.md): FS.h's calls over FILES.BIN and the loose PS2\ files, in
// place of FS.cpp's filesys.dNN archives. Built only with NIGHTFIRE_ACTION_DATA_PS2.
//
// PS2 psiFileOpen (0x10bf00) finds a name in FileList, the table in ACTION.ELF, and reads FILES.BIN from the entry's
// sector; the IOP's sound driver reads the loose files itself. Here both are read through the game's own file layer
// (XboxFile.cpp, paths on d:, which DiscPath names), and every read is complete when it returns, as the Xbox build's
// overlapped reads now are (docs/action-portable.md), so the state machine FS.cpp runs is never busy.

#include "../engine/FS.h"
#include "../engine/XboxFile.h"
#include "../engine/XboxError.h"
#include "../memory.h"
#include "../game.h"            // timestamp
#include "../gfx/LowLevel.h"    // ShowLoadProgressScreen
#include "Ps2Disc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A file the game asked for: where its bytes are
struct Ps2File {
    HANDLE handle;          // FILES.BIN's, or a loose file's own
    uint32_t base;          // its first byte in that file
    uint32_t size;
    bool loose;             // the handle is the file's own, closed with it
};

struct Ps2FileSystem {
    Ps2FileListEntry files[PS2_FILELIST_COUNT];
    int fileCount;
    HANDLE filesBin;
};
static Ps2FileSystem FileSystem;

// The sound code's streams (psiAsyncOpenFile and friends). Slot 0 is never used: 0 is "no file" to the callers.
#define MAX_ASYNC_STREAMS 16
struct AsyncStream {
    bool open;
    Ps2File file;
};
static AsyncStream AsyncStreams[MAX_ASYNC_STREAMS];

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

// The pause screen's film, the Xbox's woman.rle: the "woman" entry of the first level bundle in FileList that has one.
// Its place in FILES.BIN, found once.
static bool FindWoman(uint32_t *offset, uint32_t *size) {
    static uint32_t womanOffset, womanSize;
    static bool searched;
    if (!searched) {
        searched = true;
        for (int i = 0; i < FileSystem.fileCount && womanSize == 0; i++) {
            const Ps2FileListEntry *bundle = &FileSystem.files[i];
            if (bundle->name[0] != '0' || bundle->name[1] != '7' || bundle->size <= PS2_SECTOR_SIZE)
                continue;
            uint8_t header[PS2_SECTOR_SIZE];
            uint32_t dirSize;
            if (!ReadAt(FileSystem.filesBin, header, sizeof(header), bundle->offset))
                continue;
            memcpy(&dirSize, header, 4);
            if (dirSize == 0 || PS2_SECTOR_SIZE + dirSize > bundle->size)
                continue;
            uint8_t *directory = (uint8_t *)malloc(dirSize);
            uint32_t at, length;
            if (directory != NULL && ReadAt(FileSystem.filesBin, directory, dirSize, bundle->offset + PS2_SECTOR_SIZE) &&
                Ps2Disc_FindBundleEntry(header, directory, dirSize, PS2_BUNDLE_WOMAN_TYPE, PS2_BUNDLE_WOMAN_NAME, &at,
                                        &length)) {
                womanOffset = bundle->offset + at;
                womanSize = length;
            }
            free(directory);
        }
    }
    *offset = womanOffset;
    *size = womanSize;
    return womanSize != 0;
}

// The file named: a FileList entry in FILES.BIN, the woman, else a loose file under PS2\. False when it is none.
static bool Ps2File_Open(const char *name, Ps2File *file) {
    int index = Ps2Disc_FindFile(FileSystem.files, FileSystem.fileCount, name);
    if (index >= 0) {
        file->handle = FileSystem.filesBin;
        file->base = FileSystem.files[index].offset;
        file->size = FileSystem.files[index].size;
        file->loose = false;
        return true;
    }
    if (_stricmp(name, "woman.rle") == 0 && FindWoman(&file->base, &file->size)) {
        file->handle = FileSystem.filesBin;
        file->loose = false;
        return true;
    }
    char path[256];
    Ps2Disc_LoosePath(name, path, sizeof(path));
    HANDLE handle = OpenOnDisc(path);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    file->handle = handle;
    file->base = 0;
    file->size = SizeOf(handle);
    file->loose = true;
    return true;
}

static void Ps2File_Close(Ps2File *file) {
    if (file->loose)
        DoNtClose(file->handle);
    file->handle = INVALID_HANDLE_VALUE;
}

// Reads len bytes from offset in the file, no further than its end
static void Ps2File_Read(const Ps2File *file, void *buffer, uint32_t len, uint32_t offset) {
    if (offset >= file->size)
        return;
    if (len > file->size - offset)
        len = file->size - offset;
    if (!ReadAt(file->handle, buffer, len, file->base + offset))
        FS_FatalErrorHandler();
}

// A missing file is fatal, as the Xbox's archive reader has it
static void Ps2File_OpenOrDie(const char *name, Ps2File *file) {
    if (!Ps2File_Open(name, file)) {
        printf("[ps2] %s is neither in FileList nor a loose file\n", name);
        FS_FatalErrorHandler();
    }
}

int FS_MatchFilenameToHeader(char *filename) {
    return Ps2Disc_FindFile(FileSystem.files, FileSystem.fileCount, filename);
}

// FileList from ACTION.ELF, and FILES.BIN opened
void FS_Init(void) {
    memset(&FileSystem, 0, sizeof(FileSystem));
    memset(AsyncStreams, 0, sizeof(AsyncStreams));

    HANDLE elfHandle = OpenOnDisc("ACTION.ELF");
    if (elfHandle == INVALID_HANDLE_VALUE)
        XboxError_Halt("the PS2 disc has no ACTION.ELF (DiscPath in settings.ini)", "d:\\ACTION.ELF");
    uint32_t elfSize = SizeOf(elfHandle);
    uint8_t *elf = (uint8_t *)malloc(elfSize);
    bool read = elf != NULL && ReadAt(elfHandle, elf, elfSize, 0);
    DoNtClose(elfHandle);
    if (read)
        FileSystem.fileCount = Ps2Disc_ReadFileList(elf, elfSize, FileSystem.files, PS2_FILELIST_COUNT);
    free(elf);
    if (FileSystem.fileCount == 0)
        XboxError_Halt("ACTION.ELF has no FileList symbol", "d:\\ACTION.ELF");

    FileSystem.filesBin = OpenOnDisc("FILES.BIN");
    if (FileSystem.filesBin == INVALID_HANDLE_VALUE)
        XboxError_Halt("the PS2 disc has no FILES.BIN", "d:\\FILES.BIN");
    printf("[ps2] FileList: %d files in FILES.BIN\n", FileSystem.fileCount);
}

// Every read is complete when it returns
bool FS_StateMachineIterate(void) {
    return false;
}

bool FS_OpInProgressWithCleanup(void) {
    return false;
}

int FS_GetFileSize(char *filename) {
    Ps2File file;
    Ps2File_OpenOrDie(filename, &file);
    int size = (int)file.size;
    Ps2File_Close(&file);
    return size;
}

void FS_LoadFileIfReady(char *fileName, void *buffer) {
    Ps2File file;
    Ps2File_OpenOrDie(fileName, &file);
    Ps2File_Read(&file, buffer, file.size, 0);
    Ps2File_Close(&file);
}

double TimeSpentLoadingFiles;

// As FS.cpp's (0x000dc990): the whole file, in memory of its own
void* FS_AllocateAndLoadBlocking(char *filename, MallocFlags allocType, int *sizeOut) {
    TimeSpentLoadingFiles = TimeSpentLoadingFiles - timestamp();
    int size = FS_GetFileSize(filename);
    if (size < 1) {
        logError("File missing..\n\n%s", filename);
        ShowFatalErrorScreen(true);
        XboxError_Halt("file missing from the PS2 disc", filename);
    }
    void *data = Mem_Malloc(size, allocType, 0);
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
    strcpy(EurocomPath, "d:\\eurocom\\");
    strcat(EurocomPath, name);
    return EurocomPath;
}

int psiAsyncOpenFile(char *fileName) {
    int stream = 1;
    while (stream < MAX_ASYNC_STREAMS && AsyncStreams[stream].open)
        stream++;
    if (stream >= MAX_ASYNC_STREAMS)
        FS_FatalErrorHandler();
    Ps2File_OpenOrDie(fileName, &AsyncStreams[stream].file);
    AsyncStreams[stream].open = true;
    return stream;
}

void psiAsyncCloseFile(int fileHandle) {
    Ps2File_Close(&AsyncStreams[fileHandle].file);
    AsyncStreams[fileHandle].open = false;
}

// As FS.cpp's: byteSize bytes at byteOffset into destBuffer, or the whole file for -1, the read widened to whole
// 2 KB sectors (so it lands up to 2 KB before destBuffer and runs past its end: psiAsyncCreateBuffer leaves room)
void psiAsyncReadFile(int fileHandle, int byteOffset, int byteSize, void *destBuffer) {
    if (fileHandle < 1)
        return;
    const Ps2File *file = &AsyncStreams[fileHandle].file;
    if (byteSize == -1) {
        Ps2File_Read(file, destBuffer, file->size, 0);
        return;
    }
    uint32_t start = byteOffset & ~(uint32_t)(PS2_SECTOR_SIZE - 1);
    uint32_t end = (byteOffset + (PS2_SECTOR_SIZE - 1) + byteSize) & ~(uint32_t)(PS2_SECTOR_SIZE - 1);
    Ps2File_Read(file, (char *)destBuffer + (int32_t)(start - byteOffset), end - start, start);
}

int psiAsyncHasReadFinished(int fileHandle) {
    return 1;
}
