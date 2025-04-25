#include "HUD.h"
#include "../game/mp/multiplayer.h"
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

// AUTOGEN
void HUD_CreateShrink(BLData *playerInfo,HUDPANE_tag *pane,HUDPANECREATE_tag *param_3,obj_tag *param_4);

#define OICW_timer I16_AT(0x002790b4)
#define OICW_mode U8_AT(0x002790ae)

// AUTOINJECT
void HUD_CreateOICWPane(BLData *playerInfo,HUDPANE_tag *pane,HUDPANECREATE_tag *param_3,obj_tag *param_4) {
  
  HUD_CreateShrink(playerInfo,pane,param_3,param_4);

  if (MPSettings.isMultiplayer) {
    OICW_timer = -555; // 555 timer mentioned - this code was written by an electronics geek?
    OICW_mode = 2;
    return;
  }

  OICW_timer = 150;
  OICW_mode = 0;

  sprintf(pane->spriteList[2]->text,"COMMAND.COM\n");
  Sprite_SetText(pane->spriteList[2], pane->spriteList[2]->text);

  sprintf(pane->spriteList[3]->text,"LOAD BIOS\n");
  Sprite_SetText(pane->spriteList[3], pane->spriteList[3]->text);

  sprintf(pane->spriteList[4]->text,"MEMORY SET\n");
  Sprite_SetText(pane->spriteList[4], pane->spriteList[4]->text);
  
  sprintf(pane->spriteList[5]->text,"SYSTEM STATUS\n");
  Sprite_SetText(pane->spriteList[5],pane->spriteList[5]->text);

  sprintf(pane->spriteList[6]->text,"OK\n");
  Sprite_SetText(pane->spriteList[6],pane->spriteList[6]->text);

}


// AUTOINJECT
void HUD_UpdateCarPane(BLData *playerInfo, HUDPANE_tag *pane, obj_tag *obj) {

	if(!pane->enabled)
		return;
	
	if(playerInfo->remoteControlDevice == NULL)
		return;

	if(playerInfo->remoteControlDevice->objGraphics == hashtable_hashcode_to_celglist(GFX_LittleNellie_Body)) {
		// Helicopter overlay
		pane->spriteList[0]->colourTint = 0x000020ff; // Blue tint
		pane->spriteList[1]->maybeEnabled = 0xff;
		pane->spriteList[2]->maybeEnabled = 0xff;
		pane->spriteList[3]->maybeEnabled = 0x27;
		pane->spriteList[4]->maybeEnabled = 0x27;
		pane->spriteList[5]->maybeEnabled = 0x27;
	} else {
		// Tank overlay
		pane->spriteList[0]->colourTint = 0x002000ff; // Green tint
		pane->spriteList[1]->maybeEnabled = 0x27;
		pane->spriteList[2]->maybeEnabled = 0x27;
		pane->spriteList[3]->maybeEnabled = 0xff;
		pane->spriteList[4]->maybeEnabled =	0xff;
		pane->spriteList[5]->maybeEnabled = 0xff;
	}
}