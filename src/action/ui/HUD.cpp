#include "HUD.h"
#include <math.h>
#include "../input.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"
#include "../game/drone/Drone.h"
#include "../game/drone/NDrone2.h"
#include "../game/obj/bullet.h"
#include "../game/obj/build.h"
#include "../game/obj/car.h"
#include "../game/obj/control.h"
#include "../game/obj/Copter.h"
#include "../game/obj/Effect.h"
#include "../game/obj/GT.h"
#include "../game/obj/GunImp.h"
#include "../game/obj/Light.h"
#include "../game/obj/Player.h"
#include "../engine/Collide.h"
#include "../engine/Text.h"
#include "../engine/viewer.h"
#include "../gfx/Font.h"
#include "../memory.h"
#include "../math/X87Ops.h"
#include "../util/Random.h"
#include "../util/hashtable.h"
#include <stdio.h>
#include <string.h>
#include "../game/view.h"

#pragma fp_contract(off)

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// The multiplayer module's, called by address
#define MP_GetTeamScore ((char *(__cdecl *)(ushort team))0x0009d840)
#define MP_GetPlayerScore ((char *(__cdecl *)(short player))0x0009d8d0)
#define MP_IsFlagParent ((bool (__cdecl *)(obj_tag *obj, ushort team))0x0009d980)
#define MP_IsBluePrintParent ((bool (__cdecl *)(obj_tag *obj))0x0009d9b0)
#define MP_ObjTeamHasGEObj ((bool (__cdecl *)(obj_tag *obj, int part))0x0009d9d0)
#define MP_getUplinkStatus ((short (__cdecl *)(short uplink))0x0009e2a0)
#define MP_GetRadarObjects ((ushort (__cdecl *)(obj_tag *obj, RADAROBJ **list))0x0009da80)

// Sprite 3 of the multiplayer score pane is gone (no teams); the PS2 build's name
#define FuckOffSprite3 U8_AT(0x002790b2)
// HUD_UpdateNightSightPane's frame count, every other frame its scanlines jump
#define NightSightScanline U8_AT(0x002790d2)


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

// The crosshair: hidden while dead or on a security camera; zoomed in with a weapon that has a sight of its own
// (weaponFlags 0x40), that sight's pane instead (the upgraded camera turned into the plain one); otherwise the
// crosshair the player's aim asks for (the zoomed one when zoomed), at the aim's offset in the view, green in night
// vision, unless the player turned crosshairs off or a camera mode is on.
void _HUD_UpdateCrossHair(BLData *player, sprite *spr) {
    if (spr == NULL)
        return;
    if (player->hudInfo->pane[Blood].enabled || player->hudInfo->pane[SecCam].enabled) {
        spr->maybeEnabled = 0xff;
        return;
    }

    viewer_tag *viewer = glb_viewer[player->playerNum];
    bool zoomed = glb_players[player->playerNum]->animState->animFlags & 1;
    HUD_Enable(player->hudInfo, Sight, 0, 0);
    HUD_Enable(player->hudInfo, Camera, 0, 0);
    HUD_Enable(player->hudInfo, OICW, 0, 0);
    HUD_Enable(player->hudInfo, Laser, 0, 0);

    ushort crosshair;
    if (zoomed) {
        AnimState *anim = glb_players[player->playerNum]->animState;
        if (weapon_data[anim->currentWeaponId].weaponFlags & 0x40) {
            switch (anim->currentWeaponId) {
                case 0x1a:
                case 0x1b:
                    HUD_Enable(player->hudInfo, OICW, 1, 0);
                    break;
                case 0x32:
                case 0x33:
                    HUD_Enable(player->hudInfo, Laser, 1, 0);
                    break;
                case Weap_Camera_Upgraded:
                    anim->currentWeaponId = Weap_Camera;
                    glb_players[player->playerNum]->animState->switchingToWeaponId = Weap_Camera;
                    // fall through
                case Weap_Camera:
                    HUD_Enable(player->hudInfo, Camera, 1, 0);
                    break;
                default:
                    HUD_Enable(player->hudInfo, Sight, 1, 0);
                    break;
            }
            spr->maybeEnabled = 0xff;
            return;
        }
        crosshair = 1;
    } else {
        crosshair = (uchar)player->crosshairType;
    }

    CrosshairInfo *info = &HUDCrossCoords[crosshair];
    spr->spritesheetX = info->spritesheetX;
    spr->spritesheetY = info->spritesheetY;
    spr->spritesheetWidth = info->width;
    spr->onscreenWidth = info->width;
    spr->spritesheetHeight = info->height;
    spr->onscreenHeight = info->height;
    spr->colourTint = viewer->nightVisionRelated == 1 ? 0x208020ff : 0xff0000ff;
    // On the x87: y first, held while x is worked out
    double y = (double)player->crosshairOffsetY * -0.5f * viewer->height + (double)viewer->height * 0.5f;
    double x = (double)player->crosshairOffsetX * 0.5f * viewer->width + (double)viewer->width * 0.5f;
    spr->positionX = (short)Ftol(x);
    spr->positionY = (short)Ftol(y);
    spr->maybeEnabled = 0x31;
    if (!zoomed)
        spr->maybeEnabled = PlayerInputs[player->playerNum].crosshairsEnabled ? 0x31 : 0xff;
    if (player->camMode)
        spr->maybeEnabled = 0xff;
}

