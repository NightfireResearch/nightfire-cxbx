#ifndef MISSION_H
#define MISSION_H

#include "../../actionhelpers.h"


void Mission_SetFailLabel(Action_TranslatedText text);
void Mission_SetMapHCode(HASHCODE param_1);
HASHCODE Mission_BaseMapHCode(void);
void Mission_SetStatus(undefined4 param_1);
undefined4 Mission_Status(void);




#endif // MISSION_H