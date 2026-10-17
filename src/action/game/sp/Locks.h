#ifndef LOCKS_H_
#define LOCKS_H_

#include "../../actionhelpers.h"


typedef struct {
    char asciiDigit[4];
    bool discovered;
} KeyCodeEntry;

// XBE_GLOBAL(0x0029aaf8, 0xff)
#define KeyCodes (*(KeyCodeEntry (*)[51])0x0029aaf8)



void Locks_Init(void);


#endif // LOCKS_H_