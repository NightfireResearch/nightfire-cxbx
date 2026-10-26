#ifndef DRIVING_GC_GCUDATA_H_
#define DRIVING_GC_GCUDATA_H_

// The GameCube's data groups (CARP .crp, galleries .gal) as the Xbox's UGroup reads them (engine/UGroup.h,
// docs/gc-driving.md). The GameCube's records are the Xbox's 16 bytes, big-endian, and their flags word is a bit-field
// its compiler laid out from the top bit down:
//
//   group   bits 31-27 indexed, relative, owned, data sorted, groups sorted; bits 26-0 the subgroups
//   data    bits 31-29 indexed, relative, owned; bits 28-24 a 5-bit field; bits 23-0 the payload's size
//
// where the Xbox's has the same fields from bit 0 up (flags in bits 0-4 or 0-2, the 5-bit field in bits 3-7, the
// size from bit 8, the subgroups from bit 5). Only the record headers are turned round here: the payloads are
// big-endian field by field and need each record type's layout.

#include <stddef.h>
#include <stdint.h>

// Turns every record header of the group tree at data (size bytes) into the Xbox's order in place. False when an
// offset leaves the buffer; the headers met before that are already turned.
bool GcUData_ConvertHeaders(uint8_t *data, size_t size);

#endif // DRIVING_GC_GCUDATA_H_
