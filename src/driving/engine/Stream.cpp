#include "Stream.h"

#include "../../common/standalone.h"
#include "../../common/xboxPath.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// Media that is not on the disc.
//
// A console's disc is always complete. A dump of one need not be: this project's own disc folder has the
// levels, the speech and the music but none of the .mad movies, and the game's handling of that is to wait
// for them. Two guards here, both of which do nothing when the file is present.
//
// THE MOVIE. RunTheGame picks an intro per level - "<region>\island_intro2.mad" for uw_mis11 - and hands it
// to GameLoop_StopUsingMainBigFile (0x0005a6b0), which builds a PlayMPC and plays it. PlayMPC::Play already
// copes with having no movie: its whole body is under "if (player != 0)". What it does not cope with is a
// player that exists but whose stream never produces a first frame, which is the missing-file case - it
// dereferences the null frame handler and faults at 0x0013097d. So the check happens before any of that, at
// the one place that has the path in its hand, and the game skips the movie exactly as it does for a level
// that has none.
//
// THE STREAM. STREAM_queuefile (0x0014b3b0) is the streaming reader's queue, and the movie player is only its
// biggest customer. A request for a file the host does not have is refused here rather than queued, because
// the callers' pattern is
//
//     do { chunk = STREAM_get(stream); SYNCTASK_run(0); } while (!STREAM_isidle(stream));
//
// and a request that can never complete leaves that loop spinning - measured at ten to sixteen seconds per
// frame before this existed. Refusing it leaves the stream idle, so the loop ends at once.
//
// Neither of these is a "skip movies" switch. Put the .mad files on the disc and the movies play.
// ---------------------------------------------------------------------------------------------------------------

#define STREAM_QUEUEFILE_ADDRESS   0x0014b3b0u
#define PLAY_INTRO_MOVIE_ADDRESS   0x0005a6b0u

// Both are __cdecl - a plain RET, with the caller doing the cleaning - unlike most of the XBE's library code.
typedef unsigned int (__cdecl *StreamQueueFileFn)(void *stream, const char *path, unsigned int a,
                                                  unsigned int b);
typedef void (__cdecl *PlayIntroMovieFn)(const char *path);

static StreamQueueFileFn g_originalQueueFile = NULL;
static PlayIntroMovieFn g_originalPlayIntroMovie = NULL;

// Is this file on the host? The paths that arrive here are Xbox paths, but not always complete ones: the
// movie path is "<region>\<name>.mad" with no drive letter, because the reader prepends D: itself. So a path
// without a drive is tried both as it stands and under the disc root, and only counts as missing if neither
// is there.
static bool HostHasFile(const char *xboxPath) {
    char hostPath[MAX_PATH];
    if (Xbox_ResolvePath(xboxPath, hostPath, sizeof(hostPath)) &&
        GetFileAttributesA(hostPath) != INVALID_FILE_ATTRIBUTES)
        return true;

    if (xboxPath[0] != '\0' && xboxPath[1] == ':')
        return false;   // it named a drive and was not there

    char onDisc[MAX_PATH];
    snprintf(onDisc, sizeof(onDisc), "d:\\%s", xboxPath);
    return Xbox_ResolvePath(onDisc, hostPath, sizeof(hostPath)) &&
           GetFileAttributesA(hostPath) != INVALID_FILE_ATTRIBUTES;
}

// Each missing file is worth saying once. The game asks for the same ambient sound every few frames when it
// cannot have it, and a line per request buries everything else in the log.
static bool ReportedBefore(const char *path) {
    static char reported[32][MAX_PATH];
    static unsigned count = 0;

    for (unsigned i = 0; i < count; i++) {
        if (strcmp(reported[i], path) == 0)
            return true;
    }
    if (count < sizeof(reported) / sizeof(reported[0]))
        snprintf(reported[count++], MAX_PATH, "%s", path);
    return false;
}

