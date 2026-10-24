#ifndef LOCKS_H_
#define LOCKS_H_

#include "../../actionhelpers.h"

typedef struct {
    char asciiDigit[4];
    bool discovered;
} KeyCodeEntry;

extern KeyCodeEntry KeyCodes[51];

void Locks_Init(void);

#endif // LOCKS_H_
