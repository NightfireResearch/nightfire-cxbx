#include "HUD.h"
#include <math.h>
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
// XBE_GLOBAL(0x00181348, 0xa2)
static CrosshairInfo HUDCrossCoords[9] = {
    {0, 0, 0, 0, {0}},
    {1, 2, 29, 29, {0}},
    {10, 11, 11, 11, {0}},
    {31, 1, 14, 13, {0}},
    {31, 15, 12, 16, {0}},
    {45, 14, 18, 18, {0}},
    {0, 30, 13, 16, {0}},
    {17, 32, 21, 21, {0}},
    {46, 1, 17, 10, {0}},
};

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
		if(weapon_data[weaponId].weaponFlags & 0x40) { // Custom crosshair pane?
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

// XBE_GLOBAL(0x0017f778, 0x2c)
static SpriteInfo CrossHair = {0xff0000ff, 0x7f7f7fff, 0x001f, 0x0800, 0, 0, 0, 0, 0, 0, 0, 0, (Action_TranslatedText)0xffffffff, NULL, (HASHCODE)0x03000026, 5, {0, 0, 0}};
// XBE_GLOBAL(0x002790b8, 0x2)
static int16_t ttimer;
// XBE_GLOBAL(0x002790bc, 0x2)
static int16_t ctimer;


// The mission-status pane: "mission failed" banners and the like (message type 3), with the reason for the
// failure (Mission_FailLabel) as its second line. Its sprite info (3 sprites) is still the game's.
// XBE_GLOBAL(0x0017fea8, 0x1c)
static HUDPANECREATE_tag MsgMissionStatusPane = {0, 167, 640, 103, (HUDPANE_createFunc)0x000b6450 /* HUD_CreateMissionStatusPane */, HUD_UpdateStatusPane, (SpriteInfo*)0x00181408 /* mission status sprites */, 3, 4, 16, 0, 0};
// The objective pane (message type 2): its width is 0 in the XBE and filled in before main by
// HUD_InitObjectiveStatusPaneWidth, like the info and pickup panes. Its sprite info (6 sprites) is still the game's.
// XBE_GLOBAL(0x001813ec, 0x1c)
static HUDPANECREATE_tag MsgObjectiveStatusPane = {160, 0, 0 /* HUD_InitObjectiveStatusPaneWidth */, 0, (HUDPANE_createFunc)0x000b6490 /* HUD_CreateObjectiveStatusPane */, HUD_UpdateStatusPane, (SpriteInfo*)0x00181490 /* objective status sprites */, 6, 4, 16, 0, 0};
// XBE_GLOBAL(0x00181598, 0x1c)
static HUDPANECREATE_tag MsgInfoStatusPane = {160, 137, 0 /* HUD_InitInfoStatusPaneWidth */, 0, (HUDPANE_createFunc)0x000b64d0 /* HUD_CreateInfoStatusPane */, HUD_UpdateStatusPane, (SpriteInfo*)0x00181698 /* InfoStatusSprInfo */, 5, 4, 16, 0, 0};
// XBE_GLOBAL(0x0017fe8c, 0x1c)
static HUDPANECREATE_tag AirPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b5c30 /* HUD_CreateDefault */, (HUDPANE_updateFunc)0x000b3a10 /* HUD_UpdateAirPane */, (SpriteInfo*)0x0017fe08 /* AirSprInfo */, 3, 0, 0, 0, 0};
// XBE_GLOBAL(0x00180218, 0x1c)
static HUDPANECREATE_tag NightSightPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b5e80 /* HUD_CreateShrink */, (HUDPANE_updateFunc)0x000b3da0 /* HUD_UpdateNightSightPane */, (SpriteInfo*)0x00180110 /* NightSightSprInfo */, 6, 0, 384, 0, 0};
// XBE_GLOBAL(0x0018036c, 0x1c)
static HUDPANECREATE_tag LensFlarePane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b5c30 /* HUD_CreateDefault */, (HUDPANE_updateFunc)0x000b4330 /* HUD_UpdateLensFlarePane */, (SpriteInfo*)0x00180238 /* LensFlareSprInfo */, 7, 0, 0, 0, 0};
// XBE_GLOBAL(0x00180690, 0x1c)
static HUDPANECREATE_tag RCCarPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b5e80 /* HUD_CreateShrink */, (HUDPANE_updateFunc)0x000b3ff0 /* HUD_UpdateCarPane */, (SpriteInfo*)0x00180588 /* RCCarSprInfo */, 6, 0, 384, 0, 0};
// XBE_GLOBAL(0x001800f4, 0x1c)
static HUDPANECREATE_tag CameraPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b5c30 /* HUD_CreateDefault */, (HUDPANE_updateFunc)0x000b3cc0 /* HUD_UpdateCameraPane */, (SpriteInfo*)0x0017ff68 /* CameraSprInfo */, 9, 1, 0, 0, 0};
// XBE_GLOBAL(0x001807d4, 0x1c)
static HUDPANECREATE_tag XrayPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b5c30 /* HUD_CreateDefault */, (HUDPANE_updateFunc)0x000b3ee0 /* HUD_UpdateXRayPane */, (SpriteInfo*)0x001806f8 /* XraySprInfo */, 5, 0, 0, 0, 0};
// XBE_GLOBAL(0x00180924, 0x1c)
static HUDPANECREATE_tag SecCamPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b5c30 /* HUD_CreateDefault */, (HUDPANE_updateFunc)0x000b3fa0 /* HUD_UpdateSecCamPane */, (SpriteInfo*)0x001807f0 /* SecCamSprInfo */, 7, 0, 0, 0, 0};
// XBE_GLOBAL(0x00180a74, 0x1c)
static HUDPANECREATE_tag OICWPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b65b0 /* HUD_CreateOICWPane */, (HUDPANE_updateFunc)0x000b3ae0 /* HUD_UpdateOICWPane */, (SpriteInfo*)0x00180940 /* OICWSprInfo */, 7, 0, 384, 0, 0};
// XBE_GLOBAL(0x00180b98, 0x1c)
static HUDPANECREATE_tag RoninPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b5e80 /* HUD_CreateShrink */, (HUDPANE_updateFunc)0x000e0ec0 /* __profiling_or_debugging_hook_point */, (SpriteInfo*)0x00180a90 /* RoninSprInfo */, 6, 0, 384, 0, 0};
// XBE_GLOBAL(0x00180cc0, 0x1c)
static HUDPANECREATE_tag LaserPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b5e80 /* HUD_CreateShrink */, (HUDPANE_updateFunc)0x000e0ec0 /* __profiling_or_debugging_hook_point */, (SpriteInfo*)0x00180bb8 /* LaserSprInfo */, 6, 0, 384, 0, 0};
// XBE_GLOBAL(0x00180ec4, 0x1c)
static HUDPANECREATE_tag SpacePane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b5c30 /* HUD_CreateDefault */, (HUDPANE_updateFunc)0x000b4090 /* HUD_UpdateSpacePane */, (SpriteInfo*)0x00180ce0 /* SpaceSprInfo */, 11, 0, 0, 0, 0};
// XBE_GLOBAL(0x00181774, 0x1c)
static HUDPANECREATE_tag MsgPickupStatusPane = {160, 372, 0 /* HUD_InitPickupStatusPaneWidth */, 0, (HUDPANE_createFunc)0x000b6540 /* HUD_CreatePickupStatusPane */, HUD_UpdateStatusPane, (SpriteInfo*)0x001815b8 /* PickupStatusSprInfo */, 5, 4, 24, 0, 0};


