// settings.ini's DumpFiles=on: a copy of every file the game loads, under dump_driving\ with the game's own path.
// For looking at the data as the game sees it (archives unpacked). Off by default - it is slow and large. The same
// key in the same file turns on the action engine's dump (src/action/devtools/FileDump.cpp).

#include "FileDump.h"

#include <windows.h>
#include <direct.h>
#include <stdio.h>
#include <string.h>

static bool DumpEnabled(void) {
    static int enabled = -1;
    if (enabled < 0) {
        char value[16] = "";
        GetPrivateProfileStringA("Settings", "DumpFiles", "off", value, sizeof(value), ".\\settings.ini");
        enabled = (_stricmp(value, "on") == 0 || strcmp(value, "1") == 0) ? 1 : 0;
        if (enabled)
            printf("[dump] DumpFiles=on: every file loaded is saved under dump_driving\\\n");
    }
    return enabled != 0;
}

// Creates every folder in the path, so dump_driving\data\track\x.crp can be written into a fresh folder.
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
    if (!DumpEnabled() || data == NULL)
        return;

    char path[256];
    snprintf(path, sizeof(path), "dump_driving/%s", gamePath);
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
