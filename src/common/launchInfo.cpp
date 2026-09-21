#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The launch data page, and launching the other image.
//
// A console title moves between XBEs by rebooting into the next one: XLaunchNewImageA writes a page of launch
// data (here, the score buffer - "BOND" and the mission state) that survives the soft reboot, and the image
// that comes up reads it back with XGetLaunchInfo. Nightfire does this all the time: the action engine
// launches DRIVING.XBE for a driving mission, the driving engine launches *itself* between the parts of a
// mission (ReturnToAction, 0x00130740, picks the target from a table: DRIVING.XBE for the next part,
// DEFAULT.XBE to hand back), and the action engine comes back the same way.
//
// Both replacements below work on a file. XGetLaunchInfo reads psiLaunch.bin if it exists; XLaunchNewImageA
// writes it and then starts the loader for the named image - driving.exe or action.exe, next to this process's
// own executable, inheriting its working directory and its standard handles so that one log carries on across
// the relaunch - and exits. Two XBEs cannot share a process, because both are linked at 0x10000 and the loader
// gets that address by being the image there, so the relaunch is the only shape this can take (see
// docs/driving-engine-plan.md, section 0). Under the CXBX launchers there is no sibling loader to start, and the
// process just says what was asked of it and exits, as it always did.
//
// The file is not removed after it is read. The action engine treats its contents as the persistent PTPDATA
// (Language_Get skips its first-run scan when there is data, see src/action/engine/XboxFile.cpp), and a stale
// file is also the quickest way back to a later part of a mission: copy the one the hand-over wrote and start
// the loader.
// ---------------------------------------------------------------------------------------------------------------

#define LAUNCH_FILE "psiLaunch.bin"
#define LAUNCH_WORDS 0x300

// Replaces XAPILIB::XGetLaunchInfo
int __stdcall XGetLaunchInfo(int *launchDataType, void *data) {
    printf("[launch] XGetLaunchInfo: data at 0x%08x\n", (unsigned)(uintptr_t)data);

    FILE *file = fopen(LAUNCH_FILE, "rb");
    if (file == NULL)
        return 0x490;   // ERROR_NOT_FOUND: no launch data page

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    size_t words = fread(data, 4, LAUNCH_WORDS, file);
    fclose(file);
    printf("[launch] read %ld bytes of launch data from " LAUNCH_FILE " (%u words taken)\n", length, (unsigned)words);

    // LDT_TITLE: launched by another title. Values 2 and 3 (dashboard, debugger command line) make the games
    // take other paths - the action engine fails out on anything non-zero, the driving engine treats 3 as a
    // mission name on the command line.
    *launchDataType = 0;
    return 0;
}

static HANDLE InheritableCopy(DWORD which) {
    HANDLE handle = GetStdHandle(which);
    HANDLE copy = NULL;
    if (handle == NULL || handle == INVALID_HANDLE_VALUE)
        return handle;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &copy, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return handle;
    return copy;
}

// Starts the loader for the named image, next to this process's own executable. False if there is no such
// loader there - which is the case under the CXBX launchers, whose executable is cxbx's.
static bool StartLoaderFor(const char *executableName) {
    const char *slash = strrchr(executableName, '\\');
    const char *image = slash != NULL ? slash + 1 : executableName;

    const char *loader = NULL;
    if (_stricmp(image, "DRIVING.XBE") == 0)
        loader = "driving.exe";
    else if (_stricmp(image, "DEFAULT.XBE") == 0)
        loader = "action.exe";
    if (loader == NULL) {
        printf("[launch] no loader is known for %s\n", image);
        return false;
    }

    char path[MAX_PATH];
    if (GetModuleFileNameA(NULL, path, sizeof(path)) == 0)
        return false;
    char *cut = strrchr(path, '\\');
    if (cut == NULL)
        return false;
    snprintf(cut + 1, sizeof(path) - (size_t)(cut + 1 - path), "%s", loader);

    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        printf("[launch] %s is not there, so nothing can be started for %s\n", path, image);
        return false;
    }

    STARTUPINFOA startup;
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = InheritableCopy(STD_INPUT_HANDLE);
    startup.hStdOutput = InheritableCopy(STD_OUTPUT_HANDLE);
    startup.hStdError = InheritableCopy(STD_ERROR_HANDLE);

    char commandLine[MAX_PATH + 2];
    snprintf(commandLine, sizeof(commandLine), "\"%s\"", path);

    PROCESS_INFORMATION process;
    memset(&process, 0, sizeof(process));
    if (!CreateProcessA(path, commandLine, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process)) {
        printf("[launch] could not start %s (error %lu)\n", path, GetLastError());
        return false;
    }
    printf("[launch] started %s as process %lu\n", path, process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

// Replaces XAPILIB::XLaunchNewImageA
int __stdcall XLaunchNewImageA(char *executableName, void *launchInfo) {
    printf("\n[launch] ================================================================\n"
           "[launch] the game asked to launch %s with %u bytes of launch data\n",
           executableName, (unsigned)(LAUNCH_WORDS * 4));

    FILE *file = fopen(LAUNCH_FILE, "wb");
    if (file == NULL) {
        perror("[launch] could not open " LAUNCH_FILE " for writing");
        fflush(stdout);
        ExitProcess(1);
    }
    size_t written = fwrite(launchInfo, 4, LAUNCH_WORDS, file);
    fclose(file);
    if (written != LAUNCH_WORDS)
        printf("[launch] only %u of %u words reached " LAUNCH_FILE "\n", (unsigned)written, (unsigned)LAUNCH_WORDS);
    else
        printf("[launch] launch data written to " LAUNCH_FILE "\n");

    bool started = StartLoaderFor(executableName);
    if (!started)
        printf("[launch] the data is in " LAUNCH_FILE " for whichever loader runs %s next. Exiting.\n", executableName);
    printf("[launch] ================================================================\n");
    fflush(stdout);

    FILE *note = fopen("crash.log", "a");
    if (note != NULL) {
        fprintf(note, "[launch] the game launched %s%s - not a fault\n",
                executableName, started ? " (its loader was started)" : " (no loader started)");
        fclose(note);
    }
    ExitProcess(0);
    return 0;
}