// The layout of each kind of Text_AddMsg message (its third argument): the font it is word-wrapped in and the
// width it is wrapped to. PS2 name: TextMsgFormats. Entry 0 is empty; 5 (subtitles over a movie) wraps to 0.6 and
// 0.4 of its width in two lines. Only read here.
#pragma pack(push, 1)
typedef struct TextMsgFormat {
    const char *font;     // font data, handed to Font_WordWrapString
    ushort wrapWidth;
    ushort pad;
} TextMsgFormat;
#pragma pack(pop)
static_assert(sizeof(TextMsgFormat) == 8, "TextMsgFormat is 8 bytes");
// XBE_GLOBAL(0x0018ccf8, 0x38)
#define TextMsgFormats (*(TextMsgFormat(*)[7])0x0018ccf8)

#define TXTMSG_INFO      1   // e.g. a hostage's line (NDrone2_Hostage)
#define TXTMSG_OBJECTIVE 2   // objective updates (Mission.cpp)
#define TXTMSG_PICKUP    6

// The three message panes are sized to the width their messages are wrapped to. That is not a constant, so the
// compiler made each width a dynamic initialiser - these three, run by _cinit from the C++ initialiser table
// (0x163114...) before main - and left the rest of each pane as plain data. On PS2 the whole of each pane is
// built at run time in HUD.cpp's __static_initialization_and_destruction_0, from the same TextMsgFormats entries.
// Each copies exactly the 16-bit width (MOV AX / MOV [pane+4],AX), nothing else.

// FUNC_AT(000f54b0)
void HUD_InitObjectiveStatusPaneWidth(void) {
    MsgObjectiveStatusPane.width = (short)TextMsgFormats[TXTMSG_OBJECTIVE].wrapWidth;
}

// FUNC_AT(000f54e0)
void HUD_InitInfoStatusPaneWidth(void) {
    MsgInfoStatusPane.width = (short)TextMsgFormats[TXTMSG_INFO].wrapWidth;
}

// FUNC_AT(000f5510)
void HUD_InitPickupStatusPaneWidth(void) {
    MsgPickupStatusPane.width = (short)TextMsgFormats[TXTMSG_PICKUP].wrapWidth;
}

// AUTOGEN
void HUD_CreateRedeemer(BLData* blData, HUDPANE_tag *hudPane, HUDPANECREATE_tag *paneCreate, obj_tag *obj);

// TODO: Change from 640x480 to generic
// XBE_GLOBAL(0x00180388, 0x1e4)
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

// XBE_GLOBAL(0x0018056c, 0x1c)
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



// XBE_GLOBAL(0x0017f950, 0x58)
SpriteInfo BloodSprInfo[] = {
	{0x007f7fff, 0x7f7f7fff, 0x0009, 0x0200, 0, 	0, 		SCREEN_WIDTH, 	32,	0, 	0, 	0, 	0, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_BLOOD_DRIP, 5, 0, 0, 0}, // Drippy edge
	{0x007f7fff, 0x7f7f7fff, 0x0009, 0x0200, 0,		0,		SCREEN_WIDTH, 	64,	0, 	0, 	0, 	0, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_COLOUR_FILL, 5, 0, 0, 0}, // Colour fill
};

