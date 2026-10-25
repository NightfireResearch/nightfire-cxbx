#include "GcDisc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool Ends(const char *name, const char *suffix) {
    size_t n = strlen(name), k = strlen(suffix);
    if (n < k)
        return false;
    for (size_t i = 0; i < k; i++)
        if (tolower((unsigned char)name[n - k + i]) != tolower((unsigned char)suffix[i]))
            return false;
    return true;
}

static bool Starts(const char *name, const char *prefix) {
    for (; *prefix != '\0'; name++, prefix++)
        if (tolower((unsigned char)*name) != tolower((unsigned char)*prefix))
            return false;
    return true;
}

uint32_t GcDisc_BundleHash(const char *name) {
    if (strlen(name) != 12 || !Ends(name, ".bin"))
        return 0;
    for (int i = 0; i < 8; i++)
        if (!isxdigit((unsigned char)name[i]))
            return 0;
    return (uint32_t)strtoul(name, NULL, 16);
}

// The name with '\' for '/', after prefix
static void Join(char *path, size_t pathSize, const char *prefix, const char *name) {
    snprintf(path, pathSize, "%s%s", prefix, name);
    for (char *p = path; *p != '\0'; p++)
        if (*p == '/')
            *p = '\\';
}

GcFileKind GcDisc_Path(const char *name, char *path, size_t pathSize) {
    path[0] = '\0';
    if (GcDisc_BundleHash(name) != 0) {
        Join(path, pathSize, "gc\\", name);
        return GC_FILE_BUNDLE;
    }
    if (Ends(name, "txt.dat") || Ends(name, "txtu.dat")) {
        Join(path, pathSize, "common\\", name);
        return GC_FILE_AS_IS;
    }
    if (Ends(name, "woman.rle")) {
        Join(path, pathSize, "common\\", "Woman.rle");
        return GC_FILE_AS_IS;
    }
    if (Ends(name, "sbinfo.sbi")) {
        Join(path, pathSize, "gc\\audio\\SFX\\", "SBInfo.sbi");
        return GC_FILE_SOUND;
    }
    if (Ends(name, "mfxinfo.mxi")) {
        Join(path, pathSize, "gc\\audio\\SFX\\", "MFXInfo.mxi");
        return GC_FILE_SOUND;
    }
    if (Starts(name, "music/") || Starts(name, "music\\")) {
        Join(path, pathSize, "gc\\audio\\SFX\\", name);
        return GC_FILE_SOUND;
    }
    // A language's sound banks ("ENGLISH/SB_0/SB_0.SHF"): the GameCube's folder is "English"
    const char *slash = strpbrk(name, "/\\");
    if (slash != NULL && Starts(slash + 1, "SB_")) {
        char language[32];
        size_t n = (size_t)(slash - name);
        if (n == 0 || n >= sizeof(language))
            return GC_FILE_NONE;
        for (size_t i = 0; i < n; i++)
            language[i] = (char)(i == 0 ? toupper((unsigned char)name[i]) : tolower((unsigned char)name[i]));
        language[n] = '\0';
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "gc\\audio\\SFX\\%s\\", language);
        Join(path, pathSize, prefix, slash + 1);
        return GC_FILE_SOUND;
    }
    if (Ends(name, ".xmv") && strlen(name) == 12) {
        char movie[16];
        snprintf(movie, sizeof(movie), "%.8s.h4m", name);
        Join(path, pathSize, "gc\\movies\\", movie);
        return GC_FILE_MOVIE;
    }
    return GC_FILE_NONE;
}