static unsigned int __cdecl Stream_QueueFile(void *stream, const char *path, unsigned int a, unsigned int b) {
    if (path != NULL && !HostHasFile(path)) {
        if (ReportedBefore(path))
            return 0;
        printf("[stream] %s is not on the disc - refusing the request rather than waiting for it\n", path);
        fflush(stdout);
        return 0;   // nothing queued, so the stream is idle and the caller's wait ends at once
    }
    return g_originalQueueFile(stream, path, a, b);
}

static void __cdecl Stream_PlayIntroMovie(const char *path) {
    if (path != NULL && !HostHasFile(path)) {
        printf("[movie] %s is not on the disc - skipping it\n", path);
        fflush(stdout);
        return;
    }
    g_originalPlayIntroMovie(path);
}

// ---------------------------------------------------------------------------------------------------------------
// Patching. Both of these keep the original and call it, so the patch is a jump at the entry with a
// trampoline holding the instructions it displaced. The number of bytes to copy is per function, because it
// has to be a whole number of instructions: a five-byte jump landing in the middle of one would leave the
// tail of that instruction behind for the trampoline's return jump to run into.
// ---------------------------------------------------------------------------------------------------------------

static bool InstallHook(const char *tag, unsigned address, const unsigned char *prologue,
                        unsigned prologueLength, void *replacement, unsigned char *trampoline,
                        void **originalOut) {
    unsigned char *site = (unsigned char *)address;

    // Checked rather than assumed: a wrong address would put a jump in the middle of something else.
    if (memcmp(site, prologue, prologueLength) != 0) {
        printf("[%s] 0x%08x does not start as expected - not patching it\n", tag, address);
        return false;
    }

    DWORD previous = 0;
    if (!VirtualProtect(site, prologueLength, PAGE_EXECUTE_READWRITE, &previous)) {
        printf("[%s] could not make 0x%08x writable (error %lu)\n", tag, address, GetLastError());
        return false;
    }
    VirtualProtect(trampoline, 16, PAGE_EXECUTE_READWRITE, &previous);

    memcpy(trampoline, prologue, prologueLength);
    trampoline[prologueLength] = 0xE9;                       // jmp back to the rest of the original
    *(int *)(trampoline + prologueLength + 1) =
        (int)((site + prologueLength) - (trampoline + prologueLength + 5));
    *originalOut = (void *)trampoline;

    site[0] = 0xE9;                                          // jmp rel32 to the replacement
    *(int *)(site + 1) = (int)((unsigned char *)replacement - (site + 5));
    memset(site + 5, 0x90, prologueLength - 5);              // nops, so no half instruction is left behind

    FlushInstructionCache(GetCurrentProcess(), site, prologueLength);
    FlushInstructionCache(GetCurrentProcess(), trampoline, 16);
    return true;
}

void Inject_Stream(void) {
    if (!Xbox_RunningStandalone())
        return;

    // "mov eax, [esp+4]" then "test eax, eax": six bytes, two instructions.
    static const unsigned char queueFilePrologue[] = { 0x8b, 0x44, 0x24, 0x04, 0x85, 0xc0 };
    static unsigned char queueFileTrampoline[16];
    InstallHook("stream", STREAM_QUEUEFILE_ADDRESS, queueFilePrologue, sizeof(queueFilePrologue),
                (void *)Stream_QueueFile, queueFileTrampoline, (void **)&g_originalQueueFile);

    // "push -1" then "push 0x151f38", the start of the function's exception frame: seven bytes, and both are
    // position-independent, so they run just as well from the trampoline.
    static const unsigned char playMoviePrologue[] = { 0x6a, 0xff, 0x68, 0x38, 0x1f, 0x15, 0x00 };
    static unsigned char playMovieTrampoline[16];
    InstallHook("movie", PLAY_INTRO_MOVIE_ADDRESS, playMoviePrologue, sizeof(playMoviePrologue),
                (void *)Stream_PlayIntroMovie, playMovieTrampoline, (void **)&g_originalPlayIntroMovie);
}