// AUTOGEN
void HUD_UpdateBloodPane(BLData *blData, HUDPANE_tag *hudPane, obj_tag *gameObj);

// XBE_GLOBAL(0x0017f9a8, 0x1c)
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


// XBE_GLOBAL(0x0017fec8, 0x84)
SpriteInfo SightSprInfo[] = { // Left bar, Scope (square fitted to SCREEN_HEIGHT), Right bar
	{0x7f7f7fff, 0x7f7f7fff, 0x0027, 0x0600, (SCREEN_WIDTH-SCREEN_HEIGHT)/2, 	0, 		SCREEN_HEIGHT, 						SCREEN_HEIGHT,	0, 	0, 	0x1FF,	0x1FF, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_SNIPER_SCOPE, 5, 0, 0, 0}, // Scope graphic centre
	{0x7f7f7fff, 0x7f7f7fff, 0x0027, 0x0200, 0,									0,		(SCREEN_WIDTH-SCREEN_HEIGHT)/2, 	SCREEN_HEIGHT,	0, 	0, 	0, 		0, 		Action_TranslatedText_NULLVALUE, NULL, SPRITE_COLOUR_FILL, 5, 0, 0, 0}, // Fill left
	{0x7f7f7fff, 0x7f7f7fff, 0x0027, 0x0200, (SCREEN_WIDTH+SCREEN_HEIGHT)/2,	0,		(SCREEN_WIDTH-SCREEN_HEIGHT)/2, 	SCREEN_HEIGHT,	0, 	0, 	0, 		0, 		Action_TranslatedText_NULLVALUE, NULL, SPRITE_COLOUR_FILL, 5, 0, 0, 0}, // Fill right
};

// XBE_GLOBAL(0x0017ff4c, 0x1c)
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

// XBE_GLOBAL(0x0017f930, 0x1c)
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

// Frames left of the third-person icon's fade after the icon goes away (BLData.thirdIcon back to 0xff)
// XBE_GLOBAL(0x002790b0, 0x2)
static int16_t ThirdIconTimer;
// Frames left of the "standing up" icon after the player stops crouching
// XBE_GLOBAL(0x002790ac, 0x2)
static int16_t CrouchIconTimer;

// Written by the menu's coordinates cheat (C_CHCHCOORDS_Handler) and read by HUD_UpdateMPHealthPane, both still
// the game's
#define switch_SHOW_COORDS (*(int*)0x001df9dc)

// The health pane's sprites (HealthSprInfo, 0x0017f9c8, 24 entries)
#define HEALTH_ARMOUR_FIRST     0   // 0-7: the armour bar's segments
#define HEALTH_NUM_ARMOUR       8
#define HEALTH_HEALTH_FIRST     8   // 8-14: the health bar's segments
#define HEALTH_NUM_HEALTH       7
#define HEALTH_BACKING          15  // the tinted panel behind both bars
#define HEALTH_NUM_BARS         16  // 0-15, hidden together when a full-screen pane is up
#define HEALTH_BOND_MOMENT      16  // TEX_GOLD007BONUS, spun in at the top of the screen
#define HEALTH_COORDS           17  // the coordinates cheat's text
#define HEALTH_THIRD_ICON       18  // what the player can do here (grapple, wire, ...)
#define HEALTH_CROUCH_ICON      19
#define HEALTH_HIT_FIRST        20  // 20-23: full-screen red hit-direction overlays, one per BLData.hitDirections bit
#define HEALTH_NUM_HIT          4

// The health bar's colour by how much is left (the alpha byte is filled in per segment)
#define HEALTH_COLOUR_HIGH  0x52a88b00
#define HEALTH_COLOUR_MID   0xc6984e00
#define HEALTH_COLOUR_LOW   0x9d191200

#define HUD_ICON_ON         0x1d    // maybeEnabled of a shown icon
#define HUD_HIT_ON          0x1e

// The third-person icons, indexed by BLData.thirdIcon
static const HASHCODE ThirdIconSprites[7] = {
    (HASHCODE)0x03000064, (HASHCODE)0x03000065, ICON_GRAPPLE, ICON_WIRE,
    (HASHCODE)0x03000068, (HASHCODE)0x03000069, ICON_STANDING
};
#define ICON_CROUCHING  ((HASHCODE)0x03000173)
#define ICON_UNCROUCH   ((HASHCODE)0x03000174)

// AUTOINJECT
void HUD_CreateHealthPane(BLData* blData, HUDPANE_tag *hudPane, HUDPANECREATE_tag *paneCreate, obj_tag *obj) { 

	HUD_CalcWidthHeight(paneCreate, &paneCreate->width, &paneCreate->height);
	HUD_CreateDefault(blData, hudPane, paneCreate, obj);

	
	// Resize fullscreen effects (inc blood vignette effect) to fit screen
	// The original game does this, it's unclear why they wouldn't just bake this into the HUDPANECREATE
	// but this works in our favour for non-default resolutions anyway!
	for(int i = 20; i < 24; i++) {
		hudPane->spriteList[i]->positionX = 0;
		hudPane->spriteList[i]->positionY = 0;
		hudPane->spriteList[i]->onscreenWidth = SCREEN_WIDTH;
		hudPane->spriteList[i]->onscreenHeight = SCREEN_HEIGHT;
	}
	
	ThirdIconTimer = 0;
	CrouchIconTimer = 0;

}

