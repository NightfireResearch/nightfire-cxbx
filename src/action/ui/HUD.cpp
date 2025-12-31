#include "HUD.h"
#include "../input.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/bullet.h"
#include "../game/obj/Copter.h"
#include "../engine/viewer.h"
#include "../memory.h"
#include <stdio.h>
#include <string.h>
#include "../game/view.h"


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

#pragma pack(push, 1)
typedef struct {
	ushort spritesheetX; // position in a sprite sheet?
	ushort spritesheetY;
	short width;
	short height;
	char padding[10]; // 0 in all instances, and can't see this ever being referenced
} CrosshairInfo;
#pragma pack(pop)
static_assert(sizeof(CrosshairInfo) == 18, "CrosshairInfo size wrong");

// Several unused crosshairs in the array...
// 0: none (punch)
// 1: zoomed
// 2: standard
// 3: civilian (disallowed target)
// 4: door
// 5: disallowed door?
// 6: rectangle with arrow up through it
// 7: 6 but with disallowed circle/strikethrough
// 8: camera
#define HUDCrossCoords (*(CrosshairInfo(*)[9])(0x00181348))

// UNINJECTABLE - custom calling convention
void HUD_UpdateCrossHair(BLData *player,sprite *spr) {
	
	if(spr == NULL)
		return;
	
	if(player->hudInfo->pane[Blood].enabled || player->hudInfo->pane[SecCam].enabled) {
		// Hide the crosshair when player is dead or watching a security camera feed
		spr->maybeEnabled = 0xff;
		return;
	}

	viewer_tag *v = glb_viewer[player->playerNum];

	// Turn off the special weapon/gadget sight panes
	HUD_Enable(player->hudInfo, Sight, 0, 0);
	HUD_Enable(player->hudInfo, Camera, 0, 0);
	HUD_Enable(player->hudInfo, OICW, 0, 0);
	HUD_Enable(player->hudInfo, Laser, 0, 0);
	

	int crosshairIdx = player->crosshairType;

	// Turn on if we're using the specific weapons
	bool weaponZoomedIn = (glb_players[player->playerNum]->animState->animFlags & 1);
	if(weaponZoomedIn) {
		int weaponId = glb_players[player->playerNum]->animState->currentWeaponId;
		if(weapon_data[weaponId].someFlags & 0x40) { // Custom crosshair pane?
			switch(weaponId) {
				case 0x1a:
				case 0x1b:
					// OICW
					HUD_Enable(player->hudInfo, OICW, 1, 0);
					spr->maybeEnabled = 0xff;
					return;
				case 0x32:
				case 0x33:
					// Laser
					HUD_Enable(player->hudInfo, Laser, 1, 0);
					spr->maybeEnabled = 0xff;
					return;
				case Weap_Camera_Upgraded:
					glb_players[player->playerNum]->animState->currentWeaponId = 0x54;
					glb_players[player->playerNum]->animState->switchingToWeaponId = 0x54;
					// Intentional fallthrough
				case Weap_Camera:
					HUD_Enable(player->hudInfo, Camera, 1, 0);
					spr->maybeEnabled = 0xff;
					return;
				default:
					HUD_Enable(player->hudInfo, Sight, 1, 0);
					spr->maybeEnabled = 0xff;
					return;
			}
		}
		crosshairIdx = 1; // Zoomed-in but no custom pane; use the standard zoomed crosshair
	}

	CrosshairInfo *curCrosshair = &HUDCrossCoords[crosshairIdx];
	spr->spritesheetX = curCrosshair->spritesheetX;
	spr->spritesheetY = curCrosshair->spritesheetY;
	spr->onscreenWidth = curCrosshair->width;
	spr->onscreenHeight = curCrosshair->height;
	spr->spritesheetWidth = curCrosshair->width;
	spr->spritesheetHeight = curCrosshair->height;
	
	float posX = player->crosshairOffsetX * v->width * 0.5f + v->width * 0.5f;
	float posY = -player->crosshairOffsetY * v->height * 0.5f + v->height * 0.5f;
	spr->positionX = (short)posX;
	spr->positionY = (short)posY;

	spr->colourTint =  (v->nightVisionRelated == 1 ? 0x208020ff : 0xff0000ff);

	spr->maybeEnabled = 0x31;

	// Override if the player has configured to hide the crosshair
	if(!PlayerInputs[player->playerNum].crosshairsEnabled)
		spr->maybeEnabled = 0xff;
	
	// Override in some cam mode?
	if(player->camMode)
		spr->maybeEnabled = 0xff;
	
}

