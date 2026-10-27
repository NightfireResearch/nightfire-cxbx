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
// size from bit 8, the subgroups from bit 5). The payloads are big-endian field by field: each record type is turned
// round by its layout (GcUData.cpp's table, the structures the game reads them as). No game state: the offline
// checker uses it too.

#include <stddef.h>
#include <stdint.h>

// Turns every record header of the group tree at data (size bytes) into the Xbox's order in place. False when an
// offset leaves the buffer; the headers met before that are already turned.
bool GcUData_ConvertHeaders(uint8_t *data, size_t size);

// Turns every data record's payload of a tree whose headers are already the Xbox's into the Xbox's order, by its
// type. The model object (ELFd, ELFr) and image containers (tx) are left as they are; the loader and the image
// conversion take those. False when an offset leaves the buffer.
bool GcUData_ConvertPayloads(uint8_t *data, size_t size);

// One payload of a record type (the tag as the Xbox's header has it, parent the tag of the group holding it): what
// GcUData_ConvertPayloads does to each. False for a type it does not know, which it leaves as it is.
bool GcUData_ConvertPayload(uint32_t tag, uint32_t parent, uint8_t *payload, uint32_t size, uint32_t count);

#endif // DRIVING_GC_GCUDATA_H_
