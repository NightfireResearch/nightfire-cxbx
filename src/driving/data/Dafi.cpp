#include "Dafi.h"

#include "../engine/UMemory.hpp"

#include <stddef.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// DAFI, the INI-style text reader (see Dafi.h). Section and key indexing write terminators into DAFI's own copy
// of the text, exactly where the original does; names compare with the game's _stricmp.
// ---------------------------------------------------------------------------------------------------------------

#define Crt_stricmp ((int (*)(const char *, const char *))0x00134537)

static bool IsLineEnd(char c) {
    return c == '\n' || c == '\r' || c == 0;
}

char *DafiNextLine(char *line, char *end) {
    if (line >= end)
        return NULL;
    while (!IsLineEnd(*line)) {
        line++;
        if (line >= end)
            return NULL;
    }
    if (line >= end)
        return NULL;
    while (IsLineEnd(*line) || *line == ' ' || *line == '\t') {
        line++;
        if (line >= end)
            return NULL;
    }
    if (line >= end)
        return NULL;
    return line;
}

char *DafiTrim(char *string) {
    char *last = string + strlen(string) - 1;
    while (*string == ' ' || *string == '\t')
        string++;
    while (last > string && (*last == ' ' || *last == '\t')) {
        *last = 0;
        last--;
    }
    return string;
}

// EAX = line, EDX = end -> EAX; EDX kept, as the original keeps it.
// AUTOLTCG
__declspec(naked) void FUN_0011a1b0() {
    __asm {
        push edx
        push edx
        push eax
        call DafiNextLine
        add esp, 8
        pop edx
        ret
    }
}

// EAX = string -> EAX.
// AUTOLTCG
__declspec(naked) void FUN_0011a200() {
    __asm {
        push eax
        call DafiTrim
        add esp, 4
        ret
    }
}

// FUNC_AT(0x0011a240)
DAFI* DAFI_open(const char *text, int size) {
    DAFI *dafi = static_cast<DAFI *>(OperatorNew(sizeof(DAFI)));
    if (dafi == NULL)
        return NULL;
    memset(dafi, 0, sizeof(DAFI));
    dafi->text = static_cast<char *>(OperatorNewArray(size + 1));
    if (dafi->text == NULL) {
        OperatorDelete(dafi);
        return NULL;
    }
    memcpy(dafi->text, text, size);
    dafi->text[size] = 0;
    char *s = dafi->text;
    dafi->textEnd = s + size;
    dafi->sectionCount = 0;
    if (s != NULL) {
        while (dafi->sectionCount < kDafiMaxSections) {
            if (*s == '[') {
                s++;
                dafi->sectionNames[dafi->sectionCount] = s;
                if (*s != ']') {
                    for (;;) {
                        if (IsLineEnd(*s))
                            break;
                        char next = s[1];
                        s++;
                        if (next == ']')
                            break;
                    }
                }
                if (*s == ']') {
                    *s = 0;
                    s = DafiNextLine(s, dafi->textEnd);
                    dafi->sectionBodies[dafi->sectionCount] = s;
                    dafi->sectionNames[dafi->sectionCount] = DafiTrim(dafi->sectionNames[dafi->sectionCount]);
                    dafi->sectionCount++;
                } else {
                    dafi->sectionNames[dafi->sectionCount] = NULL;   // no ']' on the line; s stays at its end
                }
            } else {
                s = DafiNextLine(s, dafi->textEnd);
            }
            if (s == NULL)
                break;
        }
    }
    dafi->currentSection = -1;
    return dafi;
}

// FUNC_AT(0x0011a370)
void DAFI_close(DAFI *dafi) {
    OperatorDelete(dafi->text);
    OperatorDelete(dafi);
}

// FUNC_AT(0x0011a390)
void DAFI_setsectionbyindex(DAFI *dafi, int section) {
    dafi->currentSection = section;
    char *s = dafi->sectionBodies[section];
    dafi->keyCount = 0;
    if (s == NULL)
        return;
    while (*s != '[' && dafi->keyCount < kDafiMaxKeys && s < dafi->textEnd) {
        // the key: up to '=' on this line
        dafi->keys[dafi->keyCount] = s;
        if (*s != '=') {
            for (;;) {
                if (IsLineEnd(*s) || s >= dafi->textEnd)
                    break;
                char next = s[1];
                s++;
                if (next == '=')
                    break;
            }
        }
        if (*s != '=' && *s != 0) {
            // A line with no '=': the original starts over at the same place, and never gets past it.
            dafi->keys[dafi->keyCount] = NULL;
            continue;
        }
        *s = 0;
        dafi->keys[dafi->keyCount] = DafiTrim(dafi->keys[dafi->keyCount]);

        // the value: past any terminators, then to the line's end or a ';'
        s++;
        if (*s == 0) {
            while (s < dafi->textEnd) {
                char next = s[1];
                s++;
                if (next != 0)
                    break;
            }
        }
        dafi->values[dafi->keyCount] = s;
        if (IsLineEnd(*s) && s < dafi->textEnd) {
            *s = ' ';   // an empty value: blanked, and the value runs on into the next line
            s++;
        }
        if (*s != '\n') {
            for (;;) {
                char c = *s;
                if (c == '\r' || c == ';' || c == 0 || s >= dafi->textEnd)
                    break;
                char next = s[1];
                s++;
                if (next == '\n')
                    break;
            }
        }
        *s = 0;
        dafi->values[dafi->keyCount] = DafiTrim(dafi->values[dafi->keyCount]);

        s = DafiNextLine(s, dafi->textEnd);
        dafi->keyCount++;
        if (s == NULL)
            return;
    }
}

// FUNC_AT(0x0011a5b0)
int DAFI_getsectioncount(DAFI *dafi) {
    return dafi->sectionCount;
}

// FUNC_AT(0x0011a5c0)
int DAFI_getsectionindex(DAFI *dafi, const char *name) {
    if (name == NULL)
        return dafi->currentSection;
    for (int i = 0; i < dafi->sectionCount; i++)
        if (Crt_stricmp(dafi->sectionNames[i], name) == 0)
            return i;
    return -1;
}

// FUNC_AT(0x0011a630)
char* DAFI_getsectionbyindex(DAFI *dafi, int section) {
    if (section == -1)
        return dafi->sectionNames[dafi->currentSection];
    return dafi->sectionNames[section];
}

// FUNC_AT(0x0011a650)
int DAFI_getkeycount(DAFI *dafi) {
    return dafi->keyCount;
}

// FUNC_AT(0x0011a660)
int DAFI_getkeyindex(DAFI *dafi, const char *key) {
    for (int i = 0; i < dafi->keyCount; i++)
        if (Crt_stricmp(dafi->keys[i], key) == 0)
            return i;
    return -1;
}

// FUNC_AT(0x0011a6b0)
char* DAFI_getkeybyindex(DAFI *dafi, int key) {
    return dafi->keys[key];
}

// FUNC_AT(0x0011a6c0)
char* DAFI_getvaluebyindex(DAFI *dafi, int key) {
    return dafi->values[key];
}

// FUNC_AT(0x0011a6d0)
int DAFI_setsection(DAFI *dafi, const char *name) {
    int section = DAFI_getsectionindex(dafi, name);
    if (section >= 0)
        DAFI_setsectionbyindex(dafi, section);
    return section;
}

// FUNC_AT(0x0011a700)
char* DAFI_getvalue(DAFI *dafi, const char *key) {
    int index = DAFI_getkeyindex(dafi, key);
    if (index >= 0)
        return dafi->values[index];
    return NULL;
}