// UNINJECTABLE - custom calling convention
void HUD_MonitorNightSight(BLData *player) {

	if(player == NULL)
		return;

	viewer_tag* vwr = glb_viewer[player->playerNum];
	obj_tag* obj = glb_players[player->playerNum];

	if(player->someNightVisionThing == NULL)
		return;

	if(obj->curState != 1)
		return;

	if(MPSettings.isMultiplayer)
		return;

	if(obj->objectType == OBJECTTYPE_DEAD_PLAYER)
		return;

	switch(obj->subState) {
		case MovementType_Walk: 	// == 0
		case MovementType_Swim: 	// == 3
		case MovementType_Crouch: 	// == 4
		case MovementType_Decoding: // == 5
			break;
		default: // Everything else (not 0, and either < 3 or > 5)
			return;
	}

	if(vwr == NULL)
		return;

	if( (obj->animState->currentWeaponId == Weap_MaybeNightvision) && (player->weaponObject->curState == 0) ) {	
		
		vwr->nightVisionRelated = 1;
		HUD_Enable(player->hudInfo, NightSight, 1, 0);
		obj->animState->switchingToWeaponId = obj->animState->prevHeldWeaponId;
		player->nightVisionActive = 1;

		int otherId = obj->animState->switchingToWeaponId;
		if((otherId == 0x47) || ((0x5c < otherId) && (otherId < 0x5f))) {
			obj->animState->switchingToWeaponId = 1;
			return;
		}
	

	} else {
	
		if(Input_Action(player->playerNum, ACTION_NIGHTVISION, 4) && !(obj->animState->animFlags & 1)) { // Activate night sight button pressed - cycle modes

			if(!player->nightVisionActive) {
				obj->animState->switchingToWeaponId = 0x5d;
			} else {

				switch(vwr->nightVisionRelated) {
					case 0:
						vwr->nightVisionRelated = 1;
						HUD_Enable(player->hudInfo, NightSight, 1, 0);
						break;
					case 1:
						vwr->nightVisionRelated = 2;
						HUD_Enable(player->hudInfo, NightSight, 0, 0);
						HUD_Enable(player->hudInfo, Xray, 1, 0);
						break;
					case 2:
						vwr->nightVisionRelated = 0;
						HUD_Enable(player->hudInfo, NightSight, 0, 0);
						HUD_Enable(player->hudInfo, Xray, 0, 0);
						break;
				}

			}

		}

		// Recharge if the goggles are off
		if(vwr->nightVisionRelated == 0) {
			// Recharge to a maximum of 1800 frames (30 seconds)
			player->nightVisionTimer += FRAME_RATE_MUL * 3;
			if(player->nightVisionTimer > 1800) 
				player->nightVisionTimer = 1800;
		} else {
			// Drain the battery if the goggles are on
			player->nightVisionTimer -= FRAME_RATE_MUL;
		}
 
		// If the battery is depleted, turn off night sight and xray and enter recharge state
		if(player->nightVisionTimer <= 0) {
			player->nightVisionTimer = 0;
			vwr->nightVisionRelated = 0;
			HUD_Enable(player->hudInfo, NightSight, 0, 0);
			HUD_Enable(player->hudInfo, Xray, 0, 0);
		}

	}

}

// AUTOINJECT
void HUD_Update(BLData *playerInfo, obj_tag *obj) {

	if(playerInfo == NULL || obj == NULL || playerInfo->hudInfo == NULL)
		return;

	HUD_UpdateCrossHair(playerInfo, playerInfo->hudInfo->crosshairSprite);
	HUD_MonitorNightSight(playerInfo);

	for(int i = 0; i < NUM_PANES; i++) {
	
		HUDPANE_tag *pane = &(playerInfo->hudInfo->pane[i]);

		if(!pane->enabled) {
			// Hide all sprites on this pane
			for(int j = 0; j < pane->numSprites; j++) {
				pane->spriteList[j]->maybeEnabled = 0xff;
			}
		} else {
			// Copy the sprite enablement state from the base pane
			for(int j = 0; j < pane->base->numSprites; j++) {
				pane->spriteList[j]->maybeEnabled = pane->base->spriteInfo[j].maybeEnabled;
			}
		}
		// Run the update function (regardless of whether it's enabled)
		if((*pane->updateFunction) != NULL)
			(*pane->updateFunction)(playerInfo, pane, obj);

	}
}

// AUTOGEN
void HUD_CalcWidthHeight(HUDPANECREATE_tag *param_1, short *param_2, short *param_3);

// AUTOGEN
void HUD_CreateDefault(BLData *blData, HUDPANE_tag *hudPane, HUDPANECREATE_tag *hudPaneCreate, obj_tag *unused);