// EAX the player, ESI the crosshair
// AUTOLTCG
void __declspec(naked) HUD_UpdateCrossHair(void) {
    _asm {
        push ecx
        push edx
        push esi
        push eax
        call _HUD_UpdateCrossHair
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The night vision goggles in single player: putting them on (the weapon 0x5d once it is in hand) turns night vision
// on and goes back to the weapon before; after that their button steps night vision, x-ray and off. The battery runs
// down while on and charges three times as fast while off, to 1800 frames; empty, both go off.
void _HUD_MonitorNightSight(BLData *player) {
    obj_tag *obj = glb_players[player->playerNum];
    if (!player->active || obj->curState != 1 || MPSettings.isMultiplayer || obj->objectType == OBJECTTYPE_DEAD_PLAYER)
        return;
    short movement = obj->subState;
    if (movement != MovementType_Walk && (movement <= MovementType_Grapple || movement > MovementType_ScanMode))
        return;
    viewer_tag *viewer = glb_viewer[player->playerNum];
    if (viewer == NULL)
        return;

    if (obj->animState->currentWeaponId == Weap_MaybeNightvision) {
        if (player->weaponObject->curState != 0)
            return;
        viewer->nightVisionRelated = 1;
        HUD_Enable(player->hudInfo, NightSight, 1, 0);
        obj->animState->switchingToWeaponId = obj->animState->prevHeldWeaponId;
        player->nightVisionActive = 1;
        char next = obj->animState->switchingToWeaponId;
        if (next == 0x47 || (next > 0x5c && next <= 0x5e))
            obj->animState->switchingToWeaponId = 1;
        return;
    }

    if (Input_Action(player->playerNum, ACTION_NIGHTVISION, 4) && !(obj->animState->animFlags & 1)) {
        if (!player->nightVisionActive) {
            obj->animState->switchingToWeaponId = Weap_MaybeNightvision;
        } else {
            switch ((ushort)viewer->nightVisionRelated) {
                case 0:
                    viewer->nightVisionRelated = 1;
                    HUD_Enable(player->hudInfo, NightSight, 1, 0);
                    break;
                case 1:
                    viewer->nightVisionRelated = 2;
                    HUD_Enable(player->hudInfo, NightSight, 0, 0);
                    HUD_Enable(player->hudInfo, Xray, 1, 0);
                    break;
                case 2:
                    viewer->nightVisionRelated = 0;
                    HUD_Enable(player->hudInfo, NightSight, 0, 0);
                    HUD_Enable(player->hudInfo, Xray, 0, 0);
                    break;
            }
        }
    }

    if (viewer->nightVisionRelated != 0) {
        player->nightVisionTimer = player->nightVisionTimer - FRAME_RATE_MUL;
    } else {
        double charged = (double)FRAME_RATE_MUL * 3.0f + player->nightVisionTimer;
        if (1800.0f < charged)
            charged = 1800.0f;
        player->nightVisionTimer = (float)charged;
    }
    if (player->nightVisionTimer <= 0.0f && (ushort)viewer->nightVisionRelated > 0 &&
        (ushort)viewer->nightVisionRelated <= 2) {
        player->nightVisionTimer = 0.0f;
        viewer->nightVisionRelated = 0;
        HUD_Enable(player->hudInfo, NightSight, 0, 0);
        HUD_Enable(player->hudInfo, Xray, 0, 0);
    }
}

// EAX the player
// AUTOLTCG
void __declspec(naked) HUD_MonitorNightSight(void) {
    _asm {
        push ecx
        push edx
        push eax
        call _HUD_MonitorNightSight
        add esp, 4
        pop edx
        pop ecx
        ret
    }
}

// AUTOINJECT
void HUD_Update(BLData *playerInfo, obj_tag *obj) {

	if(playerInfo == NULL || obj == NULL || playerInfo->hudInfo == NULL)
		return;

	_HUD_UpdateCrossHair(playerInfo, playerInfo->hudInfo->crosshairSprite);
	_HUD_MonitorNightSight(playerInfo);

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

// The bounding box of a pane's sprite layout from the origin, leaving out the sprites clipped to the view
// AUTOINJECT
void HUD_CalcWidthHeight(HUDPANECREATE_tag *create, short *width, short *height) {
    if (create == NULL)
        return;
    short left = 0, top = 0, right = 0, bottom = 0;
    for (short i = 0; i < create->numSprites; i++) {
        SpriteInfo *info = &create->spriteInfo[i];
        if (info->maybeFlags & SPRITEINFO_CLIP_TO_VIEW)
            continue;
        if (left >= info->posX)
            left = info->posX;
        if (top >= info->posY)
            top = info->posY;
        if (right <= info->posX + info->onscreenWidth)
            right = info->posX + info->onscreenWidth;
        if (bottom <= info->posY + info->onscreenHeight)
            bottom = info->posY + info->onscreenHeight;
    }
    if (width != NULL)
        *width = right - left;
    if (height != NULL)
        *height = bottom - top;
}

// Keeps a pane of width x height inside the player's view (HUDPANE_KEEP_IN_X, _Y: 50 pixels in at the sides and
// bottom, 40 at the top, but nothing at an edge another player's view meets in split screen) and makes it relative
// to the view (HUDPANE_VIEW_X, _Y)
// AUTOINJECT
void HUD_ValidateXY(short *x, short *y, ushort width, ushort height, ushort player, uint flags) {
    viewer_tag *viewer = glb_viewer[player];
    float viewWidth = viewer->width;
    float viewHeight = viewer->height;
    double viewX = viewer->xMin;
    float viewY = viewer->yMin;
    ushort players = MPSettings.isMultiplayer ? (ushort)MPSettings.numPlayers : 1;

    ushort right = (ushort)Ftol(viewWidth - 50.0);
    ushort bottom = (ushort)Ftol(viewHeight - 50.0);
    ushort minX = (ushort)Ftol(50.0 + viewX);
    ushort maxX = (ushort)Ftol(right + viewX);
    ushort minY = (ushort)Ftol(viewY + 40.0);
    ushort maxY = (ushort)Ftol(bottom + (double)viewY);

    if (players == 2) {
        if (MultiplayerLayout_LeftRightOrTopBtm == 1) {
            if (player == 0)
                maxX = (ushort)Ftol(viewWidth);
            else if (player == 1)
                minX = (ushort)Ftol(viewX);
        } else {
            if (player == 0)
                maxY = (ushort)Ftol(viewHeight);
            else if (player == 1)
                minY = (ushort)Ftol(viewY);
        }
    } else if (players > 2 && players <= 4) {
        switch (player) {
            case 0:
                maxX = (ushort)Ftol(viewWidth);
                maxY = (ushort)Ftol(viewHeight);
                break;
            case 1:
                minX = (ushort)Ftol(viewX);
                maxY = (ushort)Ftol(viewHeight);
                break;
            case 2:
                maxX = (ushort)Ftol(viewWidth);
                minY = (ushort)Ftol(viewY);
                break;
            case 3:
                minX = (ushort)Ftol(viewX);
                minY = (ushort)Ftol(viewY);
                break;
        }
    }

    int lowest = *y + height;
    if (flags & HUDPANE_KEEP_IN_X) {
        if (maxX < *x + width)
            *x = maxX - width;
        if (*x < minX)
            *x += minX;
    }
    if (flags & HUDPANE_KEEP_IN_Y) {
        if (lowest > maxY)
            *y = maxY - height;
        if (*y < minY)
            *y += minY;
    }
    if (flags & HUDPANE_VIEW_X)
        *x = (short)Ftol(*x + viewX);
    if (flags & HUDPANE_VIEW_Y)
        *y = (short)Ftol(*y + (double)viewY);
}

// Makes a pane's sprites from its layout, at its position kept to the view, with numExtraItems words cleared
// after them. A sprite with SPRITEINFO_CLIP_TO_VIEW is cut to the view.
// AUTOINJECT
void HUD_CreateDefault(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *unused) {
    pane->numSprites = create->numSprites;
    pane->spriteList = (sprite **)Mem_Malloc((create->numExtraItems + (ushort)pane->numSprites) * sizeof(void *),
                                             0x2704, 0);
    pane->extraItems = (void **)&pane->spriteList[(ushort)pane->numSprites];
    for (ushort i = 0; i < create->numExtraItems; i++)
        pane->extraItems[i] = NULL;
    pane->updateFunction = create->updateFunc;
    pane->posX = create->posX;
    pane->posY = create->posY;
    pane->width = create->width;
    pane->height = create->height;

    viewer_tag *viewer = glb_viewer[blData->playerNum];
    float viewLeft = viewer->xMin;
    float viewRight = viewer->xMax + viewer->xMin;
    float viewBottom = viewer->yMax + viewer->yMin;
    HUD_ValidateXY(&pane->posX, &pane->posY, create->width, create->height, blData->playerNum, create->flags);

    for (ushort i = 0; i < (ushort)pane->numSprites; i++) {
        SpriteInfo *info = &create->spriteInfo[i];
        pane->spriteList[i] = Sprite_Create2(info);
        sprite *spr = pane->spriteList[i];
        spr->positionX += pane->posX;
        spr->positionY += pane->posY;
        if (info->textRef != Action_TranslatedText_NULLVALUE) {
            spr->backupOnscreenWidth = spr->positionX;
            spr->backupOnscreenHeight = spr->positionY;
        }
        spr->maybeEnabled = (uchar)info->maybeEnabled;
        if (info->textRef == 0)
            Txt_LockString(spr->text);

        if (info->maybeFlags & SPRITEINFO_CLIP_TO_VIEW) {
            int left = spr->positionX;
            if ((double)(spr->positionX + spr->onscreenWidth) > viewRight)
                spr->onscreenWidth = (short)Ftol((double)viewRight - left);
            int top = spr->positionY;
            if ((double)(spr->positionY + spr->onscreenHeight) > viewBottom)
                spr->onscreenHeight = (short)Ftol((double)viewBottom - top);
            if ((double)left < viewLeft) {
                spr->onscreenWidth = (short)Ftol(spr->onscreenWidth - ((double)viewLeft - left));
                spr->positionX = (short)Ftol(viewLeft);
            }
        }
    }
}


// AUTOINJECT
void HUD_CreateShrink(BLData *playerInfo, HUDPANE_tag *pane, HUDPANECREATE_tag *param_3, obj_tag *param_4) {
	  
	HUD_CalcWidthHeight(param_3, &param_3->width, &param_3->height);
	HUD_CreateDefault(playerInfo, pane, param_3, param_4);
	viewer_tag *vwr = glb_viewer[playerInfo->playerNum];

	for(int i = 0; i < pane->numSprites; i++) {

		sprite* s = pane->spriteList[i];

		// In a view narrower than the screen (split screen), half the size, and moved in by half the layout's offset
		if (vwr->width < (float)SCREEN_WIDTH) {
			s->onscreenWidth >>= 1;
			s->positionX -= param_3->spriteInfo[i].posX >> 1;
		}

		if (vwr->height < (float)SCREEN_HEIGHT) {
			s->onscreenHeight >>= 1;
			s->positionY -= param_3->spriteInfo[i].posY >> 1;
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
static HUDPANECREATE_tag MsgMissionStatusPane = {0, 167, 640, 103, HUD_CreateMissionStatusPane, HUD_UpdateStatusPane, (SpriteInfo*)0x00181408 /* mission status sprites */, 3, 4, 16, 0, 0};
// The objective pane (message type 2): its width is 0 in the XBE and filled in before main by
// HUD_InitObjectiveStatusPaneWidth, like the info and pickup panes. Its sprite info (6 sprites) is still the game's.
// XBE_GLOBAL(0x001813ec, 0x1c)
static HUDPANECREATE_tag MsgObjectiveStatusPane = {160, 0, 0 /* HUD_InitObjectiveStatusPaneWidth */, 0, HUD_CreateObjectiveStatusPane, HUD_UpdateStatusPane, (SpriteInfo*)0x00181490 /* objective status sprites */, 6, 4, 16, 0, 0};
// XBE_GLOBAL(0x00181598, 0x1c)
static HUDPANECREATE_tag MsgInfoStatusPane = {160, 137, 0 /* HUD_InitInfoStatusPaneWidth */, 0, HUD_CreateInfoStatusPane, HUD_UpdateStatusPane, (SpriteInfo*)0x00181698 /* InfoStatusSprInfo */, 5, 4, 16, 0, 0};
// XBE_GLOBAL(0x0017fe8c, 0x1c)
static HUDPANECREATE_tag AirPane = {0, 0, 640, 480, HUD_CreateDefault, HUD_UpdateAirPane, (SpriteInfo*)0x0017fe08 /* AirSprInfo */, 3, 0, 0, 0, 0};
// XBE_GLOBAL(0x00180218, 0x1c)
static HUDPANECREATE_tag NightSightPane = {0, 0, 640, 480, HUD_CreateShrink, HUD_UpdateNightSightPane, (SpriteInfo*)0x00180110 /* NightSightSprInfo */, 6, 0, 384, 0, 0};
// XBE_GLOBAL(0x0018036c, 0x1c)
static HUDPANECREATE_tag LensFlarePane = {0, 0, 640, 480, HUD_CreateDefault, HUD_UpdateLensFlarePane, (SpriteInfo*)0x00180238 /* LensFlareSprInfo */, 7, 0, 0, 0, 0};
// XBE_GLOBAL(0x00180690, 0x1c)
static HUDPANECREATE_tag RCCarPane = {0, 0, 640, 480, HUD_CreateShrink, HUD_UpdateCarPane, (SpriteInfo*)0x00180588 /* RCCarSprInfo */, 6, 0, 384, 0, 0};
// XBE_GLOBAL(0x001800f4, 0x1c)
static HUDPANECREATE_tag CameraPane = {0, 0, 640, 480, HUD_CreateDefault, HUD_UpdateCameraPane, (SpriteInfo*)0x0017ff68 /* CameraSprInfo */, 9, 1, 0, 0, 0};
// XBE_GLOBAL(0x001807d4, 0x1c)
static HUDPANECREATE_tag XrayPane = {0, 0, 640, 480, HUD_CreateDefault, HUD_UpdateXRayPane, (SpriteInfo*)0x001806f8 /* XraySprInfo */, 5, 0, 0, 0, 0};
// XBE_GLOBAL(0x00180924, 0x1c)
static HUDPANECREATE_tag SecCamPane = {0, 0, 640, 480, HUD_CreateDefault, HUD_UpdateSecCamPane, (SpriteInfo*)0x001807f0 /* SecCamSprInfo */, 7, 0, 0, 0, 0};
// XBE_GLOBAL(0x00180a74, 0x1c)
static HUDPANECREATE_tag OICWPane = {0, 0, 640, 480, HUD_CreateOICWPane, HUD_UpdateOICWPane, (SpriteInfo*)0x00180940 /* OICWSprInfo */, 7, 0, 384, 0, 0};
// XBE_GLOBAL(0x00180b98, 0x1c)
static HUDPANECREATE_tag RoninPane = {0, 0, 640, 480, HUD_CreateShrink, (HUDPANE_updateFunc)0x000e0ec0 /* __profiling_or_debugging_hook_point */, (SpriteInfo*)0x00180a90 /* RoninSprInfo */, 6, 0, 384, 0, 0};
// XBE_GLOBAL(0x00180cc0, 0x1c)
static HUDPANECREATE_tag LaserPane = {0, 0, 640, 480, HUD_CreateShrink, (HUDPANE_updateFunc)0x000e0ec0 /* __profiling_or_debugging_hook_point */, (SpriteInfo*)0x00180bb8 /* LaserSprInfo */, 6, 0, 384, 0, 0};
// XBE_GLOBAL(0x00180ec4, 0x1c)
static HUDPANECREATE_tag SpacePane = {0, 0, 640, 480, HUD_CreateDefault, HUD_UpdateSpacePane, (SpriteInfo*)0x00180ce0 /* SpaceSprInfo */, 11, 0, 0, 0, 0};
// XBE_GLOBAL(0x00181774, 0x1c)
static HUDPANECREATE_tag MsgPickupStatusPane = {160, 372, 0 /* HUD_InitPickupStatusPaneWidth */, 0, HUD_CreatePickupStatusPane, HUD_UpdateStatusPane, (SpriteInfo*)0x001815b8 /* PickupStatusSprInfo */, 5, 4, 24, 0, 0};


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

// The message panes' sprites (still the game's), and the dynamic initialisers that give their text sprites the
// messages' formats. Not named on PS2; the names are ours.
#define MissionStatusSprInfo ((SpriteInfo *)0x00181408)
#define ObjectiveStatusSprInfo ((SpriteInfo *)0x00181490)
#define InfoStatusSprInfo ((SpriteInfo *)0x00181698)
#define PickupStatusSprInfo ((SpriteInfo *)0x001815b8)

// FUNC_AT(0x000f5480)
void HUD_InitMissionStatusFormats(void) {
    MissionStatusSprInfo[0].maybeClippedText = TextMsgFormats[TXTMSG_MISSION].font;
}

// FUNC_AT(0x000f5490)
void HUD_InitObjectiveStatusFormats(void) {
    ObjectiveStatusSprInfo[0].maybeClippedText = TextMsgFormats[TXTMSG_OBJECTIVE].font;
    ObjectiveStatusSprInfo[1].maybeClippedText = TextMsgFormats[TXTMSG_MISSION].font;
    ObjectiveStatusSprInfo[5].maybeClippedText = TextMsgFormats[TXTMSG_OBJECTIVE].font;
}

// FUNC_AT(0x000f54c0)
void HUD_InitInfoStatusFormats(void) {
    InfoStatusSprInfo[0].maybeClippedText = TextMsgFormats[TXTMSG_INFO].font;
    InfoStatusSprInfo[1].maybeClippedText = TextMsgFormats[TXTMSG_MISSION].font;
}

// FUNC_AT(0x000f54f0)
void HUD_InitPickupStatusFormats(void) {
    PickupStatusSprInfo[0].maybeClippedText = TextMsgFormats[TXTMSG_OBJECTIVE].font;
    PickupStatusSprInfo[1].maybeClippedText = TextMsgFormats[TXTMSG_MISSION].font;
}


// TODO: Change from 640x480 to generic
// XBE_GLOBAL(0x00180388, 0x1e4)
SpriteInfo RedeemerSpriteInfo[] = {
	{0x7f7f7f78, 0x7f7f7fff, 0x0022, 0x0500, 256, 	176, 	128, 			128, 			1, 	1, 	127, 	127, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_RLAUNCH_UI_5E, 5, 0, 0, 0}, // 0: Central reticle. The image is only 1/4 of the whole - repeats mirrored in H and V?
	{0xffffff40, 0x7f7f7fff, 0x0022, 0x0100, 0,		0,		SCREEN_WIDTH, 	SCREEN_HEIGHT,	0, 	0, 	127, 	255, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_RLAUNCH_UI_4A, 5, 0, 0, 0}, // 1: Static lines
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



// The guided missile's sight's pieces
#define GFX_REDEEMER_SIGHT      ((HASHCODE)0x0200045e)
#define GFX_REDEEMER_SIGHT_ARM  ((HASHCODE)0x02000465)

// The guided missile's view: the shrunk pane, and in single player its extra items made as objects (the rotating
// parts of the sight), in the room of view 5, whose models are its pieces 0x0200045e and 0x02000465
// AUTOINJECT
void HUD_CreateRedeemer(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj) {
    HUD_CreateShrink(blData, pane, create, obj);
    obj_tag **parts = (obj_tag **)pane->extraItems;
    for (ushort i = 0; i < create->numExtraItems; i++) {
        if (MPSettings.isMultiplayer) {
            parts[i] = NULL;
            continue;
        }
        parts[i] = Control_CreateObjEx(0, &CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, NULL,
                                       hashtable_hashcode_to_celglist(GFX_REDEEMER_SIGHT), NULL, 0, 0x20, 2.0f, 0,
                                       0xff, 0xff, 0xff);
        parts[i]->position.z = 1.0f;
        parts[i]->displayMask = 0x20;
        parts[i]->effectFlags |= 0x10;
        build_LinkToRoom(parts[i], 0, glb_viewer[5]->world);
    }
    if (parts[0] != NULL)
        hashtable_set_object_to_entity_gfx(parts[0], GFX_REDEEMER_SIGHT);
    if (parts[1] != NULL)
        hashtable_set_object_to_entity_gfx(parts[1], GFX_REDEEMER_SIGHT_ARM);
    if (parts[2] != NULL)
        hashtable_set_object_to_entity_gfx(parts[2], GFX_REDEEMER_SIGHT_ARM);
}

// One of the lens flare's rings: at along of the way from the centre of the view (cx, cy) to the sun on the screen,
// scale times its picture's size, in the flare's alpha. Our name.
void _HUD_PlaceLensFlare(sprite *ring, _VECTOR sun, float cx, float cy, float along, float scale, uchar alpha) {
    ring->onscreenWidth = (short)Ftol((ushort)ring->backupOnscreenWidth * (double)scale);
    ring->onscreenHeight = (short)Ftol((ushort)ring->backupOnscreenHeight * (double)scale);
    ring->positionX = (short)Ftol(((double)sun.x - cx) * along + cx);
    ring->positionY = (short)Ftol(((double)cy - sun.y) * along + cy);
    ring->maybeEnabled = 0x31;
    ring->colourTint = alpha | 0xffffff00;
}

// ESI the ring; the rest on the stack
// AUTOLTCG
void __declspec(naked) HUD_PlaceLensFlare(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 40]   // alpha
        push dword ptr [esp + 40]   // scale
        push dword ptr [esp + 40]   // along
        push dword ptr [esp + 40]   // cy
        push dword ptr [esp + 40]   // cx
        push dword ptr [esp + 40]   // the sun, by value
        push dword ptr [esp + 40]
        push dword ptr [esp + 40]
        push esi
        call _HUD_PlaceLensFlare
        add esp, 36
        pop edx
        pop ecx
        ret
    }
}

// The lens flare: while the sun shows in the sky of the player's view, its seven rings along the line from the sun
// through the centre, brighter (16 less than the first ring's alpha, plus 32) while the sun is on screen and in sight
// of the player's head (which also sets BLData.ambientBoost); the last ring larger the nearer the sun is to the centre
// AUTOINJECT
void HUD_UpdateLensFlarePane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj) {
    viewer_tag *viewer = glb_viewer[blData->playerNum];
    blData->ambientBoost = 0;
    if (!pane->enabled || (SkySunPos.x == 0.0f && SkySunPos.y == 0.0f && SkySunPos.z == 0.0f))
        return;

    _VECTOR sun;
    Vec_Zero(&sun);
    if (viewer->draw.skyVisible) {
        int alpha = (short)(uchar)pane->spriteList[0]->colourTint - 16;
        if (alpha < 0)
            alpha = 0;
        _VECTOR head;
        Player_GetHeadPos(&head, obj, NULL);
        Vec_Subtract(&head, &SkySunPos, &sun);
        if (Collide_LineOfSight(&head, &sun, obj->inCel, obj, NULL, 8))
            blData->ambientBoost = 1;
        bool show = false;
        if (View_3DPoint2Screen(&sun, &sun, blData->playerNum) > 0) {
            // Out from the centre by the zoom
            sun.x = (float)((double)sun.x - viewer->width * 0.5f);
            double y = (double)sun.y - viewer->height * 0.5f;
            sun.x = (float)(sun.x * (double)blData->zoom);
            sun.y = (float)(y * blData->zoom);
            sun.x = (float)(viewer->width * 0.5f + (double)sun.x);
            sun.y = (float)(viewer->height * 0.5f + (double)sun.y);
            if (blData->ambientBoost && sun.x > -5.0f && sun.x < 645.0f && sun.y > -5.0f && sun.y < 485.0f) {
                alpha = (short)alpha + 0x20;
                if (alpha > 0xff) {
                    alpha = 0xff;
                    show = true;
                }
            }
        }
        if (show || (short)alpha > 0) {
            float cx = viewer->width * 0.5f;
            float cy = viewer->height * 0.5f;
            uchar a = (uchar)alpha;
            _HUD_PlaceLensFlare(pane->spriteList[0], sun, cx, cy, 0.7f, 3.0f, a);
            _HUD_PlaceLensFlare(pane->spriteList[1], sun, cx, cy, 0.5f, 4.0f, a);
            _HUD_PlaceLensFlare(pane->spriteList[2], sun, cx, cy, 0.3f, 1.0f, a);
            _HUD_PlaceLensFlare(pane->spriteList[3], sun, cx, cy, -0.1f, 5.0f, a);
            _HUD_PlaceLensFlare(pane->spriteList[4], sun, cx, cy, -0.2f, 2.0f, a);
            _HUD_PlaceLensFlare(pane->spriteList[5], sun, cx, cy, -0.4f, 0.5f, a);
            double dy = (double)sun.y - cy;
            double dx = (double)sun.x - cx;
            double size = 256.0f - sqrt(dx * dx + dy * dy);
            if (!(0.0 <= size))
                size = 0.0;
            _HUD_PlaceLensFlare(pane->spriteList[6], sun, cx, cy, 1.0f, (float)(size * 0.015625f), a);
            return;
        }
    }
    for (int i = 0; i < 7; i++)
        pane->spriteList[i]->maybeEnabled = 0xff;
}

// XBE_GLOBAL(0x0017f950, 0x58)
SpriteInfo BloodSprInfo[] = {
	{0x007f7fff, 0x7f7f7fff, 0x0009, 0x0200, 0, 	0, 		SCREEN_WIDTH, 	32,	0, 	0, 	0, 	0, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_BLOOD_DRIP, 5, 0, 0, 0}, // Drippy edge
	{0x007f7fff, 0x7f7f7fff, 0x0009, 0x0200, 0,		0,		SCREEN_WIDTH, 	64,	0, 	0, 	0, 	0, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_COLOUR_FILL, 5, 0, 0, 0}, // Colour fill
};


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
SpriteInfo SightSprInfo[] = { // Scope (square fitted to SCREEN_HEIGHT), left bar, right bar: the bars a pixel over it
	{0x7f7f7fff, 0x7f7f7fff, 0x0027, 0x0600, (SCREEN_WIDTH-SCREEN_HEIGHT)/2, 	0, 		SCREEN_HEIGHT, 						SCREEN_HEIGHT,	0, 	0, 	0x1FF,	0x1FF, 	Action_TranslatedText_NULLVALUE, NULL, SPRITE_SNIPER_SCOPE, 5, 0, 0, 0}, // Scope graphic centre
	{0x7f7f7fff, 0x7f7f7fff, 0x0027, 0x0200, 0,									0,		(SCREEN_WIDTH-SCREEN_HEIGHT)/2 + 1, SCREEN_HEIGHT,	0, 	0, 	0, 		0, 		Action_TranslatedText_NULLVALUE, NULL, SPRITE_COLOUR_FILL, 5, 0, 0, 0}, // Fill left
	{0x7f7f7fff, 0x7f7f7fff, 0x0027, 0x0200, (SCREEN_WIDTH+SCREEN_HEIGHT)/2 - 1, 0, (SCREEN_WIDTH-SCREEN_HEIGHT)/2 + 1, SCREEN_HEIGHT,	0, 	0, 	0, 		0, 		Action_TranslatedText_NULLVALUE, NULL, SPRITE_COLOUR_FILL, 5, 0, 0, 0}, // Fill right
};

// XBE_GLOBAL(0x0017ff4c, 0x1c)
HUDPANECREATE_tag SightPane = {
	0,
	0,
	SCREEN_WIDTH,
	SCREEN_HEIGHT,
	HUD_CreateShrink,
	(HUDPANE_updateFunc)0x000e0ec0, // __profiling_or_debugging_hook_point
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

// The ammo pane's sprites (AmmoSprInfo, 0x0017f7d0)
#define AMMO_COUNT      0   // what is loaded and carried, or a percentage
#define AMMO_FIRE_MODE  1
#define AMMO_WEAPON     2   // the new weapon's name, for a second after a change
#define AMMO_ROUNDS     5   // the rounds left in the clip, a column of the ammo's picture
#define AMMO_CLIP       6   // the whole clip, behind them
#define AMMO_GRAPPLE    7   // the nearest grapple point in view

// Ammo types shown as a percentage of the clip
#define AMMO_TYPE_14    0x14
#define AMMO_TYPE_1A    0x1a
#define AMMO_TYPE_CHARGE 0x1f   // a charge that runs down (the clip less 12), and the emplacements' and Ronin's
#define WEAPON_GRAPPLE_A 0x50
#define WEAPON_GRAPPLE_B 0x51
#define GFX_RC_CAR       ((HASHCODE)0x02000194)

// XBE_GLOBAL(0x00181790, 0x220) readonly relocate
AmmoImage BulletImg[34] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {26.0f, 0.0f, 14.0f, 4.0f},
    {26.0f, 0.0f, 14.0f, 4.0f},
    {26.0f, 0.0f, 14.0f, 4.0f},
    {0.0f, 0.0f, 0.0f, 4.0f},
    {42.0f, 0.0f, 22.0f, 4.0f},
    {0.0f, 0.0f, 0.0f, 4.0f},
    {0.0f, 111.0f, 24.0f, 5.0f},
    {0.0f, 162.0f, 24.0f, 5.0f},
    {0.0f, 0.0f, 25.0f, 8.0f},
    {0.0f, 0.0f, 0.0f, 4.0f},
    {26.0f, 0.0f, 14.0f, 4.0f},
    {32.0f, 179.0f, 17.0f, 6.0f},
    {32.0f, 179.0f, 17.0f, 6.0f},
    {0.0f, 223.0f, 38.0f, 8.0f},
    {39.0f, 222.0f, 25.0f, 4.0f},
    {0.0f, 0.0f, 0.0f, 4.0f},
    {0.0f, 0.0f, 0.0f, 4.0f},
    {45.0f, 222.0f, 19.0f, 4.0f},
    {0.0f, 65.0f, 0.0f, 0.0f},
    {62.0f, 121.0f, 1.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {55.0f, 121.0f, 1.0f, 0.2f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {58.0f, 121.0f, 1.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 6.0e-45f, 6.0e-45f},
};

// The ammo of the weapon in hand: the count, the fire mode, the clip as a column of rounds, the new weapon's name
// for a second after a change; for an emplacement or Ronin their charge, for an RC vehicle its two counts; with a
// grappling hook, a marker on the nearest grapple point in view, coloured when in reach and the third-person icon
// says so.
// AUTOINJECT
void HUD_UpdateAmmoPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj) {
    if (blData == NULL || obj == NULL || pane == NULL || !pane->enabled)
        return;

    char weapon = obj->animState->currentWeaponId;
    weapon_definition_tag *def = &weapon_data[weapon];
    if (def->weaponVariantNum == 0x4b)
        def = &weapon_data[0x4a];
    if (def->weaponVariantNum == 0x4d)
        def = &weapon_data[0x4c];
    uint ammoType = def->ammoType;
    ushort loaded = blData->weaponStats[Player_AmmoIndex(weapon)].clipOrCooldown;
    uint flags = weapon_data[obj->animState->currentWeaponId].weaponFlags;
    Action_TranslatedText fireMode =
        def->fireModeName[(uchar)blData->weaponStats[obj->animState->currentWeaponId].fireModeIndex];
    sprite *count = pane->spriteList[AMMO_COUNT];
    sprite *mode = pane->spriteList[AMMO_FIRE_MODE];
    sprite *name = pane->spriteList[AMMO_WEAPON];
    sprite *rounds = pane->spriteList[AMMO_ROUNDS];
    sprite *clipSprite = pane->spriteList[AMMO_CLIP];
    sprite *grapple = pane->spriteList[AMMO_GRAPPLE];
    ushort clip = def->clipSize;
    bool showCount = !(flags & 0x20);

    if (ammoType == AMMO_TYPE_CHARGE) {
        loaded = clip - loaded;
        clip -= 12;
        if (loaded >= clip)
            loaded = clip;
    }
    if (obj->subState == MovementType_Emplacement) {
        ammoType = AMMO_TYPE_CHARGE;
        clip = 100;
        loaded = (ushort)Ftol(((GUNIMP *)blData->remoteControlDevice->extraObjectData)->heat * 0.5);
        if (loaded >= 100)
            loaded = 100;
    }
    if (obj->subState == MovementType_Ronin) {
        loaded = ((GUNTURRET *)blData->remoteControlDevice->extraObjectData)->ammo / 2;
        ammoType = AMMO_TYPE_CHARGE;
        clip = 100;
    }

    if (obj->subState == MovementType_RCVehicle) {
        // The car's or helicopter's rounds and machine-gun heat
        obj_tag *device = blData->remoteControlDevice;
        if (device == NULL)
            return;
        CAR_INFO *car = device->objGraphics == hashtable_hashcode_to_celglist(GFX_RC_CAR)
                            ? (CAR_INFO *)device->extraObjectData
                            : (CAR_INFO *)device->maybeParent->extraObjectData;
        sprintf(count->text, "%d", (ushort)car->mainAmmo);
        Sprite_SetText(count, count->text);
        sprintf(mode->text, "%d", Ftol(car->tankMachinegunTemperature));
        Sprite_SetText(mode, mode->text);
        name->maybeEnabled = 0xff;
        mode->maybeEnabled = 0x1c;
        return;
    }

    if (count != NULL && pane->numSprites > AMMO_COUNT) {
        if (!showCount || blData->weaponChangeTimer != 0) {
            count->length = 0;
        } else if (ammoType == AMMO_TYPE_14 || ammoType == AMMO_TYPE_1A || ammoType == AMMO_TYPE_CHARGE) {
            sprintf(count->text, "%d", loaded * 100 / clip);
            Sprite_SetText(count, count->text);
        } else {
            sprintf(count->text, "%d-%d", loaded, blData->ammo[ammoType]);
            Sprite_SetText(count, count->text);
        }
    }

    if (mode != NULL && pane->numSprites > AMMO_FIRE_MODE) {
        sprintf(mode->text, "%s", fireMode != 0 ? Txt_BindLabel(fireMode, 0) : "~-~");
        Sprite_SetText(mode, mode->text);
    }

    if (name != NULL && pane->numSprites > AMMO_WEAPON) {
        // A change to another weapon (not another variant) shows its name for a second instead of the fire mode
        AnimState *anim = obj->animState;
        if (anim->switchingToWeaponId != anim->currentWeaponId &&
            weapon_data[anim->switchingToWeaponId].weaponBaseNum != weapon_data[anim->currentWeaponId].weaponBaseNum)
            blData->weaponChangeTimer = (char)Ftol(_FRAME_RATE);
        if (blData->weaponChangeTimer != 0) {
            blData->weaponChangeTimer--;
            name->maybeEnabled = 0x1c;
            mode->maybeEnabled = 0xff;
        } else {
            name->maybeEnabled = 0xff;
            mode->maybeEnabled = 0x1c;
        }
        // The name is the format (as the original)
        weapon_definition_tag *next = &weapon_data[obj->animState->switchingToWeaponId];
        Action_TranslatedText label = MPSettings.isMultiplayer ? next->nameShort : next->nameLong;
        if (label != Action_TranslatedText_NULLVALUE)
            sprintf(name->text, Txt_BindLabel(label, 0));
        else
            name->text[0] = '\0';
        Sprite_SetText(name, name->text);
    }

    // The clip: a column of rounds, double width for a big clip, over the empty clip
    if (rounds != NULL && pane->numSprites > AMMO_ROUNDS) {
        if (blData->weaponChangeTimer != 0) {
            rounds->maybeEnabled = 0xff;
        } else {
            double scale = clip >= 0x58 ? 16.0f : 1.0f;
            AmmoImage *image = &BulletImg[ammoType];
            rounds->maybeEnabled = 0x1c;
            float spent = (float)((clip - loaded) * (double)image->h);
            double height = loaded * (double)image->h;
            rounds->positionX = (short)Ftol(count->positionX - scale * image->w);
            rounds->positionY = (short)Ftol(pane->posY - height);
            short roundsHeight = (short)Ftol(height);
            rounds->onscreenHeight = roundsHeight;
            rounds->spritesheetX = (short)Ftol(image->u);
            rounds->spritesheetWidth = (short)Ftol(image->w);
            rounds->onscreenWidth = (short)Ftol(scale * image->w);
            rounds->spritesheetY = (short)Ftol((double)spent + image->v);
            rounds->spritesheetHeight = roundsHeight;
        }
    }
    if (clipSprite != NULL && pane->numSprites > AMMO_CLIP) {
        if (blData->weaponChangeTimer != 0) {
            clipSprite->maybeEnabled = 0xff;
        } else {
            double scale = clip >= 0x58 ? 16.0f : 1.0f;
            AmmoImage *image = &BulletImg[ammoType];
            clipSprite->maybeEnabled = 0x1d;
            double height = clip * (double)image->h;
            clipSprite->positionX = (short)Ftol(count->positionX - scale * image->w);
            clipSprite->positionY = (short)Ftol(pane->posY - height);
            short clipHeight = (short)Ftol(height);
            clipSprite->onscreenHeight = clipHeight;
            clipSprite->spritesheetHeight = clipHeight;
            clipSprite->spritesheetX = (short)Ftol(image->u);
            clipSprite->spritesheetWidth = (short)Ftol(image->w);
            clipSprite->onscreenWidth = (short)Ftol(scale * image->w);
            clipSprite->spritesheetY = (short)Ftol(image->v);
        }
    }

    if (grapple != NULL && pane->numSprites > AMMO_GRAPPLE) {
        grapple->maybeEnabled = 0xff;
        char held = obj->animState->currentWeaponId;
        float nearest = 10000.0f;
        if (held != WEAPON_GRAPPLE_A && held != WEAPON_GRAPPLE_B)
            return;
        for (obj_tag *point = control_first_object(); point != NULL; point = point->nextObject) {
            if (point->drawnCountdown == 0 || point->objectType != OBJECTTYPE_GRAPPLE)
                continue;
            if (!Collide_LineOfSight(&point->position, &glb_viewer[blData->playerNum]->pos, point->inCel, obj, point,
                                     8))
                continue;
            _VECTOR screen;
            if (View_3DPoint2Screen(&point->position, &screen, blData->playerNum) <= 0)
                continue;
            double distance = Vec_Dist3D(&point->position, &obj->position);
            if (!(distance < nearest))
                continue;
            nearest = (float)distance;
            grapple->maybeEnabled = 0x1c;
            grapple->positionX = (short)Ftol(screen.x);
            grapple->positionY = (short)Ftol((double)glb_viewer[blData->playerNum]->height - screen.y);
            if (distance < weapon_data[WEAPON_GRAPPLE_A].range && blData->thirdIcon == 2)
                grapple->colourTint = 0x00ff00ff;
            else
                grapple->colourTint = 0xffffffff;
        }
    }
}

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
        bondMoment->linkedViewer = (glb_viewer[4] != NULL && glb_viewer[4]->enabled != 0) ? 4 : 5;

        if (blData->bondMomentTimer <= 0) {
            blData->bondMomentTimer = 0;
            bondMoment->maybeEnabled = 0xff;
        } else {
            bondMoment->positionX = 320;
            bondMoment->positionY = 80;
            short t = blData->bondMomentTimer;
            if (t > 100) {
                // On the x87: the cosine by FCOS, unrounded, and the products with it
                double scale = (double)(200 - t) * (double)0.01f;
                Extended flip = FCos((double)(t * 4 - 400) * (double)0.03141593f);
                Extended size = (double)flip >= 0.0 ? flip : -flip;
                ushort height = (ushort)bondMoment->backupOnscreenHeight;
                bondMoment->onscreenHeight = (short)(int)(double)(Extended((double)height) * size * Extended(scale));
                // Upside down while the card shows its back
                bondMoment->spritesheetHeight = ((double)flip < 0.0) ? (short)-height : (short)height;
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
static HUDPANECREATE_tag MPAmmoPane = {640, 480, 0, 26, HUD_CreateAmmoPane, HUD_UpdateAmmoPane, (SpriteInfo*)0x00180f38 /* MPAmmoSprInfo */, 3, 4, 24, 0, 0};
// XBE_GLOBAL(0x00181138, 0x1c)
static HUDPANECREATE_tag MPHealthPane = {0, 0, 640, 480, HUD_CreateMPHealthPane, HUD_UpdateMPHealthPane, (SpriteInfo*)0x00180fd8 /* MPHealthSprInfo */, 8, 0, 384, 0, 0};
// XBE_GLOBAL(0x00181180, 0x1c)
static HUDPANECREATE_tag MPMsgInfoStatusPane = {0, 0, 640, 480, HUD_CreateInfoStatusPane, HUD_MPUpdateStatusPane, (SpriteInfo*)0x00181154 /* MPInfoStatusSprInfo */, 1, 4, 384, 0, 0};
// XBE_GLOBAL(0x001812d4, 0x1c)
static HUDPANECREATE_tag MPScorePane = {0, 0, 640, 480, HUD_CreateMPScorePane, HUD_MPUpdatePane, (SpriteInfo*)0x001811a0 /* MPScoreInfo */, 7, 0, 384, 0, 0};
// XBE_GLOBAL(0x001806d8, 0x1c)
static HUDPANECREATE_tag RadarPane = {0, 0, 640, 480, HUD_CreateRadar, HUD_RadarUpdate, (SpriteInfo*)0x001806ac /* RadarSprInfo */, 1, 28, 384, 0, 0};

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
	// Only players need the pane updated, not drones
	if (gameObj != NULL && (gameObj->objectType == OBJECTTYPE_DRONE || gameObj->objectType == OBJECTTYPE_DEAD_DRONE))
		return;

	// The pane's three game objects (the single player screen's turning parts; NULL in multiplayer)
	obj_tag **items = (obj_tag **)hudPane->extraItems;
	if (!hudPane->enabled) {
		if (items[0] != NULL)
			items[0]->effectFlags |= 0x10;
		if (items[1] != NULL)
			items[1]->effectFlags |= 0x10;
		if (items[2] != NULL)
			items[2]->effectFlags |= 0x10;
		return;
	}
	if (items[0] != NULL)
		items[0]->effectFlags &= ~0x10;
	if (items[1] != NULL)
		items[1]->effectFlags &= ~0x10;
	if (items[2] != NULL)
		items[2]->effectFlags &= ~0x10;

	// The player has left the missile: control returns to them
	if (gameObj->subState != MovementType_RemoteControl) {
		Player_SetCamMode(blData, 0);
		glb_viewer[blData->playerNum]->someCel = NULL;
		if (blData->hudInfo != NULL && blData->hudInfo->pane[Redeemer].maybeCanBeEnabled) {
			blData->hudInfo->pane[Redeemer].enabled = false;
			blData->hudInfo->pane[Redeemer].state = 0;
		}
	}

	short tint = (short)Rand_Rand(24) + 27;
	viewer_tag *viewer = glb_viewer[blData->playerNum];

	// A target marker over each helicopter on screen, flashing when it is near the middle
	hudPane->spriteList[10]->maybeEnabled = 0xff;
	for (COPTER *c = (COPTER *)CopterList.head; c != NULL; c = (COPTER *)c->node.next) {
		obj_tag *body = Copter_GetBody(c);
		_VECTOR at;
		if (body == NULL || View_3DPoint2Screen(&body->position, &at, (uchar)viewer->idx) <= 0)
			continue;
		if (at.x >= 0.0f) {
			if (at.x > 640.0f)
				at.x = 640.0f;
		} else {
			at.x = 0.0f;
		}
		if (at.y >= 0.0f) {
			if (at.y > 480.0f)
				at.y = 480.0f;
		} else {
			at.y = 0.0f;
		}
		sprite *marker = hudPane->spriteList[10];
		short y = (short)Ftol(Extended(viewer->height) - Extended(at.y) -
		                      Extended((double)(ushort)marker->backupOnscreenHeight));
		marker->positionX = (short)Ftol(Extended(at.x) - Extended((double)(ushort)marker->backupOnscreenWidth));
		hudPane->spriteList[10]->positionY = y;
		hudPane->spriteList[10]->maybeEnabled = 0x27;
		float dx = at.x - 320.0f;
		if (dx < 0.0f)
			dx = -dx;
		if (dx < 32.0f) {
			float dy = at.y - 240.0f;
			if (dy < 0.0f)
				dy = -dy;
			if (dy < 32.0f && (GameState.NumFramesUnpaused & 8))
				hudPane->spriteList[10]->maybeEnabled = 0xff;
		}
	}

	// Turn the pane off once the camera has left the missile
	if (blData->camMode != CamMode_Redeemer && blData->hudInfo != NULL &&
	    blData->hudInfo->pane[Redeemer].maybeCanBeEnabled) {
		blData->hudInfo->pane[Redeemer].enabled = false;
		blData->hudInfo->pane[Redeemer].state = 0;
	}

	// The static: brighter as the missile nears the end of its flight, full while the picture is lost
	Extended lifetime;
	if (blData->remoteControlDevice == NULL) {
		lifetime = Extended(1.0f);
		hudPane->spriteList[2]->maybeEnabled = 0x27;
	} else {
		BU_tag *missile = (BU_tag *)blData->remoteControlDevice->extraObjectData;
		lifetime = Extended(missile->distanceTravelled) / Extended(missile->wpnDef->range);
		hudPane->spriteList[2]->maybeEnabled = 0xff;
		if (items[0] != NULL && items[1] != NULL && items[2] != NULL) {
			float yaw = blData->remoteControlDevice->rotation.z;
			items[0]->rotation.z = (float)(Extended(1.5707964f) - Extended(yaw) + Extended(3.1415927f));
			items[0]->transformFlags |= TRANSFORM_MOVED;
			items[1]->rotation.z = (float)(Extended(blData->remoteControlDevice->rotation.z) - Extended(0.7853982f));
			items[1]->transformFlags |= TRANSFORM_MOVED;
			items[2]->rotation.z = (float)(Extended(blData->remoteControlDevice->rotation.z) + Extended(3.1415927f) -
			                               Extended(0.7853982f));
			items[2]->transformFlags |= TRANSFORM_MOVED;
		}
	}
	if (blData->remoteControlDevice == NULL || !(lifetime <= Extended(0.85f)))
		tint = (short)Ftol((lifetime - Extended(0.85f)) * Extended(3400.0f) + Extended((double)tint));
	hudPane->spriteList[1]->colourTint = (tint < 0x100 ? (uint)tint : 0xff) | 0x7f000000;

	// The scan lines move every other frame
	static uint8_t Scrl = 0;
	if (Scrl++ & 1)
		hudPane->spriteList[1]->spritesheetY =
			(ushort)Rand_Rand((ushort)hudPane->spriteList[1]->backupOnscreenHeight - 1);
}

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

            if (Text_UpdateMsg(blData->playerNum, data->msgType, &data->msg) && data->msg != NULL) {
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
            if (Text_UpdateMsg(blData->playerNum, data->msgType, &data->msg))
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

// The message panes: the default pane, and the type of message each takes off the queue and how fast its bar closes
// AUTOINJECT
void HUD_CreateMissionStatusPane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj) {
    HUD_CreateDefault(blData, pane, create, obj);
    StatusPaneData *data = (StatusPaneData *)pane->extraItems;
    data->state = STATUS_IDLE;
    data->msgType = TXTMSG_MISSION;
    data->shrinkStep = 32;
}

// AUTOINJECT
void HUD_CreateObjectiveStatusPane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj) {
    HUD_CreateDefault(blData, pane, create, obj);
    StatusPaneData *data = (StatusPaneData *)pane->extraItems;
    data->state = STATUS_IDLE;
    data->msgType = TXTMSG_OBJECTIVE;
    data->shrinkStep = 16;
}

// In multiplayer the info and pickup panes' text moves into the player's view, 72 pixels down
// AUTOINJECT
void HUD_CreateInfoStatusPane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj) {
    HUD_CreateDefault(blData, pane, create, obj);
    StatusPaneData *data = (StatusPaneData *)pane->extraItems;
    data->state = STATUS_IDLE;
    data->msgType = TXTMSG_INFO;
    data->shrinkStep = 16;
    if (MPSettings.isMultiplayer) {
        HUD_FixMP(pane->spriteList[0], blData->playerNum);
        pane->spriteList[0]->positionY += 72;
    }
}

// AUTOINJECT
void HUD_CreatePickupStatusPane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj) {
    HUD_CreateDefault(blData, pane, create, obj);
    StatusPaneData *data = (StatusPaneData *)pane->extraItems;
    data->state = STATUS_IDLE;
    data->msgType = TXTMSG_PICKUP;
    data->shrinkStep = 16;
    if (MPSettings.isMultiplayer) {
        HUD_FixMP(pane->spriteList[0], blData->playerNum);
        pane->spriteList[0]->positionY += 72;
    }
}

// The security camera's view: no night vision, and its recording light blinking
// AUTOINJECT
void HUD_UpdateSecCamPane(BLData *blData, HUDPANE_tag *pane, obj_tag *unused) {
    if (!pane->enabled)
        return;
    glb_viewer[blData->playerNum]->nightVisionRelated = 0;
    if ((GameState.NumFramesUnpaused & 0x1f) < 0x10)
        pane->spriteList[6]->colourTint = 0x7f7f7f78;
    else
        pane->spriteList[6]->colourTint = 0x7f7f7f00;
}

// Dead: blood runs down the view, its drip (sprite 0) a frame's worth at a time and the fill (1) behind it, until
// the drip is past the bottom of the view; the pane's state then goes to 1. Off, both go back to the top.
// AUTOINJECT
void HUD_UpdateBloodPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj) {
    if (blData == NULL || obj == NULL || pane == NULL)
        return;
    sprite *drip = pane->spriteList[0];
    sprite *fill = pane->spriteList[1];
    viewer_tag *viewer = glb_viewer[blData->playerNum];
    if (!pane->enabled) {
        drip->positionY = (short)Ftol(viewer->yMin);
        fill->onscreenHeight = 0;
        drip->onscreenHeight = 0;
        return;
    }
    double y = drip->positionY;
    double run = (short)Ftol(y - viewer->yMin);
    if (run >= viewer->height) {
        pane->state = 1;
        return;
    }
    int filled = Ftol(run + FRAME_RATE_MUL);
    drip->positionY = (short)Ftol(y + FRAME_RATE_MUL);
    fill->onscreenHeight = (short)filled;
    if (MPSettings.isMultiplayer) {
        double length = drip->onscreenHeight + (double)FRAME_RATE_MUL;
        float most = viewer->height - (short)filled;
        if (!(length < most))
            length = most;
        drip->onscreenHeight = (short)Ftol(length);
    } else {
        drip->onscreenHeight = (short)Ftol(drip->onscreenHeight + (double)FRAME_RATE_MUL);
    }
}

// The width of a pane's bar for a level of 0 to 1: 28 to 228 pixels, at least 1
static void HUD_SetBarWidth(sprite *bar, double level) {
    short width = (short)Ftol(level * 200.0f + 28.0f);
    if (width <= 1)
        width = 1;
    bar->spritesheetWidth = width;
    bar->onscreenWidth = width;
}

// The air bar under water (sprites 0 and 1); on a wire, in its other colour with sprite 2, the grip left
// AUTOINJECT
void HUD_UpdateAirPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj) {
    sprite *frame = pane->spriteList[0];
    sprite *bar = pane->spriteList[1];
    sprite *wire = pane->spriteList[2];
    if (!pane->enabled)
        return;
    double level = (obj->subState == MovementType_Wire ? 100.0 - (double)blData->ropeFatigue : blData->air) * 0.01f;
    if (blData->waterFlags & WATER_HEAD_UNDER) {
        bar->maybeEnabled = 0x1e;
        frame->maybeEnabled = 0x1f;
        if (obj->subState == MovementType_Wire) {
            bar->colourTint = 0x68d6b1ff;
            wire->maybeEnabled = 0x1f;
        } else {
            bar->colourTint = 0x68b1d6ff;
            wire->maybeEnabled = 0xff;
        }
    } else {
        bar->maybeEnabled = 0xff;
        frame->maybeEnabled = 0xff;
        wire->maybeEnabled = 0xff;
    }
    HUD_SetBarWidth(bar, level);
}

// The camera gadget: its flash (sprite 0) for 10 frames after a shot, and an effect on each drone in view that
// is alive, at its position (the photo's mark)
// AUTOINJECT
void HUD_UpdateCameraPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj) {
    if (!pane->enabled)
        return;
    int *flashFrames = (int *)pane->extraItems;
    if (blData->shotFired)
        *flashFrames = 10;
    else if (*flashFrames != 0)
        (*flashFrames)--;
    pane->spriteList[0]->maybeEnabled = *flashFrames != 0 ? 0x28 : 0xff;

    for (Drone_tag *drone = (Drone_tag *)NDrone2Info.head; drone != NULL; drone = drone->next) {
        obj_tag *body = drone->gameObj;
        if (body->drawnCountdown == 0 || !(drone->health > 0.0f) || body->curState == 1)
            continue;
        obj_tag *mark = control_create_object(sizeof(Effect_tag), &body->position, NULL, NULL);
        Effect_tag *effect = (Effect_tag *)mark->extraObjectData;
        mark->objectType = OBJECTTYPE_EFFECT;
        effect->life = 0;
        effect->fade = 0;
        hashtable_set_object_to_entity_gfx(mark, (HASHCODE)0x02000832);
        mark->effectFlags |= 0x2000;
        mark->maybeParent = obj;
    }
}

