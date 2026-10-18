#ifndef MENUMANAGER_H_
#define MENUMANAGER_H_

#include "ui.h"

void MenuManager_Init(void);
uint MenuManager_Create(uint menu, uint startPage, short player, uchar managerNum, uint closeAction, char pauseAudio,
                        uint status);
void MenuManager_Delete(uchar managerNum);
void MenuManager_Update(void);
void MenuManager_Monitor(void);
void MenuManager_Load(uint hashcode, uint *data);
uchar MenuManager_Exists(uchar managerNum);
uint MenuManager_GetStatus(void);
void MenuManager_Enable(bool enable, uchar managerNum);
uchar Menu_PlayerHasQuitOrRestarted(void);

// MenuManager_Create's answer when it makes nothing
#define MENU_NONE 0xffffffffu
// A manager's closeAction when no action closes it
#define MENU_NO_CLOSE_ACTION 0x28

#endif // MENUMANAGER_H_
