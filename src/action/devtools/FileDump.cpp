// settings.ini's DumpFiles=on: a copy of every file the game loads, under dump\ with the game's own path. For
// looking at the data as the game sees it (archives unpacked). Off by default - it is slow and large.

#include "FileDump.h"

#include <direct.h>
#include <stdio.h>

#include "../engine/XboxSettings.h"

// Creates every folder in the path, so dump\levels\m01\x.dat can be written into a fresh dump\.
static void CreateFolders(const char *path) {
    char folders[256];
    snprintf(folders, sizeof(folders), "%s", path);
    for (char *p = folders + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char separator = *p;
            *p = '\0';
            _mkdir(folders);   // already there is fine
            *p = separator;
        }
    }
}

void FileDump_Save(const char *gamePath, const void *data, size_t size) {
    if (!Settings_GetDumpFiles() || data == NULL)
        return;

    char path[256];
    snprintf(path, sizeof(path), "dump/%s", gamePath);
    CreateFolders(path);

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        printf("[dump] cannot write %s\n", path);
        return;
    }
    if (fwrite(data, 1, size, file) != size)
        printf("[dump] short write to %s\n", path);
    else
        printf("[dump] %s (%u bytes)\n", path, (unsigned)size);
    fclose(file);
}