// The single-player armour and health bars, the red hit-direction flashes, the 007 "Bond moment" icon, the
// coordinates cheat's readout, the third-person action icon and the crouch icon.
// AUTOINJECT
void HUD_UpdateHealthPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj) {

    // Read before the enabled test, as the original does
    sprite *backing = pane->spriteList[HEALTH_BACKING];
    sprite *bondMoment = pane->spriteList[HEALTH_BOND_MOMENT];
    sprite *coords = pane->spriteList[HEALTH_COORDS];
    sprite *thirdIcon = pane->spriteList[HEALTH_THIRD_ICON];
    sprite *crouchIcon = pane->spriteList[HEALTH_CROUCH_ICON];

    if (!pane->enabled)
        return;

    for (int i = 0; i < HEALTH_NUM_HIT; i++)
        pane->spriteList[HEALTH_HIT_FIRST + i]->maybeEnabled = 0xff;

    // Flash the sides the player was hit from, fading by a frame's worth each frame
    uchar fade = (uchar)blData->hitDirectionFade;
    if (fade != 0) {
        for (int i = 0; i < HEALTH_NUM_HIT; i++) {
            if (blData->hitDirections & (1 << i)) {
                pane->spriteList[HEALTH_HIT_FIRST + i]->maybeEnabled = HUD_HIT_ON;
                pane->spriteList[HEALTH_HIT_FIRST + i]->colourTint = (uint)(uchar)blData->hitDirectionFade | 0xff000000;
            }
        }
        double faded = (double)(int)(uchar)blData->hitDirectionFade - (double)FRAME_RATE_MUL;
        if (faded < 0.0)
            faded = 0.0;
        blData->hitDirectionFade = (uchar)(int)faded;
    }

    // Armour in bar segments: 50 armour fills all 8. Stored as a float, as the original does.
    float armourSegments = blData->armor * 0.16f;

    // The bars fade in over a quarter of hudFadeIn's rise; always fully shown while paused
    if (GS_IsPaused(-1))
        blData->hudFadeIn = 1.0f;
    float alpha = blData->hudFadeIn * 4.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    // No bars under a full-screen view: camera, OICW, Ronin, security camera, Sentinel missile, RC car
    HUDINFO_tag *hud = blData->hudInfo;
    if (!hud->pane[Camera].enabled && !hud->pane[OICW].enabled && !hud->pane[Ronin].enabled &&
        !hud->pane[SecCam].enabled && !hud->pane[Redeemer].enabled && !hud->pane[RCCar].enabled) {

        // The x87 products below are exact (or all but) in double; float would round them differently.
        for (ushort i = 0; i < HEALTH_NUM_ARMOUR; i++) {
            if (armourSegments == 0.0f) {
                pane->spriteList[HEALTH_ARMOUR_FIRST + i]->maybeEnabled = 0xff;
            } else {
                // Full segments brighter than empty ones; the first (the armour icon) always full
                float brightness = ((float)i < armourSegments) ? 0.75f : 0.35f;
                if (i == 0)
                    brightness = 1.0f;
                sprite *s = pane->spriteList[HEALTH_ARMOUR_FIRST + i];
                s->maybeEnabled = (uchar)pane->base->spriteInfo[HEALTH_ARMOUR_FIRST + i].maybeEnabled;
                s = pane->spriteList[HEALTH_ARMOUR_FIRST + i];
                uchar a = (uchar)(int)((double)alpha * (double)brightness * 255.0);
                s->colourTint = (s->colourTint & 0xffffff00) | a;
            }
        }

        // Health in segments: 100 fills 7. Kept at x87 precision (exact in double) as the original keeps it on
        // the FPU stack.
        float health = blData->health;
        if (health < 0.0f)
            health = 0.0f;
        else if (health > 100.0f)
            health = 100.0f;
        double healthSegments = (double)health * (double)0.07f;

        uint colour = HEALTH_COLOUR_HIGH;
        if (healthSegments < 4.0)
            colour = HEALTH_COLOUR_MID;
        if (healthSegments < 2.0)
            colour = HEALTH_COLOUR_LOW;

        for (ushort i = 0; i < HEALTH_NUM_HEALTH; i++) {
            pane->spriteList[HEALTH_HEALTH_FIRST + i]->maybeEnabled =
                (uchar)pane->base->spriteInfo[HEALTH_HEALTH_FIRST + i].maybeEnabled;
            float brightness = (healthSegments > (double)i) ? 0.55f : 0.15f;
            // Nearly dead: the first segment blinks, half a second on and off
            if (i == 0 && healthSegments < 0.5)
                brightness = (GameState.NumFramesUnpaused % 30 < 15) ? 0.55f : 0.15f;
            uchar a = (uchar)(int)((double)alpha * (double)brightness * 255.0);
            pane->spriteList[HEALTH_HEALTH_FIRST + i]->colourTint = (uint)a | colour;
        }

        // The panel behind the bars. The original keeps 16 bits of the conversion here, not 8.
        if (backing != NULL)
            backing->colourTint = (uint)(ushort)(int)((double)alpha * 140.0) | colour;

    } else {
        for (int i = 0; i < HEALTH_NUM_BARS; i++)
            pane->spriteList[i]->maybeEnabled = 0xff;
    }

    // The Bond moment icon: spins in (a squashed, flipping card) over the first 100 frames of its 200, then sits
    // still until the timer runs out
    if (bondMoment != NULL) {
        bondMoment->linkedViewer = (glb_viewer[4] != NULL && glb_viewer[4]->field25_0x29 != 0) ? 4 : 5;

        if (blData->bondMomentTimer <= 0) {
            blData->bondMomentTimer = 0;
            bondMoment->maybeEnabled = 0xff;
        } else {
            bondMoment->positionX = 320;
            bondMoment->positionY = 80;
            short t = blData->bondMomentTimer;
            if (t > 100) {
                // On the x87: all extended precision, the cosine by FCOS
                double scale = (double)(200 - t) * (double)0.01f;
                double flip = cos((double)(t * 4 - 400) * (double)0.03141593f);
                ushort height = (ushort)bondMoment->backupOnscreenHeight;
                bondMoment->onscreenHeight = (short)(int)((double)height * fabs(flip) * scale);
                // Upside down while the card shows its back
                bondMoment->spritesheetHeight = (flip < 0.0) ? (short)-height : (short)height;
                bondMoment->onscreenWidth = (short)(int)((double)(ushort)bondMoment->backupOnscreenWidth * scale);
            }
            bondMoment->maybeEnabled = 0;
            blData->bondMomentTimer = (short)(int)((double)blData->bondMomentTimer - (double)FRAME_RATE_MUL);
        }
    }

    // The coordinates cheat
    if (coords != NULL) {
        if (switch_SHOW_COORDS != 0) {
            coords->maybeEnabled = HUD_ICON_ON;
            sprintf(coords->text, "%.2f %.2f %.2f\nFPS %.2f\n", (double)obj->position.x, (double)obj->position.y,
                    (double)obj->position.z, (double)_FRAME_RATE);
            Sprite_SetText(coords, coords->text);
        } else {
            coords->maybeEnabled = 0xff;
        }
    }

    // The third-person icon: shown while BLData.thirdIcon names one, then fades out over half a second
    if (thirdIcon != NULL) {
        uchar icon = (uchar)blData->thirdIcon;
        bool shown = false;
        if (icon != 0xff) {
            // No bounds check, as in the original (whose table is on the stack)
            if (hashtable_set_sprite(thirdIcon, ThirdIconSprites[icon])) {
                thirdIcon->maybeEnabled = HUD_ICON_ON;
                thirdIcon->positionX = 528;
                thirdIcon->positionY = 48;
                thirdIcon->colourTint = 0x7f7f7fff;
                ThirdIconTimer = (int16_t)(int)((double)_FRAME_RATE * 0.5);
                shown = true;
            }
        } else if (ThirdIconTimer != 0) {
            thirdIcon->maybeEnabled = HUD_ICON_ON;
            uchar a = (uchar)(int)((double)(ThirdIconTimer * 255) / ((double)_FRAME_RATE * 0.5));
            thirdIcon->colourTint = (uint)a | 0x7f7f7f00;
            ThirdIconTimer--;
            shown = true;
        }
        if (!shown)
            thirdIcon->maybeEnabled = 0xff;
    }

    // The crouch icon: on while crouching, then the "stand up" icon for a second, fading over the last half
    if (crouchIcon != NULL) {
        if (obj->subState == MovementType_Crouch) {
            hashtable_set_sprite(crouchIcon, ICON_CROUCHING);
            crouchIcon->positionX = 48;
            crouchIcon->positionY = 48;
            crouchIcon->maybeEnabled = HUD_ICON_ON;
            crouchIcon->colourTint = 0x7f7f7fff;
            CrouchIconTimer = (int16_t)(int)_FRAME_RATE;
            return;
        }
        if (CrouchIconTimer != 0) {
            hashtable_set_sprite(crouchIcon, ICON_UNCROUCH);
            crouchIcon->maybeEnabled = HUD_ICON_ON;
            uchar a = 0xff;
            double half = (double)_FRAME_RATE * 0.5;
            if ((double)CrouchIconTimer < half)
                a = (uchar)(int)((double)(CrouchIconTimer * 255) / half);
            crouchIcon->colourTint = (uint)a | 0x7f7f7f00;
            CrouchIconTimer--;
            return;
        }
        crouchIcon->maybeEnabled = 0xff;
    }
}

