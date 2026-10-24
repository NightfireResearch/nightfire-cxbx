#ifndef UI_PROCESS_H
#define UI_PROCESS_H

#include "ui.h"

void Process_Init(M_CONTROL *control);
void Process_Create(M_CONTROL *control, M_MESSAGE *msg, uint frames, int unused);
void Process_Destroy(M_CONTROL *control);
void Process_RunFrame(M_CONTROL *control);

#endif // UI_PROCESS_H
