#include "xboxPath.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>

// See xboxPath.h for what this is and which drives map where.

#define DEFAULT_DISC_ROOT "../disc"

// Where D: is, as an absolute path. Empty until it is set or first asked for, when the default is resolved.
// This file is compiled into both the loader and the inject DLL, and each has its own copy; both set it from
// the same settings.ini key, and resolve it the same way, so they agree.
static char g_discRoot[MAX_PATH];

// A relative path is taken relative to the folder the executables are in, not the working directory, so the
// disc is found however the game was started. That folder is found from the module this code is in - the
// loader, or the inject DLL beside it - rather than from the process, so that it does not depend on which
// executable is hosting this code. Should that fail, the path is left relative to the working
// directory, which is what it was before and is still right when the game is started from its own folder.
static void ResolveAgainstOwnFolder(const char *path, char *out, size_t outSize) {
    char joined[MAX_PATH * 2];
    bool absolute = path[0] == '/' || path[0] == '\\' || (path[0] != '\0' && path[1] == ':');

    char exe[MAX_PATH];
    DWORD length = 0;
    HMODULE self = NULL;
    if (!absolute && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        (LPCSTR)&ResolveAgainstOwnFolder, &self))
        length = GetModuleFileNameA(self, exe, sizeof(exe));
    char *lastSeparator = NULL;
    if (length > 0 && length < sizeof(exe)) {
        for (char *p = exe; *p != '\0'; p++) {
            if (*p == '\\' || *p == '/')
                lastSeparator = p;
        }
    }

    if (lastSeparator != NULL) {
        lastSeparator[1] = '\0';
        snprintf(joined, sizeof(joined), "%s%s", exe, path);
    } else {
        snprintf(joined, sizeof(joined), "%s", path);
    }

    // Folds the "..", so that logs and the paths built on it say where the disc really is.
    DWORD full = GetFullPathNameA(joined, (DWORD)outSize, out, NULL);
    if (full == 0 || full >= outSize)
        snprintf(out, outSize, "%s", joined);
}

void Xbox_SetDiscRoot(const char *path) {
    if (path != NULL && path[0] != '\0')
        ResolveAgainstOwnFolder(path, g_discRoot, sizeof(g_discRoot));
}

const char *Xbox_GetDiscRoot(void) {
    if (g_discRoot[0] == '\0')
        ResolveAgainstOwnFolder(DEFAULT_DISC_ROOT, g_discRoot, sizeof(g_discRoot));
    return g_discRoot;
}

// The fixed roots. Only D: is configurable - the others are working directories this project creates beside
// the executable, and there is no reason to let them wander.
//
// T: and U: are different drives on the Xbox and must not share a directory here. U: is the title's saved
// games, which is what the game enumerates to build the list of player profiles; T: is its persistent data,
// where it keeps things like the chosen language in t:\lang<nn><nn>.dat. Mapping both onto "saves" put that
// language file among the profiles, and it duly appeared in the codename list as a player called "lang0100".
#define SAVE_ROOT  "saves"
#define TITLE_ROOT "tdata"
#define CACHE_ROOT "cache"

// The writable roots have to exist before anything can be created in them. On the Xbox that was the kernel's
// job - XMountUtilityDrive brought Z: up, and XapiInitProcess created the title's own directories - and since
// none of that runs any more, nothing else would. Skipping it is not a visible failure at the time: the first
// symptom is a write to z:\state.bin failing with ERROR_PATH_NOT_FOUND much later, when the engine tries to
// hand over to the driving executable.
//
// Once per root is enough, and a failure is ignored the same way psiSave.cpp ignores it - the directory
// already existing is the common case, and any real problem surfaces on the open that follows.
static void EnsureRootExists(const char *root) {
    static const char *created[4];
    static int createdCount = 0;
    for (int i = 0; i < createdCount; i++) {
        if (created[i] == root)
            return;
    }
    if (createdCount < (int)(sizeof(created) / sizeof(created[0])))
        created[createdCount++] = root;
    _mkdir(root);
}

bool Xbox_ResolvePath(const char *xboxPath, char *out, size_t outSize) {
    if (out == NULL || outSize == 0)
        return false;
    out[0] = '\0';
    if (xboxPath == NULL)
        return false;

    const char *root = NULL;
    const char *rest = xboxPath;
    const char *discRootCache = Xbox_GetDiscRoot();

    // A drive letter is exactly "<letter>:" followed by a separator or the end of the string. Anything else -
    // including a bare relative path - falls through untouched.
    if (xboxPath[0] != '\0' && xboxPath[1] == ':') {
        switch (tolower((unsigned char)xboxPath[0])) {
            case 'd': root = discRootCache; break;
            case 't': root = TITLE_ROOT; break;   // title persistent data, not saves - see above
            case 'u': root = SAVE_ROOT;  break;
            case 'z': root = CACHE_ROOT; break;
            default:  root = NULL;       break;
        }
        if (root != NULL) {
            // D: is the read-only disc and must already be there; the rest are ours to create.
            if (root != discRootCache)
                EnsureRootExists(root);
            rest = xboxPath + 2;
            while (*rest == '\\' || *rest == '/')
                rest++;
        }
    }

    int written;
    if (root != NULL)
        written = snprintf(out, outSize, "%s/%s", root, rest);
    else
        written = snprintf(out, outSize, "%s", rest);

    if (written < 0 || (size_t)written >= outSize) {
        out[0] = '\0';
        return false;
    }

    for (char *p = out; *p != '\0'; p++) {
        if (*p == '\\')
            *p = '/';
    }
    return true;
}