// XBE_GLOBAL(0x0017fde8, 0x1c)
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


// XBE_GLOBAL(0x00180ee0, 0x58)
HUDPANECREATE_tag* PaneList[] = {
	&AmmoPane,
	&HealthPane,
	&MsgMissionStatusPane,
	&MsgObjectiveStatusPane,
	&MsgInfoStatusPane,
	&AirPane, // Oxygen/Swimming indicator?
	&SightPane,
	&NightSightPane,
	&LensFlarePane,
	&RedeemerPane,
	&RCCarPane,
	&CameraPane,
	&BloodPane,
	NULL,
	NULL,
	&XrayPane,
	&SecCamPane,
	&OICWPane,
	&RoninPane,
	&LaserPane,
	&SpacePane,
	&MsgPickupStatusPane
};

static_assert(ARRAY_SIZE(PaneList) == NUM_PANES, "Bad size of pane list");


// XBE_GLOBAL(0x00180fbc, 0x1c)
static HUDPANECREATE_tag MPAmmoPane = {640, 480, 0, 26, (HUDPANE_createFunc)0x000b6270 /* HUD_CreateAmmoPane */, (HUDPANE_updateFunc)0x000b1f90 /* HUD_UpdateAmmoPane */, (SpriteInfo*)0x00180f38 /* MPAmmoSprInfo */, 3, 4, 24, 0, 0};
// XBE_GLOBAL(0x00181138, 0x1c)
static HUDPANECREATE_tag MPHealthPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b6330 /* HUD_CreateMPHealthPane */, (HUDPANE_updateFunc)0x000b2870 /* HUD_UpdateMPHealthPane */, (SpriteInfo*)0x00180fd8 /* MPHealthSprInfo */, 8, 0, 384, 0, 0};
// XBE_GLOBAL(0x00181180, 0x1c)
static HUDPANECREATE_tag MPMsgInfoStatusPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b64d0 /* HUD_CreateInfoStatusPane */, (HUDPANE_updateFunc)0x000b3890 /* HUD_MPUpdateStatusPane */, (SpriteInfo*)0x00181154 /* MPInfoStatusSprInfo */, 1, 4, 384, 0, 0};
// XBE_GLOBAL(0x001812d4, 0x1c)
static HUDPANECREATE_tag MPScorePane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b67d0 /* HUD_CreateMPScorePane */, (HUDPANE_updateFunc)0x000b5190 /* HUD_MPUpdatePane */, (SpriteInfo*)0x001811a0 /* MPScoreInfo */, 7, 0, 384, 0, 0};
// XBE_GLOBAL(0x001806d8, 0x1c)
static HUDPANECREATE_tag RadarPane = {0, 0, 640, 480, (HUDPANE_createFunc)0x000b69e0 /* HUD_CreateRadar */, (HUDPANE_updateFunc)0x000b54d0 /* HUD_RadarUpdate */, (SpriteInfo*)0x001806ac /* RadarSprInfo */, 1, 28, 384, 0, 0};

