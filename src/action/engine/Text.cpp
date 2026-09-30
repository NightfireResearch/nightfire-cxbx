#include "Text.h"

#include "psiFile.h"
#include "../memory.h"
#include "../util/bin.h"

#include <stdio.h>
#include <string.h>

// AUTOGEN
void Text_Update2Line(void);
// AUTOGEN
void Text_Update(void);
// AUTOGEN
void __stdcall Text_FlushAllSubtitles(void);

// XBE_GLOBAL(0x00215588, 0x4)
static const char** Bank;
// XBE_GLOBAL(0x0021558c, 0x4)
static void* BankData;
// XBE_GLOBAL(0x00215590, 0x4)
static uint32_t NumEntries;
// XBE_GLOBAL(0x00215594, 0x4)
static uint32_t CurrentLanguage;
// XBE_GLOBAL(0x00215580, 0x4)
static uint32_t NumFixups;
// XBE_GLOBAL(0x001fec78, 0x4)
static uint* FixupTable;
#define StringHeapLock (*(uint8_t(*)[256])0x001fec80) // a lock count per heap string (Txt_LockString, Txt_UnlockString)
// XBE_GLOBAL(0x00215584, 0x4)
static uint32_t StringHeapCnt;


// AUTOINJECT
void Txt_SetLanguage(tLANGUAGE languageId) {
    CurrentLanguage = languageId;
    Txt_LoadLanguage();
}

// Order must match the order in the enum
// XBE_GLOBAL(0x0017c1f4, 0x24)
const char* LanguageFileNames[] = {
    "UKTxt.dat",
    "FRTxt.dat",
    "GRTxt.dat",
    "SPTxt.dat",
    "ITTxt.dat",
    "DUTxt.dat",
    "USATxt.dat",
    "JAPTxt.dat",
    "SWTxt.dat"
};

// AUTOINJECT
void Txt_LoadLanguage(void) {

    char filename[1024];
    sprintf(filename, "%s%s", "", LanguageFileNames[CurrentLanguage]);
    printf("Loading language %i from file %s\n", CurrentLanguage, filename);

    int size = 0;
    void* dataFile = psiFileLoad(filename, 0x3604, &size);
    void* fileAt = dataFile;

    if(dataFile == NULL)
        return;

    uint bankSize = BIN_GetDWord((uint**)&fileAt); // Also increments fileAt

    //printf("Loaded %i bytes of data file, of which %i is bank info\n", size, bankSize);

    // Allocate and copy bank data to RAM - this is the raw ASCII or UTF-16 buffer
    BankData = Mem_Malloc(bankSize, 0x3604, 0);
    memcpy(BankData, fileAt, bankSize);
    
    // Add 4 bytes even if the string ends on a word boundary already. Unclear why.
    if(bankSize % 4 == 0)
        fileAt = (void*)(((int)fileAt + 4));

    // The file is padded to a multiple of 4 bytes
    fileAt = (void*)(((int)fileAt + bankSize + 3) & ~3);

    NumEntries = BIN_GetDWord((uint**)&fileAt); // 2801 strings
    //printf("Num entries: %i\n", NumEntries);

    Bank = (const char**)Mem_Malloc(NumEntries * 4, 0x3604, 0); // 4 bytes of offset (into the ASCII data array) per string
    
    // Looks like the first entry in the bank is hardcoded to be a blank string
    Bank[0] = "";

    // "relocate", ie map from offset to memory address (ie so that we can access as char*)
    for(uint i = 1; i < NumEntries; i++) {
        uint32_t offset = BIN_GetDWord((uint**)&fileAt);
        Bank[i] = (const char*)((uint)BankData + offset);
        //printf("String %i: %s\n", i, Bank[i]);
    }
    
    // Fixup table has 7 entries
    NumFixups = BIN_GetDWord((uint**)&fileAt);
    //printf("Num fixups: %i\n", NumFixups);
    
    FixupTable = (uint*) Mem_Malloc((NumFixups+1) * 4, 0x3604, 0);

    for(uint i = 0; i < NumFixups; i++) {
        uint offset = BIN_GetDWord((uint**)&fileAt);
        FixupTable[i] = offset;
    }

    Mem_Free(&dataFile);

}

uint Txt_GetIndex(Action_TranslatedText tt) {
    return (int)FixupTable[(tt >> 24)] + (tt & 0xFFFFFF);
}

// AUTOINJECT
uint GetLanguage(void) {
    return CurrentLanguage;
}

// 256 strings of 0x168 bytes that Txt_BindLabel and friends format into, each with a lock count
// (StringHeapLock) - 0 means free. Handed out round-robin from StringHeapCnt; when none is free, everyone
// shares one overflow buffer.
#define StringHeap (*(char(*)[256][0x168])0x001fed80)
// XBE_GLOBAL(0x00215598, 0x168)
static char StringHeapOverflow[0x168];

// Finds the next free heap string at or after StringHeapCnt, wrapping round, locks it with lockCount and
// returns it emptied. The first scan compares only StringHeapCnt's low 16 bits against 256, the second the whole
// value, as the original does.
// AUTOINJECT
char* Txt_GetStringFromHeap(uchar lockCount) {
    uint start = StringHeapCnt;
    uint found = 0x100;
    for (ushort i = (ushort)start; i < 0x100; i++) {
        if (StringHeapLock[i] == 0) {
            found = i;
            break;
        }
    }
    if (found == 0x100) {
        for (ushort i = 0; i < start; i++) {
            if (StringHeapLock[i] == 0) {
                found = i;
                break;
            }
        }
        if (found == 0x100)
            return StringHeapOverflow;
    }
    StringHeapLock[found] = lockCount;
    StringHeapCnt = found + 1;
    StringHeap[found][0] = '\0';
    return StringHeap[found];
}

// AUTOINJECT
const char* Txt_BindLabel(Action_TranslatedText a, unsigned int b) {

    if(a == 0xFFFFFFFF || FixupTable == NULL)
        return NULL;

    uint index = Txt_GetIndex(a);
    
    if(index >= NumEntries)
        return "Invalid Text Label";

    if(index == 0) {
        return (const char*)Txt_GetStringFromHeap((uchar)b);
    }

    if(Bank != NULL)
        return Bank[index];
    
    return "Not Loaded";
}


// AUTOINJECT
void Txt_LanguageInit(void) {
    memset(&StringHeapLock, 0, sizeof(StringHeapLock));
    StringHeapCnt = 0;
    BankData = NULL;
    Bank = NULL;
    Txt_LoadLanguage();
}

// AUTOGEN
void Text_AddMsg(char param_1,char param_2,int param_3,const char *str,int param_5,short maybeDurationFrames);

// AUTOGEN
void Txt_UnlockString(char* text);

// AUTOGEN
void Txt_LockString(char* text);
