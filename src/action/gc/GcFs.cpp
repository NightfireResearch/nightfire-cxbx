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
#include "GcSound.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>

// The bundle converted last, by the name it was asked for
static char ConvertedName[16];
static GcBytesVec Converted;


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

// The sound files converted (GcSound.h), each kept by the name it was asked for: the banks' sample headers and
// stand-ins with their samples' PCM, the banks' sounds, the lists, the music's markers, and the last music track
struct SoundBank {
    GcBytesVec shf, sbf;
    std::vector<GcSoundSample> samples;     // each padded to whole Xbox ADPCM blocks, as its stand-in is long
    uint32_t used;                          // when it was last asked for, to give the oldest back
};
static std::map<int, SoundBank> SoundBanks;
static uint32_t SoundBankClock;
static std::map<std::string, GcBytesVec> SoundFiles;
static std::string MusicName;
static GcBytesVec Music;

#define SOUND_LIST_BYTES 0x800              // the Xbox's lists, which its archive pads with zeros to 2 KB
#define SOUND_PCM_KEPT (128u << 20)         // the banks' PCM kept, in bytes, before the oldest goes
#define XADPCM_BLOCK_SAMPLES 64

static std::string Upper(const char *name) {
    std::string s = name;
    for (char &c : s)
        c = (char)toupper((unsigned char)c);
    return s;
}

// A bank's number from its file's name (".../SB_<n>.SHF"), or -1
static int BankNumber(const char *name) {
    const char *at = strrchr(name, '_');
    return at != NULL ? atoi(at + 1) : -1;
}

// The bank converted from the disc's three files, its samples padded to whole blocks (a loop's padding taken from
// its start, the others' silent), the oldest banks given back past SOUND_PCM_KEPT
static SoundBank *LoadSoundBank(int bank, const char *path) {
    auto found = SoundBanks.find(bank);
    if (found != SoundBanks.end()) {
        found->second.used = ++SoundBankClock;
        return &found->second;
    }
    std::string base(path, strlen(path) - 3);
    GcBytesVec shf, shg, sbf;
    if (!ReadWhole(NULL, (base + "SHF").c_str(), shf) || !ReadWhole(NULL, (base + "SHG").c_str(), shg) ||
        !ReadWhole(NULL, (base + "SBF").c_str(), sbf))
        return NULL;
    SoundBank b;
    if (!GcSound_DecodeBank(shf.data(), (uint32_t)shf.size(), shg.data(), (uint32_t)shg.size(), sbf.data(),
                            (uint32_t)sbf.size(), b.samples))
        return NULL;
    size_t bytes = 0;
    for (GcSoundSample &s : b.samples) {
        size_t n = s.pcm.size(), padded = (n + XADPCM_BLOCK_SAMPLES - 1) / XADPCM_BLOCK_SAMPLES * XADPCM_BLOCK_SAMPLES;
        for (size_t k = n; k < padded; k++)
            s.pcm.push_back(s.loops ? s.pcm[s.loopStart + (k - n) % (n - s.loopStart)] : 0);
        bytes += s.pcm.size() * sizeof(int16_t);
    }
    GcSound_XboxBank(b.samples, bank, b.shf, b.sbf);
    b.used = ++SoundBankClock;
    SoundBank &kept = SoundBanks[bank] = std::move(b);
    printf("[gc] sound bank %d: %u samples, %.1f MB of PCM\n", bank, (unsigned)kept.samples.size(), bytes / 1048576.0);

    size_t total = 0;
    for (auto &k : SoundBanks)
        for (GcSoundSample &s : k.second.samples)
            total += s.pcm.size() * sizeof(int16_t);
    while (total > SOUND_PCM_KEPT && SoundBanks.size() > 1) {
        auto oldest = SoundBanks.begin();
        for (auto it = SoundBanks.begin(); it != SoundBanks.end(); ++it)
            if (it->second.used < oldest->second.used)
                oldest = it;
        if (oldest->first == bank)
            break;
        for (GcSoundSample &s : oldest->second.samples)
            total -= s.pcm.size() * sizeof(int16_t);
        SoundBanks.erase(oldest);
    }
    return &kept;
}

// A sound file as the Xbox's: its bytes, kept; NULL for one the GameCube has no counterpart of
static const GcBytesVec *LoadSoundFile(const char *name, const char *path) {
    if (EndsWith(name, ".SHF") || EndsWith(name, ".SBF")) {
        SoundBank *bank = LoadSoundBank(BankNumber(name), path);
        return bank == NULL ? NULL : EndsWith(name, ".SHF") ? &bank->shf : &bank->sbf;
    }
    if (EndsWith(name, ".SSD")) {
        if (MusicName != Upper(name)) {
            GcBytesVec ssd;
            if (!ReadWhole(NULL, path, ssd))
                return NULL;
            GcSound_XboxMusic(ssd.data(), (uint32_t)ssd.size(), Music);
            MusicName = Upper(name);
        }
        return &Music;
    }
    std::string key = Upper(name);
    auto found = SoundFiles.find(key);
    if (found != SoundFiles.end())
        return &found->second;
    GcBytesVec bytes;
    if (EndsWith(name, ".SHG")) {
        bytes.assign(4, 0);                 // not read by the Xbox's code
    } else {
        if (!ReadWhole(NULL, path, bytes))
            return NULL;
        if (EndsWith(name, ".SFX")) {
            GcSound_ConvertSfx(bytes.data(), (uint32_t)bytes.size());
        } else if (EndsWith(name, ".SMF")) {
            GcSound_ConvertMarkers(bytes.data(), (uint32_t)bytes.size());
        } else {                            // the lists: words
            GcSwapWords(bytes.data(), (uint32_t)bytes.size());
            if (bytes.size() < SOUND_LIST_BYTES)
                bytes.resize(SOUND_LIST_BYTES, 0);
        }
    }
    return &(SoundFiles[key] = std::move(bytes));
}

const int16_t *GcFs_SamplePcm(const void *data, uint32_t bytes, int channels, size_t *values) {
    int bank, sample;
    if (channels != 1 || !GcSound_ReadTag(data, bytes, &bank, &sample))
        return NULL;
    auto found = SoundBanks.find(bank);
    if (found == SoundBanks.end() || sample >= (int)found->second.samples.size())
        return NULL;
    const std::vector<int16_t> &pcm = found->second.samples[sample].pcm;
    static int told;
    if (told < 3) {
        told++;
        printf("[gc] sound bank %d's sample %d to the mixer: %u samples at %d Hz\n", bank, sample,
               (unsigned)pcm.size(), found->second.samples[sample].frequency);
    }
    *values = pcm.size();
    return pcm.data();
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
        const GcBytesVec *sound = LoadSoundFile(name, path);
        if (sound == NULL)
            return false;
        file->memory = sound->data();
        file->handle = INVALID_HANDLE_VALUE;
        file->size = (uint32_t)sound->size();
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
