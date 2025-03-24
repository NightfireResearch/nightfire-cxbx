#include "HUD.h"
#include <stdio.h>


// AUTOINJECT
void HUD_Enable(HUDINFO_tag *param_1, HUD_PANE_IND idx, char enable, ushort state) {

	if(param_1 == NULL)
		return;

	if(idx >= NUM_PANES)
		return;

	if (param_1->pane[idx].maybeCanBeEnabled) {
		param_1->pane[idx].enabled = enable;
		param_1->pane[idx].state = state;
	}

}

// AUTOINJECT
void HUD_Reset(BLData *param_1) {
	HUD_Enable(param_1->hudInfo, Sight, 0, 0);
	HUD_Enable(param_1->hudInfo, NightSight, 0, 0);
	HUD_Enable(param_1->hudInfo, Air, 0, 0);
	HUD_Enable(param_1->hudInfo, Redeemer, 0, 0);
	HUD_Enable(param_1->hudInfo, RCCar, 0, 0);
	HUD_Enable(param_1->hudInfo, Camera, 0, 0);
	HUD_Enable(param_1->hudInfo, Blood, 0, 0);
	HUD_Enable(param_1->hudInfo, Xray, 0, 0);
	HUD_Enable(param_1->hudInfo, SecCam, 0, 0);
	HUD_Enable(param_1->hudInfo, OICW, 0, 0);
	HUD_Enable(param_1->hudInfo, Ronin, 0, 0);
	HUD_Enable(param_1->hudInfo, Laser, 0, 0);
	HUD_Enable(param_1->hudInfo, Space, 0, 0);
}

// AUTOINJECT
void HUD_DisableAll(BLData *param_1) {
	for(int i = 0; i < NUM_PANES; i++) {
	  	param_1->hudInfo->pane[i].enabled = 0;
	}
}

// AUTOINJECT
ushort HUD_State(HUDINFO_tag *param_1, HUD_PANE_IND idx) {

	if(param_1 == NULL || idx >= NUM_PANES)
		return 0;

	return param_1->pane[idx].state;

}
