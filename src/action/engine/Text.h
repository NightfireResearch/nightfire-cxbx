#ifndef TEXT_H_
#define TEXT_H_

#include "../actionhelpers.h"

typedef enum {
    Lang_UK = 0,
    Lang_FR = 1,
    Lang_GR = 2,
    Lang_SP = 3,
    Lang_IT = 4,
    Lang_DU = 5,
    Lang_USA = 6,
    Lang_JAP = 7,
    Lang_SW = 8
} tLANGUAGE;

void Text_Update2Line(void);
void Text_Update(void);
const char * Txt_BindLabel(Action_TranslatedText param_1,undefined4 param_2);
void Txt_SetLanguage(tLANGUAGE languageId);
void Txt_LoadLanguage(void);
void Txt_LanguageInit(void);

#endif // TEXT_H_
