#include "Text.h"

// AUTOGEN
void Text_Update2Line(void);
// AUTOGEN
void Text_Update(void);
// AUTOGEN
char* Txt_BindLabel(Action_TranslatedText a, unsigned int b);

#define CurrentLanguage U32_AT(0x00215594)

void Txt_SetLanguage(uint languageId) {
    CurrentLanguage = languageId;
    Txt_LoadLanguage();
}

// AUTOGEN
void Txt_LoadLanguage(void);