// Night vision (viewer->nightVisionRelated 1): the green tint flickering (sprite 0, its scanlines jumping every
// other frame), a light at the view in single player, and the battery's bar (sprite 4). Off when the view leaves
// night vision.
// AUTOINJECT
void HUD_UpdateNightSightPane(BLData *blData, HUDPANE_tag *pane, obj_tag *unused) {
    if (!pane->enabled)
        return;
    if (glb_viewer[blData->playerNum]->nightVisionRelated != 1) {
        pane->enabled = false;
        return;
    }
    if (glb_viewer[4]->enabled)
        return;
    pane->spriteList[0]->colourTint = (Rand_Rand(24) + 7) | 0xff0000;
    if (NightSightScanline++ & 1)
        pane->spriteList[0]->spritesheetY = Rand_Rand((ushort)pane->spriteList[0]->backupOnscreenHeight - 1);
    if (!MPSettings.isMultiplayer)
        Light_Create(&glb_viewer[blData->playerNum]->pos, 0xff, 0xff, 0xff, 32.0f, 0, 0, 1, 2.5f, 0, 0, 0, 0xffff);
    HUD_SetBarWidth(pane->spriteList[4], (double)blData->nightVisionTimer * (1.0f / 1800.0f));
}

// X-ray (viewer->nightVisionRelated 2): the tint flickering and its scanlines rolling (sprite 0), and the battery's
// bar (sprite 3). Off when the view leaves x-ray.
// AUTOINJECT
void HUD_UpdateXRayPane(BLData *blData, HUDPANE_tag *pane, obj_tag *unused) {
    if (!pane->enabled)
        return;
    if (glb_viewer[blData->playerNum]->nightVisionRelated != 2) {
        pane->enabled = false;
        return;
    }
    if (glb_viewer[4]->enabled)
        return;
    pane->spriteList[0]->colourTint = (Rand_Rand(24) + 7) | 0xffffff00;
    pane->spriteList[0]->spritesheetY++;
    pane->spriteList[0]->spritesheetY &= 0x1f;
    HUD_SetBarWidth(pane->spriteList[3], (double)blData->nightVisionTimer * (1.0f / 1800.0f));
}

