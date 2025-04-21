#ifndef TEXT_H_
#define TEXT_H_

#include "../actionhelpers.h"

void Text_Update2Line(void);
void Text_Update(void);
const char * Txt_BindLabel(Action_TranslatedText param_1,undefined4 param_2);
void Txt_SetLanguage(uint languageId);
void Txt_LoadLanguage(void);

#endif // TEXT_H_