// AUTOINJECT
void HUD_CreateShrink(BLData *playerInfo, HUDPANE_tag *pane, HUDPANECREATE_tag *param_3, obj_tag *param_4) {
	  
	HUD_CalcWidthHeight(param_3, &param_3->width, &param_3->height);
	HUD_CreateDefault(playerInfo, pane, param_3, param_4);
	viewer_tag *vwr = glb_viewer[playerInfo->playerNum];

	for(int i = 0; i < pane->numSprites; i++) {

		sprite* s = pane->spriteList[i];

		// If player's view is compressed horizontally (MP view), shrink width and move it horizontally
		// Division by 2 is just an assumption from the fact that MP is restricted to a 2x2 grid of views
		if (vwr->width < (float)SCREEN_WIDTH) {
			s->onscreenWidth = s->onscreenWidth / 2;
			s->positionX -= param_3->spriteInfo[i].posX / 2;
		}

		// If player's view is less than fullscreen vertically (MP view), shrink height and move it vertically
		// Division by 2 is just an assumption from the fact that MP is restricted to a 2x2 grid of views
		if (vwr->height < (float)SCREEN_HEIGHT) {
			s->onscreenHeight = s->onscreenHeight / 2;
			s->positionY -= param_3->spriteInfo[i].posY / 2;
		}
	}
	
}

#define CrossHair (*(SpriteInfo*)0x0017f778)
#define ttimer I16_AT(0x002790b8)
#define ctimer I16_AT(0x002790bc)


#define MsgMissionStatusPane ((HUDPANECREATE_tag*)(0x0017fea8))
#define MsgObjectiveStatusPane ((HUDPANECREATE_tag*)(0x001813ec))
#define MsgInfoStatusPane ((HUDPANECREATE_tag*)(0x00181598))
#define AirPane ((HUDPANECREATE_tag*)(0x0017fe8c))
#define NightSightPane ((HUDPANECREATE_tag*)(0x00180218))
#define LensFlarePane ((HUDPANECREATE_tag*)(0x0018036c))
#define RCCarPane ((HUDPANECREATE_tag*)(0x00180690))
#define CameraPane ((HUDPANECREATE_tag*)(0x001800f4))
#define XrayPane ((HUDPANECREATE_tag*)(0x001807d4))
#define SecCamPane ((HUDPANECREATE_tag*)(0x00180924))
#define OICWPane ((HUDPANECREATE_tag*)(0x00180a74))
#define RoninPane ((HUDPANECREATE_tag*)(0x00180b98))
#define LaserPane ((HUDPANECREATE_tag*)(0x00180cc0))
#define SpacePane ((HUDPANECREATE_tag*)(0x00180ec4))
#define MsgPickupStatusPane ((HUDPANECREATE_tag*)(0x00181774))


// AUTOGEN
void HUD_CreateRedeemer(BLData* blData, HUDPANE_tag *hudPane, HUDPANECREATE_tag *paneCreate, obj_tag *obj);

// TODO: Change from 640x480 to generic
SpriteInfo RedeemerSpriteInfo[] = {
	{0x7f7f7f78, 0x7f7f7fff, 0x0022, 0x0500, 256, 	176, 	128, 			128, 			1, 	1, 	127, 	127, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_RLAUNCH_UI_5E, 5, 0, 0, 0}, // 0: Central reticle. The image is only 1/4 of the whole - repeats mirrored in H and V?
	{0xffffff40, 0x7f7f7fff, 0x0022, 0x0100, 0,		0,		SCREEN_WIDTH, 	SCREEN_HEIGHT,	0, 	0, 	127, 	127, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_RLAUNCH_UI_4A, 5, 0, 0, 0}, // 1: Static lines
	{0x7f7f7fff, 0x7f7f7fff, 0xffff, 0x0200, 0,		0,		SCREEN_WIDTH,	SCREEN_HEIGHT, 	0, 	0, 	0, 		0,		Action_TranslatedText_NULLVALUE, NULL, SPRITE_COLOUR_FILL, 5, 0, 0, 0}, // 2: Blackout on missile destroyed
	{0x7f7f7f78, 0x7f7f7fff, 0x0022, 0x0100, 0,		144, 	0,				0,				0, 	0, 	0,		0,		Action_TranslatedText_NULLVALUE, NULL, SPRITE_RLAUNCH_UI_5F, 5, 0, 0, 0},
	{0x7f7f7f78, 0x7f7f7fff, 0x0022, 0x0140, 384, 	144,	0,				0,				0, 	0, 	0,		0, 		Action_TranslatedText_NULLVALUE, NULL, SPRITE_RLAUNCH_UI_5F, 5, 0, 0, 0}, // Flags mean just mirrored?
	{0x7f7f7f78, 0x7f7f7fff, 0x0022, 0x0100, 0, 	336, 	0, 				0, 				0, 	0, 	0,		0,		Action_TranslatedText_NULLVALUE, NULL, SPRITE_RLAUNCH_UI_5C, 5, 0, 0, 0},
	{0x7f7f7f78, 0x7f7f7fff, 0x0022, 0x0140, 384, 	336, 	0, 				0, 				0, 	0,	0,		0,		Action_TranslatedText_NULLVALUE, NULL, SPRITE_RLAUNCH_UI_5C, 5, 0, 0, 0},
	{0x7f7f7f78, 0x7f7f7fff, 0x0022, 0x0100, 174, 	282, 	0,				0,				0,	0,	0,		0,		Action_TranslatedText_NULLVALUE, NULL, SPRITE_RLAUNCH_UI_5D, 5, 0, 0, 0},
	{0x7f7f7f78, 0x7f7f7fff, 0x0022, 0x0140, 402,	282,	0,				0,				0,	0,	0,		0,		Action_TranslatedText_NULLVALUE, NULL, SPRITE_RLAUNCH_UI_5D, 5, 0, 0, 0},
	{0x300000ff, 0x7f7f7fff, 0x0023, 0x0100, 0,		0,		SCREEN_WIDTH,	SCREEN_HEIGHT, 	0,	0,	0,		0,		Action_TranslatedText_NULLVALUE, NULL, SPRITE_COLOUR_FILL, 5, 0, 0, 0}, // 9: Red colour tint?
	{0x7f7f7f78, 0x7f7f7fff, 0x0022, 0x0500, 256, 	176, 	128,			128, 			1, 	1, 	127, 	127, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_RLAUNCH_UI_59, 5, 0, 0, 0} // 10: Target designator on chopper. The image is only 1/4 of the whole - repeats mirrored in H and V?
};

