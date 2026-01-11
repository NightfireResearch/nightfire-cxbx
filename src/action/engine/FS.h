#ifndef FS_H_
#define FS_H_

#include "../actionhelpers.h"

int FS_MatchFilenameToHeader(char* filename);
void FS_Init(void);
bool FS_StateMachineIterate(void);

#endif // FS_H_