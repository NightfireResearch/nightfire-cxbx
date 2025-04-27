#include "Mission.h"

#define FailLabel U32_AT(0x0017e548)
#define BaseMap U32_AT(0x0025fe20)

// AUTOINJECT
void Mission_SetFailLabel(Action_TranslatedText text) {
    FailLabel = text;
}

// AUTOINJECT
void Mission_SetMapHCode(undefined4 param_1) {
  BaseMap = param_1;
}