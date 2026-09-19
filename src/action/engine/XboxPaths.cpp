#include "XboxPaths.h"
#include "XboxSettings.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

// See XboxPaths.h for what this is and which drives map where.

const char *Xbox_GetDiscRoot(void) {
    return Settings_GetDiscPath();
}

// The fixed roots. Only D: is configurable - the other three are working directories this project creates
// beside the executable, and there is no reason to let them wander.
#define SAVE_ROOT  "saves"
#define CACHE_ROOT "cache"

bool Xbox_ResolvePath(const char *xboxPath, char *out, size_t outSize) {
    if (out == NULL || outSize == 0)
        return false;
    out[0] = '\0';
    if (xboxPath == NULL)
        return false;

    const char *root = NULL;
    const char *rest = xboxPath;

    // A drive letter is exactly "<letter>:" followed by a separator or the end of the string. Anything else -
    // including a bare relative path - falls through untouched.
    if (xboxPath[0] != '\0' && xboxPath[1] == ':') {
        switch (tolower((unsigned char)xboxPath[0])) {
            case 'd': root = Xbox_GetDiscRoot(); break;
            case 't': root = SAVE_ROOT;  break;
            case 'u': root = SAVE_ROOT;  break;
            case 'z': root = CACHE_ROOT; break;
            default:  root = NULL;       break;
        }
        if (root != NULL) {
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