// ---- Multiplayer

// The multiplayer health pane's sprites (MPHealthSprInfo, 0x00180fd8)
#define MPHEALTH_BACKING    0
#define MPHEALTH_COORDS     1   // the coordinates cheat's text
#define MPHEALTH_BAR        2   // the health bar
#define MPHEALTH_ARMOUR     3
#define MPHEALTH_HIT_FIRST  4   // 4-7: the hit-direction overlays

// HUD_RadarUpdate's working: the view's inverse and the bearing of a radar object
#define RadarInverse (*(_MATRIX *)0x002790e0)
#define RadarTarget (*(_VECTOR *)0x002790d4)
// XBE_GLOBAL(0x001819a0, 0xa0) readonly relocate
RadarIcon RadarIcons[8] = {
    {0, 0, 4, 4, 1},
    {0, 8, 8, 8, 0},
    {0, 8, 8, 8, 0},
    {0, 8, 8, 8, 0},
    {0, 8, 8, 8, 0},
    {8, 0, 8, 8, 0},
    {0, 8, 8, 8, 0},
    {0, 8, 8, 8, 0},
};
// The format of the players' names over their heads: font 2
#define PlayerNameFormat FontFormat2

// In split screen, a pane's sprites move up and left by half their layout's offset in a view half the screen's size
// (as HUD_CreateShrink, which also halves their size). Our name.
void _HUD_CreateShift(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj) {
    HUD_CalcWidthHeight(create, &create->width, &create->height);
    HUD_CreateDefault(blData, pane, create, obj);
    viewer_tag *viewer = glb_viewer[blData->playerNum];
    for (ushort i = 0; i < (ushort)pane->numSprites; i++) {
        sprite *spr = pane->spriteList[i];
        if (viewer->width < 640.0f)
            spr->positionX -= create->spriteInfo[i].posX >> 1;
        if (viewer->height < 480.0f)
            spr->positionY -= create->spriteInfo[i].posY >> 1;
    }
}

