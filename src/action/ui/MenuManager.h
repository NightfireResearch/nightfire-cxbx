#ifndef MENUMANAGER_H_
#define MENUMANAGER_H_

#include "../actionhelpers.h"

void MenuManager_Update(void);
void MenuManager_Monitor(void);
uint MenuManager_Create(uint param_1,uint param_2,short param_3,byte playerNum,uint32_t actions,char pauseAudio,uint status);


#endif // MENUMANAGER_H_