HUDPANECREATE_tag RedeemerPane = {
	0,
	0,
	SCREEN_WIDTH,
	SCREEN_HEIGHT,
	HUD_CreateRedeemer,
	HUD_UpdateRedeemerPane,
	RedeemerSpriteInfo,
	ARRAY_SIZE(RedeemerSpriteInfo),
	4,
	0x180, // 384
	0, // Unused?
	0 // Unused?
};



SpriteInfo BloodSprInfo[] = {
	{0x007f7fff, 0x7f7f7fff, 0x0009, 0x0200, 0, 	0, 		SCREEN_WIDTH, 	32,	0, 	0, 	0, 	0, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_BLOOD_DRIP, 5, 0, 0, 0}, // Drippy edge
	{0x007f7fff, 0x7f7f7fff, 0x0009, 0x0200, 0,		0,		SCREEN_WIDTH, 	64,	0, 	0, 	0, 	0, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_COLOUR_FILL, 5, 0, 0, 0}, // Colour fill
};

// AUTOGEN
void HUD_UpdateBloodPane(BLData *blData, HUDPANE_tag *hudPane, obj_tag *gameObj);

HUDPANECREATE_tag BloodPane = {
	0,
	0,
	SCREEN_WIDTH,
	SCREEN_HEIGHT,
	HUD_CreateShrink,
	HUD_UpdateBloodPane,
	BloodSprInfo,
	ARRAY_SIZE(BloodSprInfo),
	0,
	0x180, // 384
	0, // Unused?
	0 // Unused?
};


SpriteInfo SightSprInfo[] = { // Left bar, Scope (square fitted to SCREEN_HEIGHT), Right bar
	{0x7f7f7fff, 0x7f7f7fff, 0x0027, 0x0600, (SCREEN_WIDTH-SCREEN_HEIGHT)/2, 	0, 		SCREEN_HEIGHT, 						SCREEN_HEIGHT,	0, 	0, 	0x1FF,	0x1FF, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_SNIPER_SCOPE, 5, 0, 0, 0}, // Scope graphic centre
	{0x7f7f7fff, 0x7f7f7fff, 0x0027, 0x0200, 0,									0,		(SCREEN_WIDTH-SCREEN_HEIGHT)/2, 	SCREEN_HEIGHT,	0, 	0, 	0, 		0, 		Action_TranslatedText_NULLVALUE, NULL, SPRITE_COLOUR_FILL, 5, 0, 0, 0}, // Fill left
	{0x7f7f7fff, 0x7f7f7fff, 0x0027, 0x0200, (SCREEN_WIDTH+SCREEN_HEIGHT)/2,	0,		(SCREEN_WIDTH-SCREEN_HEIGHT)/2, 	SCREEN_HEIGHT,	0, 	0, 	0, 		0, 		Action_TranslatedText_NULLVALUE, NULL, SPRITE_COLOUR_FILL, 5, 0, 0, 0}, // Fill right
};

HUDPANECREATE_tag SightPane = {
	0,
	0,
	SCREEN_WIDTH,
	SCREEN_HEIGHT,
	HUD_CreateShrink,
	NULL, // Game uses a dummy function here but NULL is valid too
	SightSprInfo,
	ARRAY_SIZE(SightSprInfo),
	0,
	0x180,
	0,
	0
};

// AUTOINJECT
void HUD_CreateAmmoPane(BLData* blData, HUDPANE_tag *hudPane, HUDPANECREATE_tag *paneCreate, obj_tag *obj) {
	HUD_CalcWidthHeight(paneCreate, &paneCreate->width, &paneCreate->height);
	HUD_CreateDefault(blData, hudPane, paneCreate, obj);
}

// AUTOGEN
void HUD_UpdateAmmoPane(BLData *blData, HUDPANE_tag *hudPane, obj_tag *gameObj);