// EAX the player, EDI the pane, ESI the layout, the stack the object
// AUTOLTCG
void __declspec(naked) HUD_CreateShift(void) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        push esi
        push edi
        push eax
        call _HUD_CreateShift
        add esp, 16
        pop edx
        pop ecx
        ret
    }
}

// In multiplayer, keeps a sprite inside its player's view: 50 pixels in at the sides and 40 at the top and bottom,
// but nothing at an edge another player's view meets
// AUTOINJECT
void HUD_FixMP(sprite *spr, short player) {
    if (!MPSettings.isMultiplayer)
        return;
    viewer_tag *viewer = glb_viewer[player];
    short left = (short)Ftol(viewer->xMin);
    short right = (short)Ftol(viewer->xMax);
    short top = (short)Ftol(viewer->yMin);
    short bottom = (short)Ftol(viewer->yMax);
    switch (MPSettings.numPlayers) {
        case 1:
            right -= 50;
            top += 40;
            left += 50;
            bottom -= 40;
            break;
        case 2:
            if (MultiplayerLayout_LeftRightOrTopBtm == 1) {
                top += 40;
                if (player == 0)
                    left += 50;
                else
                    right -= 50;
                bottom -= 40;
            } else {
                right -= 50;
                left += 50;
                if (player != 0)
                    bottom -= 40;
                else
                    top += 40;
            }
            break;
        default:
            switch (player) {
                case 0:
                    left += 50;
                    top += 40;
                    break;
                case 1:
                    right -= 50;
                    top += 40;
                    break;
                case 2:
                    left += 50;
                    bottom -= 40;
                    break;
                case 3:
                    right -= 50;
                    bottom -= 40;
                    break;
            }
            break;
    }
    if (spr->positionX < left)
        spr->positionX = left;
    if (spr->positionY < top)
        spr->positionY = top;
    if (spr->positionX + spr->onscreenWidth > right)
        spr->positionX = right - spr->onscreenWidth;
    if (spr->positionY + spr->onscreenHeight > bottom)
        spr->positionY = bottom - spr->onscreenHeight;
}

