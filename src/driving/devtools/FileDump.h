#ifndef DRIVING_DEVTOOLS_FILEDUMP_H_
#define DRIVING_DEVTOOLS_FILEDUMP_H_

#include <stddef.h>

// Saves a loaded file under dump_driving\ when settings.ini has DumpFiles=on; does nothing otherwise
// (FileDump.cpp).
void FileDump_Save(const char *gamePath, const void *data, size_t size);

#endif // DRIVING_DEVTOOLS_FILEDUMP_H_
