#include "HUD.h"
#include <stdio.h>


// AUTOINJECT
void HUD_Enable(HUDINFO_tag *param_1, HUD_PANE_IND idx, char param_3, ushort param_4) {

  if(param_1 == NULL)
    return;

  if(idx >= NUM_PANES)
    return;

  if (param_1->pane[idx].maybeCanBeEnabled) {
    param_1->pane[idx].maybeEnable2 = param_3;
    param_1->pane[idx].maybeEnable1 = param_4;
  }

}

// AUTOINJECT
void HUD_Reset(BLData *param_1) {
    HUD_Enable(param_1->hudInfo, Sight, '\0', 0);
    HUD_Enable(param_1->hudInfo, NightSight, '\0', 0);
    HUD_Enable(param_1->hudInfo, Air, '\0', 0);
    HUD_Enable(param_1->hudInfo, Redeemer, '\0', 0);
    HUD_Enable(param_1->hudInfo, RCCar, '\0', 0);
    HUD_Enable(param_1->hudInfo, Camera, '\0', 0);
    HUD_Enable(param_1->hudInfo, Blood, '\0', 0);
    HUD_Enable(param_1->hudInfo, Xray, '\0', 0);
    HUD_Enable(param_1->hudInfo, SecCam, '\0', 0);
    HUD_Enable(param_1->hudInfo, OICW, '\0', 0);
    HUD_Enable(param_1->hudInfo, Ronin, '\0', 0);
    HUD_Enable(param_1->hudInfo, Laser, '\0', 0);
    HUD_Enable(param_1->hudInfo, Space, '\0', 0);
}