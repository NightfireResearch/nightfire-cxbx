#include "Mission.h"

#define FailLabel U32_AT(0x0017e548)

// AUTOINJECT
void Mission_SetFailLabel(Action_TranslatedText text) {
    FailLabel = text;
}

