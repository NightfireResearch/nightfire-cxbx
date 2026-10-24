#ifndef MENUMANAGER_H_
#define MENUMANAGER_H_

#include "ui.h"

// A manager's status (MenuManager_Create's last argument); MenuManager_GetStatus answers the highest
enum MenuStatus {
    MENU_STATUS_NONE,
    MENU_STATUS_KEYPAD,
    MENU_STATUS_CONTROLLER,     // the pad is gone
    MENU_STATUS_PAUSE,
    MENU_STATUS_MISSION_END,
    MENU_STATUS_FRONT_END,
};

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