// The multiplayer health pane: its bar and armour kept in the view, the armour (sprite 3) on the bar; the
// hit-direction overlays (4-7) halved in a small view
// AUTOINJECT
void HUD_CreateMPHealthPane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj) {
    _HUD_CreateShift(blData, pane, create, obj);
    HUD_FixMP(pane->spriteList[MPHEALTH_BACKING], blData->playerNum);
    HUD_FixMP(pane->spriteList[MPHEALTH_BAR], blData->playerNum);
    pane->spriteList[MPHEALTH_ARMOUR]->positionX = pane->spriteList[MPHEALTH_BACKING]->positionX + 8;
    pane->spriteList[MPHEALTH_ARMOUR]->positionY = pane->spriteList[MPHEALTH_BACKING]->positionY + 4;
    for (int i = MPHEALTH_HIT_FIRST; i < MPHEALTH_HIT_FIRST + HEALTH_NUM_HIT; i++) {
        if (glb_viewer[blData->playerNum]->width < 640.0f) {
            pane->spriteList[i]->onscreenWidth >>= 1;
            pane->spriteList[i]->positionX -= create->spriteInfo[i].posX >> 1;
        }
        if (glb_viewer[blData->playerNum]->height < 480.0f) {
            pane->spriteList[i]->onscreenHeight >>= 1;
            pane->spriteList[i]->positionY -= create->spriteInfo[i].posY >> 1;
        }
    }
}

