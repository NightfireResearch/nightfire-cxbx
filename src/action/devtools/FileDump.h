#ifndef ACTION_DEVTOOLS_FILEDUMP_H_
#define ACTION_DEVTOOLS_FILEDUMP_H_

#include <stddef.h>

// Saves a loaded file under dump\ when settings.ini has DumpFiles=on; does nothing otherwise (FileDump.cpp).
void FileDump_Save(const char *gamePath, const void *data, size_t size);

#endif // ACTION_DEVTOOLS_FILEDUMP_H_