// Anchor point is bottom-right, hence negative coordinates
// Also, all un-tested
// SpriteInfo AmmoSprInfo[] = {
// 	{0x7d6d59ff, 0x000000ff, 0x001c, 0x1020, 0, 	18, 	0, 	0,	0, 	0, 	0,	0, 	Action_TranslatedText_NULLVALUE, (char*)0x161a88, (HASHCODE)0, 5, 0, 0, 0},
// 	{0x7d6d59ff, 0x000000ff, 0x001c, 0x0020, 0, 	38, 	0, 	0,	0, 	0, 	0,	0, 	Action_TranslatedText_NULLVALUE, (char*)0x161a88, (HASHCODE)0, 5, 0, 0, 0},
// 	{0x7d6d59ff, 0x000000ff, 0x001c, 0x1020, 0, 	38, 	0, 	0,	0, 	0, 	0,	0, 	Action_TranslatedText_NULLVALUE, (char*)0x161a88, (HASHCODE)0, 5, 0, 0, 0},
// 	{0x7d6d59ff, 0x000000ff, 0xffff, 0x1020, -44, 	0, 		0, 	0,	0, 	0, 	0,	0, 	Action_TranslatedText_NULLVALUE, (char*)0x161a84, (HASHCODE)0, 5, 0, 0, 0},
// 	{0x14507fff, 0x7f7f7fff, 0xffff, 0x1000, -62, 	-20,	68,	22,	0, 	0, 	0,	0, 	Action_TranslatedText_NULLVALUE, NULL, 			   SPRITE_COLOUR_FILL, 5, 0, 0, 0},
// 	{0x7f7f7fff, 0x7f7f7fff, 0x001c, 0x1000, -25,	-64, 	25, 64, 230,0, 	25, 64, Action_TranslatedText_NULLVALUE, NULL, 			  (HASHCODE)0x03000128, 5, 0, 0, 0},
// 	{0x4040407f, 0x7f7f7f7f, 0x001d, 0x1000, -25, 	-64, 	25, 64, 230,0,	25, 64, Action_TranslatedText_NULLVALUE, NULL, 			  (HASHCODE)0x03000128, 5, 0, 0, 0},
// 	{0xffffffff, 0x7f7f7fff, 0x001f, 0x0900, 0,		0,		0,	0,	0,  0,	0,	0,	Action_TranslatedText_NULLVALUE, NULL, 			  TEX_CROSSHAIR_SAMURAI, 5, 0, 0, 0}
// };

HUDPANECREATE_tag AmmoPane = {
	SCREEN_WIDTH, // Anchor point in the bottom-right
	SCREEN_HEIGHT,
	0,
	0,
	HUD_CreateAmmoPane,
	HUD_UpdateAmmoPane,
	// Injecting seems to break stuff? Something about text injection / we need to implement HUD_UpdateAmmoPane too?
	//AmmoSprInfo,
	//ARRAY_SIZE(AmmoSprInfo),
	(SpriteInfo*)0x0017f7d0,
	8,
	4,
	0x18,
	0,
	0
};

// AUTOGEN
void HUD_CreateHealthPane(BLData* blData, HUDPANE_tag *hudPane, HUDPANECREATE_tag *paneCreate, obj_tag *obj);

// AUTOGEN
void HUD_UpdateHealthPane(BLData *blData, HUDPANE_tag *hudPane, obj_tag *gameObj);

HUDPANECREATE_tag HealthPane = {
	50,
	SCREEN_HEIGHT - 135,//345,
	0,
	0,
	HUD_CreateHealthPane,
	HUD_UpdateHealthPane,
	// Injecting seems to break stuff? Something about text injection / we need to implement HUD_UpdateAmmoPane too?
	//AmmoSprInfo,
	//ARRAY_SIZE(AmmoSprInfo),
	(SpriteInfo*)0x0017f9c8,
	24,
	0,
	0,
	0,
	0
};


HUDPANECREATE_tag* PaneList[] = {
	&AmmoPane,
	&HealthPane,
	MsgMissionStatusPane,
	MsgObjectiveStatusPane,
	MsgInfoStatusPane,
	AirPane, // Oxygen/Swimming indicator?
	&SightPane,
	NightSightPane,
	LensFlarePane,
	&RedeemerPane,
	RCCarPane,
	CameraPane,
	&BloodPane,
	NULL,
	NULL,
	XrayPane,
	SecCamPane,
	OICWPane,
	RoninPane,
	LaserPane,
	SpacePane,
	MsgPickupStatusPane
};

static_assert(ARRAY_SIZE(PaneList) == NUM_PANES, "Bad size of pane list");


#define MPAmmoPane ((HUDPANECREATE_tag*)(0x00180fbc))
#define MPHealthPane ((HUDPANECREATE_tag*)(0x00181138))
#define MPMsgInfoStatusPane ((HUDPANECREATE_tag*)(0x00181180))
#define MPScorePane ((HUDPANECREATE_tag*)(0x001812d4))
#define RadarPane ((HUDPANECREATE_tag*)(0x001806d8))