// The multiplayer health and armour bars, the hit-direction flashes and the coordinates cheat's readout; no bars
// under a full-screen view
// AUTOINJECT
void HUD_UpdateMPHealthPane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj) {
    if (!pane->enabled)
        return;
    sprite *armour = pane->spriteList[MPHEALTH_ARMOUR];
    sprite *coords = pane->spriteList[MPHEALTH_COORDS];
    sprite *bar = pane->spriteList[MPHEALTH_BAR];
    sprite *backing = pane->spriteList[MPHEALTH_BACKING];
    for (int i = 0; i < HEALTH_NUM_HIT; i++)
        pane->spriteList[MPHEALTH_HIT_FIRST + i]->maybeEnabled = 0xff;

    if (blData->hitDirectionFade != 0) {
        for (int i = 0; i < HEALTH_NUM_HIT; i++) {
            if (blData->hitDirections & (1 << i)) {
                pane->spriteList[MPHEALTH_HIT_FIRST + i]->maybeEnabled = HUD_HIT_ON;
                pane->spriteList[MPHEALTH_HIT_FIRST + i]->colourTint = (uchar)blData->hitDirectionFade | 0xff000000;
            }
        }
        double faded = (uchar)blData->hitDirectionFade - (double)FRAME_RATE_MUL;
        if (!(0.0 <= faded))
            faded = 0.0;
        blData->hitDirectionFade = (char)Ftol(faded);
    }

    HUDINFO_tag *hud = blData->hudInfo;
    if (hud->pane[Camera].enabled || hud->pane[OICW].enabled || hud->pane[Ronin].enabled ||
        hud->pane[SecCam].enabled || hud->pane[Redeemer].enabled || hud->pane[RCCar].enabled) {
        backing->maybeEnabled = 0xff;
        bar->maybeEnabled = 0xff;
        armour->maybeEnabled = 0xff;
        return;
    }
    if (bar != NULL) {
        // The health over the player's most (100 and the handicap), held on the x87
        double left = blData->health / ((double)(int)MPSettings.Player[blData->playerNum].HealthModifier + 100.0f);
        short width = (short)Ftol(128.0f * left);
        bar->onscreenWidth = width;
        bar->spritesheetWidth = width;
        uint colour = HEALTH_COLOUR_HIGH;
        if (left < 0.5f)
            colour = HEALTH_COLOUR_MID;
        if (left < 0.2f)
            colour = HEALTH_COLOUR_LOW;
        bar->colourTint = colour | 0xff;
        bar->maybeEnabled = 0x1c;
        backing->maybeEnabled = HUD_ICON_ON;
    }
    if (armour != NULL) {
        if (blData->armor != 0.0f) {
            armour->maybeEnabled = 0x1b;
            armour->onscreenWidth = (short)Ftol(blData->armor * (double)2.56f);
            armour->spritesheetWidth = (short)Ftol(blData->armor * (double)2.56f);
        } else {
            armour->maybeEnabled = 0xff;
        }
    }
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
}

// The multiplayer message pane (one line, sprite 0, in the player's view): as HUD_UpdateStatusPane without the bar
// AUTOINJECT
void HUD_MPUpdateStatusPane(BLData *blData, HUDPANE_tag *pane, obj_tag *unused) {
    sprite *text = pane->spriteList[0];
    StatusPaneData *data = (StatusPaneData *)pane->extraItems;
    TXT_MSG *msg = data->msg;
    if (!pane->enabled)
        return;
    switch (data->state) {
        case STATUS_IDLE:
            for (ushort i = 0; i < (ushort)pane->numSprites; i++)
                pane->spriteList[i]->maybeEnabled = 0xff;
            if (Text_UpdateMsg(blData->playerNum, data->msgType, &data->msg) && data->msg != NULL)
                data->state = STATUS_SHOW;
            return;
        case STATUS_SHOW:
            text->maybeEnabled = HUD_HIT_ON;
            if (msg->spriteHash != HASHCODE_NONE) {
                hashtable_set_sprite(text, msg->spriteHash);
                text->text = NULL;
            } else {
                Sprite_SetText(text, msg->text);
            }
            data->state = STATUS_HOLD;
            return;
        case STATUS_HOLD:
            if (msg != NULL) {
                // Fade out over the last 15 frames
                if (msg->timer > 0.0f && msg->timer < 15.0f)
                    text->colourTint = (text->colourTint & 0xffffff00) | Ftol(msg->timer * 17.0);
                else
                    StatusPane_SetAlpha(text, 0xff);
            }
            if (Text_UpdateMsg(blData->playerNum, data->msgType, &data->msg))
                data->state = data->msg == NULL ? STATUS_CLOSE : STATUS_SHOW;
            return;
        case STATUS_CLOSE:
            text->maybeEnabled = 0xff;
            data->state = STATUS_IDLE;
            return;
        default:
            return;
    }
}

// The multiplayer score pane: every sprite kept in the view, and laid out for the game's mode
// AUTOINJECT
void HUD_CreateMPScorePane(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj) {
    _HUD_CreateShift(blData, pane, create, obj);
    for (int i = 0; i < 7; i++)
        HUD_FixMP(pane->spriteList[i], blData->playerNum);
    sprite **s = pane->spriteList;
    if (MPSettings.GameMode == GM_ASSASSIN) {
        s[1]->positionY -= 0x10;
        s[2]->positionY -= 0x20;
    } else if (MPSettings.GameMode == GM_UPLINK) {
        // The three uplinks side by side
        s[1]->positionY -= 0x30;
        s[2]->positionY -= 0x30;
        s[6]->positionY -= 0x30;
        s[2]->positionX = s[1]->positionX + 0x10;
        s[6]->positionX = s[2]->positionX + 0x10;
        hashtable_set_sprite(s[1], (HASHCODE)0x03000185);
        hashtable_set_sprite(s[2], (HASHCODE)0x03000185);
        hashtable_set_sprite(s[6], (HASHCODE)0x03000185);
    } else {
        s[1]->positionY -= 0x18;
        s[2]->positionY -= 0x18;
    }
    FuckOffSprite3 = 0;
    s[0]->positionX += 0x18;
    s[0]->positionY -= 3;
    // Without teams sprite 3 goes, and the score takes its place
    if (!MPSettings.maybeIsTeamGame && !MPSettings.field53_0x190 && MPSettings.GameMode != GM_ASSASSIN) {
        s[3]->maybeEnabled = 0xff;
        FuckOffSprite3 = 1;
        s[3]->positionY -= 0x11;
    } else {
        s[0]->positionY += 0x11;
    }
    s[4]->positionY = s[3]->positionY + 0x12;
    s[5]->positionY = s[4]->positionY + 0x12;
}