// XBE_GLOBAL(0x001812f0, 0x58)
HUDPANECREATE_tag * MPPaneList[] = {
	&MPAmmoPane,
	&MPHealthPane,
	NULL,
	NULL,
	&MPMsgInfoStatusPane,
	NULL,
	&SightPane,
	NULL,
	NULL,
	&RedeemerPane,
	&RCCarPane,
	NULL,
	&BloodPane,
	&MPScorePane,
	&RadarPane,
	NULL,
	NULL,
	&OICWPane,
	&RoninPane,
	&LaserPane,
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

// XBE_GLOBAL(0x002790b4, 0x2)
static int16_t OICW_timer;
// XBE_GLOBAL(0x002790ae, 0x1)
static uint8_t OICW_mode;

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
		lifetime = missile->maybeAgeOrLifetime / missile->wpnDef->range;
		
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

// AUTOGEN
bool __cdecl Text_UpdateMsg(char param_1, TXTMSG_TYPE txtmsg_type, TXT_MSG **param_3);

// The four extra items HUD_CreateDefault allocates for each message pane (numExtraItems = 4), as the
// HUD_Create*StatusPane functions fill them in.
typedef struct StatusPaneData {
    TXT_MSG *msg;       // the message on show, NULL for none - Text_UpdateMsg's in/out argument
    int state;          // STATUS_* below
    int msgType;        // the Text_AddMsg type this pane shows: 1 info, 2 objective, 3 mission, 6 pickup
    int shrinkStep;     // pixels the bar sprite loses per frame while closing: 16, or 32 for the mission pane
} StatusPaneData;
static_assert(sizeof(StatusPaneData) == 4 * sizeof(void*), "StatusPaneData is the pane's four extra items");

// The life of a message on a status pane. PS2 has no names for these.
#define STATUS_IDLE     0   // nothing shown; wait for a message of this pane's type
#define STATUS_OPEN     1   // the bar (sprite 2) springs to full width
#define STATUS_SHOW     2   // put the message's text (or picture) on the pane
#define STATUS_HOLD     3   // fade in/out with the message's timer until Text_UpdateMsg moves on
#define STATUS_CLOSE    4   // the bar shrinks away, then back to STATUS_IDLE

// The sprite colours the message text is drawn in.
#define STATUS_TEXT_COLOUR      0x7d6d59ff
#define STATUS_HEADING_COLOUR   0x785a14ff  // "NEW OBJECTIVE" / "OBJECTIVE COMPLETE"

// The objective heading's state, shared by every objective pane (only player 0 has one in practice). PS2 names
// (function-local statics of HUD_UpdateStatusPane there). Only HUD_UpdateStatusPane touches any of them.
// XBE_GLOBAL(0x002790d0, 0x1)
static bool IsNew;          // the objective message is a new objective
// XBE_GLOBAL(0x002790d1, 0x1)
static bool IsComplete;     // the objective message is "objective complete"
// XBE_GLOBAL(0x002790c0, 0x4)
static int ObjectiveSFXTriggered2;  // ObjectiveSFXPlayMe2nd has been played (or is not wanted)
// XBE_GLOBAL(0x002790c4, 0x4)
static int ObjectiveSFXTriggered1;  // ObjectiveSFXPlayMe1st has been played (or is not wanted)
// XBE_GLOBAL(0x002790c8, 0x4)
static Action_SFX ObjectiveSFXPlayMe2nd;    // played as the heading appears
// XBE_GLOBAL(0x002790cc, 0x4)
static Action_SFX ObjectiveSFXPlayMe1st;    // played once the heading starts to flash

// Set the low (alpha) byte of a sprite's colour and keep the rest; the original writes that byte alone.
static void StatusPane_SetAlpha(sprite *spr, uint alpha) {
    spr->colourTint = (spr->colourTint & 0xffffff00) | alpha;
}

// Centre the bar sprite (sprite 2) on the pane's sprite-2 layout, as wide as it is now. All 16-bit arithmetic.
static void StatusPane_CentreBar(HUDPANE_tag *pane, sprite *bar) {
    SpriteInfo *layout = &pane->base->spriteInfo[2];
    bar->positionX = (short)((layout->onscreenWidth >> 1) - (bar->onscreenWidth >> 1) + layout->posX);
}

// The update function of the four message panes (mission, objective, info, pickup): takes the next message of the
// pane's type off Text's queue, opens a bar, shows the message, fades it with the message's timer, and shrinks the
// bar away again. The objective pane also puts up a flashing "NEW OBJECTIVE" / "OBJECTIVE COMPLETE" heading with
// its sounds.
// AUTOINJECT
void HUD_UpdateStatusPane(BLData *blData, HUDPANE_tag *pane, obj_tag *unused) {

    if (!pane->enabled)
        return;

    StatusPaneData *data = (StatusPaneData*)pane->extraItems;
    ushort numSprites = (ushort)pane->numSprites;
    sprite **sprites = pane->spriteList;

    // Sprite 2 is the bar and is always read; 3 and 4 are the bar's end caps, 5 an extra line, where the pane
    // has that many sprites.
    sprite *bar = sprites[2];
    sprite *leftCap = (numSprites > 3) ? sprites[3] : NULL;
    sprite *rightCap = (numSprites > 4) ? sprites[4] : NULL;
    sprite *extra = (numSprites > 5) ? sprites[5] : NULL;

    sprite *text = sprites[0];
    // The mission pane's second line: why the mission failed
    sprite *failText = (pane->base == &MsgMissionStatusPane) ? sprites[1] : NULL;
    // The objective pane's heading - the same sprite as extra above
    sprite *heading = (pane->base == &MsgObjectiveStatusPane) ? sprites[5] : NULL;

    TXT_MSG *msg = data->msg;

    switch (data->state) {

        case STATUS_IDLE:
            // Hide everything (numSprites is read again each time round, as the original does)
            if (numSprites != 0) {
                ushort i = 0;
                do {
                    pane->spriteList[i]->maybeEnabled = 0xff;
                    i++;
                } while (i < (ushort)pane->numSprites);
            }
            if (text != NULL)
                StatusPane_SetAlpha(text, 0);
            if (extra != NULL)
                StatusPane_SetAlpha(extra, 0);

            if (Text_UpdateMsg(blData->playerNum, (TXTMSG_TYPE)data->msgType, &data->msg) && data->msg != NULL) {
                // A message for us: start the bar at zero width in the middle of its space
                if (bar != NULL) {
                    SpriteInfo *layout = &pane->base->spriteInfo[2];
                    bar->onscreenWidth = 0;
                    bar->positionX = (short)((layout->onscreenWidth >> 1) + layout->posX);
                }
                data->state = STATUS_OPEN;
            }
            return;

        case STATUS_OPEN:
            if (bar == NULL) {
                data->state = STATUS_SHOW;
            } else {
                bar->onscreenWidth = pane->base->spriteInfo[2].onscreenWidth;
                StatusPane_CentreBar(pane, bar);
                data->state = STATUS_SHOW;
            }
            // The caps hug the bar. bar is not checked for NULL here, as in the original; every pane with caps
            // has a bar.
            if (leftCap != NULL)
                leftCap->positionX = bar->positionX - leftCap->onscreenWidth;
            if (rightCap != NULL)
                rightCap->positionX = bar->onscreenWidth + bar->positionX;
            return;

        case STATUS_SHOW:
            if (failText != NULL)
                failText->colourTint = STATUS_TEXT_COLOUR;
            if (text != NULL)
                text->colourTint = STATUS_TEXT_COLOUR;
            if (extra != NULL)
                StatusPane_SetAlpha(extra, 0xff);

            // A message can be a picture instead of text (Text_AddMsg's fifth argument)
            if (msg->spriteHash != HASHCODE_NONE) {
                hashtable_set_sprite(text, msg->spriteHash);
                text->text = NULL;
                data->state = STATUS_HOLD;
                return;
            }

            if (msg->type == 2) {
                // An objective: is it "objective complete" or a new one? Decides the heading and its sounds.
                // The flags are set before the label is fetched, as in the original.
                const char *label;
                if (strcmp(msg->text, Txt_BindLabel(TXT_NOTIF_OBJECTIVE_COMPLETE, 0)) == 0) {
                    IsComplete = true;
                    IsNew = false;
                    label = Txt_BindLabel(TXT_NOTIF_OBJECTIVE_COMPLETE, 0);
                } else {
                    IsComplete = false;
                    IsNew = true;
                    label = Txt_BindLabel(TXT_NOTIF_OBJECTIVE_NEW, 0);
                }
                // Only the text pointer is replaced (not its length) before the format is redone - as the original.
                heading->text = (char*)label;
                Sprite_SetFmt(heading, heading->maybeClippedString);

                if (text != NULL)
                    text->colourTint = 0;   // the message itself waits until the heading has had its say

                if (IsNew) {
                    ObjectiveSFXPlayMe1st = SFX_MENU_MISSION_UPDATE;
                    ObjectiveSFXPlayMe2nd = SFX_MENU_NEW_OBJECTIVE;
                    ObjectiveSFXTriggered1 = 0;
                    ObjectiveSFXTriggered2 = 0;
                } else {
                    ObjectiveSFXTriggered2 = 1;
                    if (IsComplete) {
                        ObjectiveSFXPlayMe1st = SFX_MENU_OBJECTIVE_COMPLETE;
                        ObjectiveSFXTriggered1 = 0;
                    } else {
                        ObjectiveSFXTriggered1 = 1;
                    }
                }
            }

            Sprite_SetText(text, msg->text);
            Sprite_SetText(failText, (char*)Txt_BindLabel(Mission_FailLabel(), 0));   // NULL-safe

            // A negative timer is a message on its way out (Text_AddMsg sets -1 on objectives when a mission
            // banner arrives): keep it hidden
            if (msg->timer < 0.0f) {
                text->colourTint = 0;
                if (failText != NULL)
                    failText->colourTint = 0;
            }
            data->state = STATUS_HOLD;
            return;

        case STATUS_HOLD:
            if (msg != NULL) {
                StatusPane_SetAlpha(text, 0xff);
                if (failText != NULL)
                    StatusPane_SetAlpha(failText, 0xff);

                if (msg->type == 2) {
                    // x87 sums: exact in double. The heading shows for the first 240 (new) or 120 (complete)
                    // frames of the message's life, then flashes (20 frames on, 20 off) for the rest of it
                    // while the message text shows.
                    if ((IsNew && (double)msg->timer + 60.0 > 300.0) ||
                        (IsComplete && (double)msg->timer + 60.0 > 180.0)) {
                        text->colourTint = 0;
                        if (failText != NULL)
                            failText->colourTint = 0;
                        heading->colourTint = (fmodf(msg->timer, 40.0f) < 20.0f) ? STATUS_HEADING_COLOUR : 0;
                        heading->unknown1 = 0;
                        text->unknown1 = 0;
                        if (failText != NULL)
                            failText->unknown1 = 0;
                        if (ScriptCam == 0 && ObjectiveSFXTriggered1 == 0) {
                            Sound_PlayExt(ObjectiveSFXPlayMe1st, 100.0f, 0, 0);
                            ObjectiveSFXTriggered1 = 1;
                        }
                    } else {
                        if (ObjectiveSFXTriggered2 == 0) {
                            Sound_PlayExt(ObjectiveSFXPlayMe2nd, 100.0f, 0, 0);
                            ObjectiveSFXTriggered2 = 1;
                        }
                        heading->colourTint = STATUS_HEADING_COLOUR;
                        text->colourTint = IsComplete ? 0 : STATUS_TEXT_COLOUR;
                        if (failText != NULL)
                            failText->colourTint = STATUS_TEXT_COLOUR;
                        heading->unknown1 = 0xff;
                        text->unknown1 = IsComplete ? 0 : 0xff;
                        if (failText != NULL)
                            failText->unknown1 = 0xff;
                    }
                }

                // Fade out over the last 15 frames. timer * 17 is exact in double, as on the x87.
                if (msg->timer >= 0.0f && msg->timer < 15.0f) {
                    text->colourTint = (text->colourTint & 0xffffff00) | (uint)(int)((double)msg->timer * 17.0);
                    if (failText != NULL)
                        failText->colourTint = (failText->colourTint & 0xffffff00) | (uint)(int)((double)msg->timer * 17.0);
                }
                if (msg->timer < 0.0f) {
                    text->colourTint = 0;
                    if (failText != NULL)
                        failText->colourTint = 0;
                }
                if (msg->type == 2 && IsComplete)
                    text->colourTint = 0;   // "objective complete" is said by the heading alone
            }

            // When the message is done: the next one straight away, or close the bar if there is none
            if (Text_UpdateMsg(blData->playerNum, (TXTMSG_TYPE)data->msgType, &data->msg))
                data->state = (data->msg == NULL) ? STATUS_CLOSE : STATUS_SHOW;
            return;

        case STATUS_CLOSE:
            if (text != NULL)
                StatusPane_SetAlpha(text, 0);
            if (failText != NULL)
                StatusPane_SetAlpha(failText, 0);
            if (extra != NULL)
                StatusPane_SetAlpha(extra, 0);

            if (bar == NULL) {
                data->state = STATUS_IDLE;
            } else {
                bar->onscreenWidth -= (short)data->shrinkStep;
                StatusPane_CentreBar(pane, bar);
                if (bar->onscreenWidth <= 0)
                    data->state = STATUS_IDLE;
            }
            if (leftCap != NULL)
                leftCap->positionX = bar->positionX - leftCap->onscreenWidth;
            if (rightCap != NULL)
                rightCap->positionX = bar->onscreenWidth + bar->positionX;
            return;

        default:
            return;
    }
}
