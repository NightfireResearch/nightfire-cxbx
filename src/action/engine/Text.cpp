#include "Text.h"

#include "../psiFile.h"
#include "../memory.h"
#include "../util/bin.h"

#include <stdio.h>
#include <string.h>

// AUTOGEN
void Text_Update2Line(void);
// AUTOGEN
void Text_Update(void);

#define Bank (*(const char***)0x00215588)
#define BankData (*(void**)0x0021558c)
#define NumEntries U32_AT(0x00215590)
#define CurrentLanguage U32_AT(0x00215594)
#define NumFixups U32_AT(0x00215580)
#define FixupTable (*(uint**)0x001fec78)

// AUTOINJECT
void Txt_SetLanguage(uint languageId) {
    CurrentLanguage = languageId;
    Txt_LoadLanguage();
}



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
    
    // The file is padded to a multiple of 4 bytes
    // TODO: The Python script adds 4 bytes even if the string ends on a word boundary already. Why?
    fileAt = (void*)(((int)fileAt + bankSize + 3) & ~3);

    NumEntries = BIN_GetDWord((uint**)&fileAt); // 2801 strings
    //printf("Num entries: %i\n", NumEntries);

    Bank = (const char**)Mem_Malloc(NumEntries * 4, 0x3604, 0); // 4 bytes of offset (into the ASCII data array) per string
    
    // Looks like the first entry in the bank is hardcoded to be a blank string
    Bank[0] = "";

    // "relocate", ie map from offset to memory address (ie so that we can access as char*)
    for(int i = 1; i < NumEntries; i++) {
        uint32_t offset = BIN_GetDWord((uint**)&fileAt);
        Bank[i] = (const char*)((uint)BankData + offset);
        //printf("String %i: %s\n", i, Bank[i]);
    }
    
    // Fixup table has 7 entries
    NumFixups = BIN_GetDWord((uint**)&fileAt);
    //printf("Num fixups: %i\n", NumFixups);
    
    FixupTable = (uint*) Mem_Malloc((NumFixups+1) * 4, 0x3604, 0);

    for(int i = 0; i < NumFixups; i++) {
        uint offset = BIN_GetDWord((uint**)&fileAt);
        FixupTable[i] = offset;
    }

    Mem_Free(&dataFile);

}

uint Txt_GetIndex(Action_TranslatedText tt) {
    return (int)FixupTable[(tt >> 24)] + (tt & 0xFFFFFF);
}

// AUTOGEN
unsigned char* Txt_GetStringFromHeap(uchar index);

// AUTOINJECT
const char* Txt_BindLabel(Action_TranslatedText a, unsigned int b) {

    if(a == 0xFFFFFFFF || FixupTable == NULL)
        return NULL;

    uint index = Txt_GetIndex(a);
    
    if(index >= NumEntries)
        return "Invalid Text Label";

    if(index == 0) {
        return (const char*)Txt_GetStringFromHeap(b);
    }

    if(Bank != NULL)
        return Bank[index];
    
    return "Not Loaded";
}