// The multiplayer score: the team's and the player's (sprite 0), and the icons of the game's mode - the flag carried,
// the blueprint, the GoldenEye's parts, the assassin and the target, the three uplinks' colours
// AUTOINJECT
void HUD_MPUpdatePane(BLData *blData, HUDPANE_tag *pane, obj_tag *obj) {
    if (!pane->enabled)
        return;
    if (obj != NULL && (obj->objectType == OBJECTTYPE_DRONE || obj->objectType == OBJECTTYPE_DEAD_DRONE))
        return;
    sprite *iconC = pane->spriteList[6];
    sprite *iconB = pane->spriteList[2];
    ushort team = (ushort)MPSettings.Player[blData->playerNum].TeamId;
    obj_tag *me = MPGame.players[blData->playerNum].playerObj;
    sprite *score = pane->spriteList[0];
    sprite *iconA = pane->spriteList[1];
    if (me != NULL && (me->objectType == OBJECTTYPE_DRONE || me->objectType == OBJECTTYPE_DEAD_DRONE))
        return;
    if (FuckOffSprite3)
        pane->spriteList[3]->maybeEnabled = 0xff;

    char *teamScore = MP_GetTeamScore(team);
    char *playerScore = MP_GetPlayerScore(blData->playerNum);
    if (teamScore != NULL && playerScore != NULL) {
        sprintf(score->text, "%s\n%s", teamScore, playerScore);
        score->unknown1 = team != 0 ? 0x0000ffff : 0xff0000ff;
    } else {
        if (teamScore == NULL && playerScore != NULL)
            sprintf(score->text, "%s", playerScore);
        else
            sprintf(score->text, "");
        score->unknown1 = 0xff;
    }
    Sprite_SetText(score, score->text);
    iconA->maybeEnabled = 0xff;
    iconB->maybeEnabled = 0xff;
    iconC->maybeEnabled = 0xff;

    switch (MPSettings.GameMode) {
        case GM_CTF:
            if (MP_IsFlagParent(me, team == 0)) {
                iconA->maybeEnabled = HUD_HIT_ON;
                hashtable_set_sprite(iconA, (HASHCODE)0x03000182);
                iconA->colourTint = team != 0 ? 0xff0000ff : 0x0040ffff;
            }
            return;
        case GM_BLUEPRINT:
            if (MP_IsBluePrintParent(me)) {
                iconA->maybeEnabled = HUD_HIT_ON;
                hashtable_set_sprite(iconA, (HASHCODE)0x0300018a);
            }
            return;
        case GM_UPLINK: {
            iconA->maybeEnabled = HUD_HIT_ON;
            iconB->maybeEnabled = HUD_HIT_ON;
            iconC->maybeEnabled = HUD_HIT_ON;
            sprite *uplinks[3] = {iconA, iconB, iconC};
            for (short i = 0; i < 3; i++) {
                switch (MP_getUplinkStatus(i)) {
                    case 0:
                        uplinks[i]->colourTint = 0xd22d35ff;
                        break;
                    case 1:
                        uplinks[i]->colourTint = 0x2d61d2ff;
                        break;
                    case 2:
                        uplinks[i]->colourTint = 0x7f7f7fff;
                        break;
                }
            }
            return;
        }
        case GM_GOLDENEYE:
            if (MP_ObjTeamHasGEObj(me, 0)) {
                iconA->maybeEnabled = HUD_HIT_ON;
                hashtable_set_sprite(iconA, (HASHCODE)0x0300018b);
            }
            if (MP_ObjTeamHasGEObj(me, 1)) {
                iconB->maybeEnabled = HUD_HIT_ON;
                hashtable_set_sprite(iconB, (HASHCODE)0x0300018c);
            }
            break;
        case GM_ASSASSIN:
            break;
        default:
            return;
    }
    if (MP_IsAssasin(me)) {
        iconA->maybeEnabled = HUD_HIT_ON;
        hashtable_set_sprite(iconA, (HASHCODE)0x03000180);
    }
    if (MP_IsTarget(me)) {
        iconB->maybeEnabled = HUD_HIT_ON;
        hashtable_set_sprite(iconB, (HASHCODE)0x03000189);
    }
}

// The radar: kept in the view, and its extra items made, a dot each, hidden, of the radar's texture
// AUTOINJECT
void HUD_CreateRadar(BLData *blData, HUDPANE_tag *pane, HUDPANECREATE_tag *create, obj_tag *obj) {
    HUD_CalcWidthHeight(create, &create->width, &create->height);
    _HUD_CreateShift(blData, pane, create, obj);
    HUD_FixMP(pane->spriteList[0], blData->playerNum);
    sprite **dots = (sprite **)pane->extraItems;
    for (ushort i = 0; i < create->numExtraItems; i++) {
        dots[i] = Sprite_CreateAndLink2Viewer(5);
        Sprite_SetParams(dots[i], 0, pane->posX, pane->posY, (HASHCODE)0x0300016f, 0x005000ff);
        dots[i]->onscreenWidth = 2;
        dots[i]->onscreenHeight = 2;
        dots[i]->maybeEnabled = 0xff;
        dots[i]->maybeFlags |= 0x900;
    }
}

// The radar (in the player's view): a dot for each radar object (MP_GetRadarObjects) in the first half of the extra
// items, at its bearing from the view out to 50 metres at the edge; then, with names over heads on (or in Assassin),
// each other player in sight: their name in the next items, and in Assassin the assassin's and the target's marks
// AUTOINJECT
void HUD_RadarUpdate(BLData *blData, HUDPANE_tag *pane, obj_tag *obj) {
    if (obj != NULL && (obj->objectType == OBJECTTYPE_DRONE || obj->objectType == OBJECTTYPE_DEAD_DRONE))
        return;
    sprite **dots = (sprite **)pane->extraItems;
    sprite *radar = pane->spriteList[0];
    viewer_tag *viewer = glb_viewer[blData->playerNum];
    float halfWidth = radar->onscreenWidth * 0.45f;
    float halfHeight = radar->onscreenHeight * 0.45f;
    Mat_Inverse(&viewer->viewMatrix, &RadarInverse);
    RADAROBJ *list = NULL;
    ushort count = MP_GetRadarObjects(obj, &list);
    radar->maybeEnabled = MPSettings.Player[blData->playerNum].SomeField2 ? 0x1e : 0xff;

    ushort used = 0;
    if (count != 0) {
        while (used < pane->base->numExtraItems) {
            RADAROBJ *thing = &list[used];
            RotPreTransVec(&RadarInverse, &thing->pos, &RadarTarget);
            double distance = Vec_NormaliseLen(&RadarTarget, &RadarTarget);
            double rise = (double)thing->pos.y - obj->transformMatrix.m[13];
            RadarTarget.z = -RadarTarget.z;
            RadarTarget.x = -RadarTarget.x;
            rise *= 0.125f;
            RadarTarget.y = (float)rise;
            if (rise < -1.0f)
                RadarTarget.y = -1.0f;
            else if (RadarTarget.y > 1.0f)
                RadarTarget.y = 1.0f;
            distance *= 0.02f;
            if (distance > 1.0f)
                distance = 1.0f;
            sprite *dot = dots[used];
            dot->positionX = (short)Ftol(((RadarTarget.x * distance * halfWidth + radar->onscreenWidth * 0.5f) +
                                          radar->positionX) + 0.5f);
            dot->positionY = (short)Ftol(((RadarTarget.z * distance * halfHeight + radar->onscreenHeight * 0.5f) +
                                          radar->positionY) + 0.5f);
            RadarIcon *icon = &RadarIcons[thing->icon];
            dot->spritesheetX = (short)icon->u;
            dot->spritesheetY = (short)icon->v;
            dot->spritesheetWidth = (short)icon->w;
            dot->onscreenWidth = dot->spritesheetWidth;
            dot->spritesheetHeight = (short)icon->h;
            dot->onscreenHeight = dot->spritesheetHeight;
            dot->colourTint = thing->colour;
            dot->maybeEnabled = 0x1c;
            used++;
            if (used >= count)
                break;
        }
    }
    for (; used < (ushort)(pane->base->numExtraItems >> 1); used++)
        dots[used]->maybeEnabled = 0xff;

    sprite *assassinMark = NULL;
    sprite *targetMark = NULL;
    if (MPSettings.GameMode == GM_ASSASSIN) {
        assassinMark = MPGame.radarMarks[blData->playerNum].assassin;
        targetMark = MPGame.radarMarks[blData->playerNum].target;
        if (assassinMark != NULL)
            assassinMark->maybeEnabled = 0xff;
        if (targetMark != NULL)
            targetMark->maybeEnabled = 0xff;
    }

    for (ushort i = 0; i < NUM_AGENTS; i++) {
        if (!MPSettings.ShowTeamAndNameOverhead && MPSettings.GameMode != GM_ASSASSIN)
            break;
        if (i == blData->playerNum)
            continue;
        obj_tag *other = MPGame.players[i].playerObj;
        if (other == NULL)
            continue;
        switch ((uchar)other->objectType) {
            case OBJECTTYPE_PLAYER:
                if (other->curState != 1)
                    continue;
                break;
            case OBJECTTYPE_DRONE: {
                // A bot: one alive and thinking
                Drone_tag *bot = (Drone_tag *)other->extraObjectData;
                if ((bot->flags & 0x600) || !(bot->flags & 0x100))
                    continue;
                if (bot->health < 0.0f || bot->health == 0.0f)
                    continue;
                if (other->objectType == OBJECTTYPE_DEAD_DRONE || (other->flags & 1))
                    continue;
                break;
            }
            case OBJECTTYPE_DEAD_DRONE:
            case OBJECTTYPE_DEAD_PLAYER:
                continue;
            default:
                break;
        }

        _VECTOR screen;
        screen.x = viewer->width;
        screen.y = viewer->height;
        screen.z = 0.0f;
        if (View_3DPoint2Screen(&other->position, &screen, blData->playerNum) <= 0)
            continue;
        char *name = MPSettings.Player[i].Name;
        GPOINT extent;
        Font_GetTextExtent(name, PlayerNameFormat, &extent, NULL, -1);
        float half = extent.x * 0.5f;
        if (!(screen.z < 100.0f) || !(screen.x > half) || !((double)viewer->width - half > screen.x))
            continue;
        obj_tag *me = MPGame.players[blData->playerNum].playerObj;
        if (!Collide_LineOfSight(&other->position, &me->position, other->inCel, other, me, 0x623))
            continue;

        if (MPSettings.ShowTeamAndNameOverhead) {
            // The name, over the player, kept 12 pixels inside the view's top and bottom; in their team's colour
            sprite *label = dots[used];
            label->maybeClippedString = PlayerNameFormat;
            Sprite_SetText(label, name);
            label->positionX = (short)Ftol((double)screen.x - half + viewer->xMin);
            double y;
            if (screen.y < 12.0f) {
                y = 12.0f;
            } else {
                y = (double)viewer->height - 12.0f;
                if (!(screen.y > y))
                    y = screen.y;
            }
            label->positionY = (short)Ftol((viewer->height - y) + viewer->yMin);
            label->maybeEnabled = HUD_HIT_ON;
            if (MPSettings.maybeIsTeamGame)
                label->colourTint = MPSettings.Player[i].TeamId != 0 ? 0x14145aff : 0x5a1414ff;
            else
                label->colourTint = 0x1e504bff;
            used++;
        }

        if (MPSettings.GameMode != GM_ASSASSIN)
            continue;
        float markWidth = 80.0f, markHeight = 16.0f;
        sprite *mark;
        if (MP_IsAssasin(other)) {
            mark = assassinMark;
        } else if (MP_IsTarget(other)) {
            mark = targetMark;
            markHeight = 40.0f;
            markWidth = 40.0f;
        } else {
            continue;
        }
        if (mark == NULL)
            continue;
        mark->positionX = (short)Ftol((double)screen.x - markWidth * 0.5f + viewer->xMin);
        mark->positionY = (short)Ftol((double)viewer->height - screen.y + viewer->yMin);
        double fromTop = mark->positionY - (double)viewer->yMin;
        float markY = (float)fromTop;
        if (!(fromTop >= 0.0))
            continue;
        if (!((double)viewer->height - markHeight > markY))
            continue;
        if (!(mark->positionX - (double)viewer->xMin >= 0.0))
            continue;
        // The y again where the x was meant (as the original)
        if (!((double)viewer->width - markWidth > markY))
            continue;
        mark->maybeEnabled = HUD_HIT_ON;
    }

    for (; used < pane->base->numExtraItems; used++) {
        if (dots[used] != NULL)
            dots[used]->maybeEnabled = 0xff;
    }
}