HUDPANECREATE_tag * MPPaneList[] = {
	MPAmmoPane,
	MPHealthPane,
	NULL,
	NULL,
	MPMsgInfoStatusPane,
	NULL,
	&SightPane,
	NULL,
	NULL,
	&RedeemerPane,
	RCCarPane,
	NULL,
	&BloodPane,
	MPScorePane,
	RadarPane,
	NULL,
	NULL,
	OICWPane,
	RoninPane,
	LaserPane,
	NULL,
	NULL
};

static_assert(ARRAY_SIZE(MPPaneList) == NUM_PANES, "Bad size of pane list");


// AUTOINJECT
void HUD_Init(BLData *player, obj_tag *obj) {
	
	if((obj != NULL) && (obj->objectType == OBJECTTYPE_DRONE || obj->objectType == OBJECTTYPE_DEAD_DRONE))
		return;

	player->hudInfo = (HUDINFO_tag*)Mem_Malloc(sizeof(HUDINFO_tag), 0x2704, 0);

	if(player->hudInfo == NULL)
		return;

	memset(player->hudInfo->pane, 0, sizeof(HUDPANE_tag) * NUM_PANES);

	// These 4 lines are probably not needed?!
	player->hudInfo->maybeUnused = 0;
	ttimer = 0;
	ctimer = 0;
	player->hudInfo->crosshairSprite = NULL;


	player->hudInfo->crosshairSprite = Sprite_Create2(&CrossHair);
	if(player->hudInfo->crosshairSprite != NULL)
		Sprite_Link2Viewer(player->hudInfo->crosshairSprite, player->playerNum);

	
	for(int i = 0; i < NUM_PANES; i++) {
		
		HUDPANECREATE_tag *paneCreate = (MPSettings.isMultiplayer ? MPPaneList[i] : PaneList[i]);

		if((paneCreate == NULL) || (paneCreate->createFunc == NULL)) {
			player->hudInfo->pane[i].enabled = 0;
			player->hudInfo->pane[i].maybeCanBeEnabled = 0;
			continue;
		}

		//printf("Processing pane %i, paneCreate = %p, createFunc = %p\n", i, paneCreate, paneCreate->createFunc);

		player->hudInfo->pane[i].enabled = 1;
		player->hudInfo->pane[i].maybeCanBeEnabled = 1;
		player->hudInfo->pane[i].field_0x1d = 0;
		player->hudInfo->pane[i].field_0x1e = 0;
		player->hudInfo->pane[i].base = paneCreate;

		// Call the create function
		(*paneCreate->createFunc)(player, &(player->hudInfo->pane[i]), paneCreate, obj);

		// If possible, also call the update function
		if((*paneCreate->updateFunc) != NULL)
			(*paneCreate->updateFunc)(player, &(player->hudInfo->pane[i]), obj);


	}

	HUD_Reset(player);
	
	
}

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
void HUD_UpdateOICWPane(BLData *playerInfo, HUDPANE_tag *pane, obj_tag *obj) {

	// Fancy animation only in singleplayer
	if(MPSettings.isMultiplayer)
		return;

	if(!pane->enabled) { // Pane closed?
		// Reset the "startup" sequence, unless we fully booted
		if(OICW_mode != 2) {
			OICW_mode = 0;
			OICW_timer = 150;
		}
		// No need to update anything else
		return;
	}

	glb_viewer[playerInfo->playerNum]->nightVisionRelated = 0;

	// Process the "startup" sequence
	switch(OICW_mode) {
		case 0:
		{
			// Phase 1: Make a new line visible every 30 frames
			OICW_timer--;

			// Enable the relevant lines
			int numVisibleLines = 5 - (OICW_timer / 30);
			int linePositionY = 440.0f - (numVisibleLines * 17); // FIXME: Depends on screen resolution vertically. PS2 uses 472, Xbox uses 440 (512x512 vs 640x480)
			for(int i = 0; i < numVisibleLines; i++) {
				// TODO: Enable the relevant lines
				pane->spriteList[i+2]->positionX = 0x32;
				pane->spriteList[i+2]->positionY = linePositionY;
				linePositionY += 17;
				pane->spriteList[i+2]->maybeEnabled = 0x1c;

				// 2nd line flickers slowly
				if(i == 1 && numVisibleLines == 2) {
					if(OICW_timer % 8 < 4) {
						pane->spriteList[i+2]->maybeEnabled = 0xff;
					}
				}
			}

			if(OICW_timer <= 0) {
				// Advance to phase 2 - all entries flicker fast
				OICW_mode = 1;
				OICW_timer = 30;
			}

			break;
		}
		case 1:
		{
			// Phase 2: All entries flicker
			OICW_timer--;
			int value = (OICW_timer % 4 < 2) ? 0xff: 0x1c;
			for(int i = 0; i < 5; i++) {
				pane->spriteList[i+2]->maybeEnabled = value;
			}

			if(OICW_timer <= 0) {
				// Advance to phase 3
				OICW_mode = 2;
			}

			break;
		}
		case 2:
		{
			// Fully booted - hide all the labels
			pane->spriteList[2]->maybeEnabled = 0xff;
			pane->spriteList[3]->maybeEnabled = 0xff;
			pane->spriteList[4]->maybeEnabled = 0xff;
			pane->spriteList[5]->maybeEnabled = 0xff;
			pane->spriteList[6]->maybeEnabled = 0xff;
			break;
		}
	}
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


#define MissileDeploy (*(uchar(*)[8])(0x0029a28c))


// AUTOINJECT
void HUD_UpdateSpacePane(BLData *param_1, HUDPANE_tag *pane, obj_tag *unused) {

	if(GameState.CurrentLevelHashcode != HT_Level_SpaceStationD) {
		pane->enabled = false;
		return;
	}

	pane->enabled = true;
	bool blink_state = (GameState.NumFramesUnpaused % (FRAME_RATE_INT >> 1) < FRAME_RATE_INT >> 2);

	for(int i = 0; i < 8; i++) {
		
		uchar deployState = MissileDeploy[i];
		sprite* spr = pane->spriteList[i+1];
		bool switchActive = switch_channels[i+1];
		
		// Deploying - solid dot sprite, flashing green/white
		if(deployState == 1) {
			hashtable_set_sprite(spr, (HASHCODE)0x3000177);
			spr->colourTint = blink_state ? 0x00ff00ff : 0xffffffff;
		}

		// Destroyed - cross sprite, solid red
		if (switchActive && deployState != 3) {
			hashtable_set_sprite(spr, (HASHCODE)0x300017a);
			// Bright red when not yet fired, dimmer red once fired
			spr->colourTint = (deployState == 2) ? 0xff000080: 0xff0000ff;
		}

		// Failed to disrupt the launch - solid green, faded a bit
		if (deployState == 3) {
			hashtable_set_sprite(spr, (HASHCODE)0x3000177);
			spr->colourTint = 0x00ff0080;
		}
	}

	// Game won - space station flashes red/white
	if(switch_channels[9]) {
		pane->spriteList[0]->colourTint = blink_state ? 0xff000080 : 0x7f7f7f7f;
	}

	// Left spawner door - flash red when active
	if(switch_channels[24]) {
		pane->spriteList[9]->colourTint = blink_state ? 0xff000080: 0x7f7f7f7f;
	} else {
		pane->spriteList[9]->colourTint = 0x7f7f7f7f;
	}

	// Right spawner door - flash red when active
	if(switch_channels[28]) {
		pane->spriteList[10]->colourTint = blink_state ? 0xff000080 : 0x7f7f7f7f;
	} else {
		pane->spriteList[10]->colourTint = 0x7f7f7f7f;
	}
	
}

// AUTOINJECT
void HUD_UpdateRedeemerPane(BLData *blData, HUDPANE_tag *hudPane, obj_tag *gameObj) {

	if(gameObj != NULL) {
		// Only players need the hud pane to be updated, not drones
		if(gameObj->objectType == OBJECTTYPE_DRONE || gameObj->objectType == OBJECTTYPE_DEAD_DRONE)
			return;
	}

	// extraItems is a pointer to some memory, which contains an array of obj_tag* pointers
	obj_tag** extraItemsAsObjList = (obj_tag**)hudPane->extraItems;

	// Redeemer elements are actually game objects not just 2D sprites. This might be so that we can rotate elements, rather than just position them?
	// These will only be created in single player mode, MP uses more basic graphics (performance, or reduced visual clutter?)
	if(!hudPane->enabled) {

		if(extraItemsAsObjList[0] != NULL)
			extraItemsAsObjList[0]->effectFlags |= 0x10;

		if(extraItemsAsObjList[1] != NULL)
			extraItemsAsObjList[1]->effectFlags |= 0x10;

		if(extraItemsAsObjList[2] != NULL)
			extraItemsAsObjList[2]->effectFlags |= 0x10;

		return;
	}

	// If we make it past here, the pane is enabled
	
	if(extraItemsAsObjList[0] != NULL)
		extraItemsAsObjList[0]->effectFlags &= 0xffffffef;

	if(extraItemsAsObjList[1] != NULL)
		extraItemsAsObjList[1]->effectFlags &= 0xffffffef;

	if(extraItemsAsObjList[2] != NULL)
		extraItemsAsObjList[2]->effectFlags &= 0xffffffef;


	// Return control if the player exits the remote-controlling substate
	if(gameObj->subState != MovementType_RemoteControl) {
		Player_SetCamMode(blData, 0);
		glb_viewer[blData->playerNum]->someCel = NULL;
		if(		blData->hudInfo != NULL 
			&& 	blData->hudInfo->pane[Redeemer].maybeCanBeEnabled) {
			
			blData->hudInfo->pane[Redeemer].enabled = false;
			blData->hudInfo->pane[Redeemer].state = 0;
		
		}
	}

	// Put targeting element over a copter if one exists
	hudPane->spriteList[10]->maybeEnabled = 0xff; // Target marker overlay, shown on copter body
	for(COPTER *c = (COPTER*)(CopterList.head); c != NULL; c = (COPTER*)c->node.next) {

		
		obj_tag *body = Copter_GetBody(c);

		// Not spawned, no action
		if(body == NULL)
			continue;

		// If it's behind us or offscreen, no action
		_VECTOR tmp;
		if(View_3DPoint2Screen(&body->position, &tmp, glb_viewer[blData->playerNum]->idx) <= 0)
			continue;

		if(tmp.x > SCREEN_WIDTH)
			tmp.x = SCREEN_WIDTH;
		if(tmp.x < 0.0f)
			tmp.x = 0.0f;
		if(tmp.y > SCREEN_HEIGHT)
			tmp.y = SCREEN_HEIGHT;
		if(tmp.y < 0.0f)
			tmp.y = 0.0f;

		sprite *s = hudPane->spriteList[10];
		s->positionX = tmp.x - s->backupOnscreenWidth;
		s->positionY = glb_viewer[blData->playerNum]->height - tmp.y - s->backupOnscreenHeight;
		s->maybeEnabled = 0x27;

		printf("Copter sprite is visible at %i, %i\n", s->positionX, s->positionY);

		// If target reticle is nearly centred in both X and Y (by 32px in both axes), blink it?
		float dx = tmp.x - (SCREEN_WIDTH / 2.0f);
		if(dx < 0.0f)
			dx = -dx;
		
		float dy = tmp.y - (SCREEN_HEIGHT / 2.0f);
		if(dy < 0.0f)
			dy = -dy;

		if(dx < 32.0f && dy < 32.0f && (GameState.NumFramesUnpaused & 8))
			hudPane->spriteList[10]->maybeEnabled = 0xff;
	
	}
	
	// Turn off the pane if the camera mode changes
	if(		blData->camMode != CamMode_Redeemer 
		&& 	blData->hudInfo != NULL
		&& 	blData->hudInfo->pane[Redeemer].maybeCanBeEnabled) 
	{	
		blData->hudInfo->pane[Redeemer].enabled = false;
		blData->hudInfo->pane[Redeemer].state = 0;	
	}
		
	float lifetime;
	
	if(blData->remoteControlDevice == NULL) { // Missile has blown up, image lost for a short while

		hudPane->spriteList[2]->maybeEnabled = 0x27; // Completely obscure the camera view, just static?
		lifetime = 1.0f;
	
	} else { // Missile in flight

		hudPane->spriteList[2]->maybeEnabled = 0xff; // Not completely obscured

		// Find the missile that the player is controlling
		BU_tag *missile = (BU_tag*)blData->remoteControlDevice->extraObjectData;
		
		// Max age of the missile is represented in the range field for the sentinel missile
		lifetime = missile->maybeAgeOrLifetime / missile->wpnDef->someDistance;
		
		// Rotate UI elements (only available if in SP)
		if(		extraItemsAsObjList[0] != NULL
			&& 	extraItemsAsObjList[1] != NULL
			&&  extraItemsAsObjList[2] != NULL) 
		{
				
			extraItemsAsObjList[0]->rotation.z = (M_PI/2.0f) - blData->remoteControlDevice->rotation.z + M_PI;
			extraItemsAsObjList[0]->renderType |= 0x20;

			extraItemsAsObjList[1]->rotation.z = blData->remoteControlDevice->rotation.z - (M_PI/4.0f);
			extraItemsAsObjList[1]->renderType |= 0x20;

			extraItemsAsObjList[2]->rotation.z = (blData->remoteControlDevice->rotation.z + M_PI) - (M_PI/4.0f);
			extraItemsAsObjList[2]->renderType |= 0x20;
				
		}
		
	}

	// Modulate the alpha value according to lifetime and some noise factor?
	int someRandNum = 27 + Rand_Rand(24);

	int unsaturatedTintModifier;

	if(lifetime <= 0.85f) {
		unsaturatedTintModifier = someRandNum;
	} else {
		unsaturatedTintModifier = (someRandNum + (lifetime - 0.85f) * 3400.0f);
	}
	
	int tintModifier = (unsaturatedTintModifier > 0xff ? 0xff : unsaturatedTintModifier);
	hudPane->spriteList[1]->colourTint = tintModifier | 0x7f000000;
	
	static uint8_t Scrl = 0;

	if(Scrl & 1) {
		// Some additional modulation (scrolling of the scanlines?)
		hudPane->spriteList[1]->spritesheetY = Rand_Rand(hudPane->spriteList[1]->backupOnscreenHeight - 1);
	}

	Scrl++;

}