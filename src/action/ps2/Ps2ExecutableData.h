#ifndef PS2_PS2EXECUTABLEDATA_H_
#define PS2_PS2EXECUTABLEDATA_H_

// Tables the game keeps in its executable where the PS2's differ from the Xbox's we compile in (docs/ps2-data.md),
// read from ACTION.ELF by its symbols when the file system starts. Built only with NIGHTFIRE_ACTION_DATA_PS2.

#include <stddef.h>
#include <stdint.h>

// The button symbols in text ("~X", Font.cpp's SpecialChars): the PS2's specialchar (0x002de700 in SLES-51258), 17
// symbols in the PS2's buttons texture where the Xbox has 23 in its own. The slots past them are left empty (no
// letter), so a "~K" the PS2 has no symbol for draws nothing, as on the PS2.
void Ps2_ReadExecutableData(const uint8_t *elf, size_t elfSize);

#endif // PS2_PS2EXECUTABLEDATA_H_
