#ifndef COMMON_LINUX_MSVCNAMES_H_
#define COMMON_LINUX_MSVCNAMES_H_

// The Microsoft C runtime's names the code calls, as the C library's own, for the Linux build (included ahead of
// every source by CMakeLists.txt). Each answers as the Microsoft one does. Nothing is included here: a source's own
// feature macros (_FILE_OFFSET_BITS, _GNU_SOURCE) must come before the C library's first header.

#define _stricmp strcasecmp   // <string.h> declares it

// In the C locale, a-z only
static inline char *MsvcStrupr(char *text) {
    for (char *c = text; *c != '\0'; c++)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 'a' + 'A');
    return text;
}
#define _strupr MsvcStrupr

#endif // COMMON_LINUX_MSVCNAMES_H_
