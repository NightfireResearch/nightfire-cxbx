#ifndef DRIVING_DATA_DAFI_H_
#define DRIVING_DATA_DAFI_H_

// DAFI: EA's reader for INI-style text files - "[section]" lines and "key = value" lines under them, values
// ending at the line's end or at ';'. The attribute system loads its collections through it, and so do IniFiles,
// the loading screen, the camera ini loader and the sound manager.
//
// DAFI_open copies the text and indexes its sections (up to 1000): the name between the brackets, trimmed, and
// where the section's first line starts. Selecting a section (DAFI_setsection, DAFI_setsectionbyindex) indexes
// its keys (up to 1000) in place in the copy: each "key=value" line has its '=' and line end overwritten with
// terminators, key and value trimmed of spaces and tabs. Lookups compare names with _stricmp.
//
// The parser's quirks are the original's and are kept: a line inside a section with no '=' makes indexing it
// loop forever, and an empty value takes the next line as its value. The game's files have neither.

#include <stdint.h>

constexpr int kDafiMaxSections = 1000;
constexpr int kDafiMaxKeys = 1000;

struct DAFI {
    int currentSection;                       // -1 until a section is selected
    int sectionCount;
    char *sectionNames[kDafiMaxSections];     // null for a "[name" line missing its ']'
    char *sectionBodies[kDafiMaxSections];    // the first line after each section's header
    int keyCount;                             // the current section's
    char *keys[kDafiMaxKeys];
    char *values[kDafiMaxKeys];
    char *text;                               // the copy, terminated
    char *textEnd;
};
static_assert(sizeof(DAFI) == 0x3e94, "DAFI is 0x3e94 bytes");

DAFI* DAFI_open(const char *text, int size);              // 0x0011a240: null if out of memory
void DAFI_close(DAFI *dafi);                              // 0x0011a370
void DAFI_setsectionbyindex(DAFI *dafi, int section);     // 0x0011a390
int DAFI_getsectioncount(DAFI *dafi);                     // 0x0011a5b0
int DAFI_getsectionindex(DAFI *dafi, const char *name);   // 0x0011a5c0: null name -> current section; -1 if none
char* DAFI_getsectionbyindex(DAFI *dafi, int section);    // 0x0011a630: -1 -> the current section's name
int DAFI_getkeycount(DAFI *dafi);                         // 0x0011a650
int DAFI_getkeyindex(DAFI *dafi, const char *key);        // 0x0011a660: -1 if none
char* DAFI_getkeybyindex(DAFI *dafi, int key);            // 0x0011a6b0 (Ghidra: FUN_0011a6b0)
char* DAFI_getvaluebyindex(DAFI *dafi, int key);          // 0x0011a6c0 (Ghidra: FUN_0011a6c0)
int DAFI_setsection(DAFI *dafi, const char *name);        // 0x0011a6d0: the section's index, -1 if none
char* DAFI_getvalue(DAFI *dafi, const char *key);         // 0x0011a700: null if none

// The two register-argument helpers DAFI_open calls: adaptors under Ghidra's names (AUTOLTCG), the C++ under them.
void FUN_0011a1b0();   // EAX = line, EDX = end -> EAX: the next line's first non-blank character, null at the end
void FUN_0011a200();   // EAX = string -> EAX: trimmed of spaces and tabs, in place
char* DafiNextLine(char *line, char *end);
char* DafiTrim(char *string);

#endif // DRIVING_DATA_DAFI_H_
