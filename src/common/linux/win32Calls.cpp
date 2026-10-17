#include "win32Calls.h"

#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// See win32Calls.h.

static void HostPath(const char *path, char *out, size_t size) {
    snprintf(out, size, "%s", path);
    for (char *p = out; *p != '\0'; p++) {
        if (*p == '\\')
            *p = '/';
    }
}

static char *Trim(char *text) {
    while (*text == ' ' || *text == '\t')
        text++;
    char *end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        *--end = '\0';
    return text;
}

DWORD GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR defaultValue, LPSTR value, DWORD size, LPCSTR file) {
    if (size == 0)
        return 0;
    const char *found = defaultValue != NULL ? defaultValue : "";
    char path[MAX_PATH], line[1024], copy[1024];
    HostPath(file, path, sizeof(path));
    FILE *ini = fopen(path, "r");
    bool inSection = false;
    while (ini != NULL && fgets(line, sizeof(line), ini) != NULL) {
        char *text = Trim(line);
        if (text[0] == '[') {
            char *end = strchr(text, ']');
            if (end != NULL)
                *end = '\0';
            inSection = strcasecmp(Trim(text + 1), section) == 0;
            continue;
        }
        char *equals = strchr(text, '=');
        if (!inSection || equals == NULL)
            continue;
        *equals = '\0';
        if (strcasecmp(Trim(text), key) != 0)
            continue;
        char *v = Trim(equals + 1);
        size_t length = strlen(v);
        if (length >= 2 && (v[0] == '"' || v[0] == '\'') && v[length - 1] == v[0]) {
            v[length - 1] = '\0';
            v++;
        }
        snprintf(copy, sizeof(copy), "%s", v);
        found = copy;
        break;
    }
    if (ini != NULL)
        fclose(ini);
    snprintf(value, size, "%s", found);
    return (DWORD)strlen(value);
}

// The leading decimal number, as Windows reads it; the default if the key is missing
UINT GetPrivateProfileIntA(LPCSTR section, LPCSTR key, INT defaultValue, LPCSTR file) {
    char value[64];
    if (GetPrivateProfileStringA(section, key, "", value, sizeof(value), file) == 0)
        return (UINT)defaultValue;
    return (UINT)strtol(value, NULL, 10);
}

BOOL QueryPerformanceCounter(LARGE_INTEGER *counter) {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    counter->QuadPart = (LONGLONG)now.tv_sec * 1000000000 + now.tv_nsec;
    return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER *frequency) {
    frequency->QuadPart = 1000000000;
    return TRUE;
}

void Sleep(DWORD milliseconds) {
    timespec delay = { (time_t)(milliseconds / 1000), (long)(milliseconds % 1000) * 1000000 };
    while (nanosleep(&delay, &delay) != 0) {
    }
}

BOOL CreateDirectoryA(LPCSTR path, SECURITY_ATTRIBUTES *) {
    char host[MAX_PATH];
    HostPath(path, host, sizeof(host));
    return mkdir(host, 0777) == 0;
}

struct Search {
    DIR *directory;
    char pattern[MAX_PATH];
};

static BOOL NextMatch(Search *search, WIN32_FIND_DATAA *found) {
    while (dirent *entry = readdir(search->directory)) {
        if (fnmatch(search->pattern, entry->d_name, FNM_CASEFOLD) == 0) {
            snprintf(found->cFileName, sizeof(found->cFileName), "%s", entry->d_name);
            return TRUE;
        }
    }
    return FALSE;
}

HANDLE FindFirstFileA(LPCSTR pattern, WIN32_FIND_DATAA *found) {
    char host[MAX_PATH];
    HostPath(pattern, host, sizeof(host));
    char *slash = strrchr(host, '/');
    Search *search = new Search;
    snprintf(search->pattern, sizeof(search->pattern), "%s", slash != NULL ? slash + 1 : host);
    if (slash != NULL)
        *slash = '\0';
    search->directory = opendir(slash != NULL ? (host[0] != '\0' ? host : "/") : ".");
    if (search->directory == NULL || !NextMatch(search, found)) {
        FindClose(search);
        return INVALID_HANDLE_VALUE;
    }
    return search;
}

BOOL FindNextFileA(HANDLE handle, WIN32_FIND_DATAA *found) {
    return NextMatch(static_cast<Search *>(handle), found);
}

BOOL FindClose(HANDLE handle) {
    Search *search = static_cast<Search *>(handle);
    if (search->directory != NULL)
        closedir(search->directory);
    delete search;
    return TRUE;
}
