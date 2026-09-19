#include "XboxPaths.h"
#include "XboxSettings.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>

// See XboxPaths.h for what this is and which drives map where.

const char *Xbox_GetDiscRoot(void) {
    return Settings_GetDiscPath();
}

// The fixed roots. Only D: is configurable - the other three are working directories this project creates
// beside the executable, and there is no reason to let them wander.
#define SAVE_ROOT  "saves"
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
            case 't': root = SAVE_ROOT;  break;
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
