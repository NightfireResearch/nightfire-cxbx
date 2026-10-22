#include "Lock.h"

#include "../../game.h"
#include "../../engine/Collide.h"
#include "../../engine/Text.h"
#include "../../sound/Sound.h"
#include "../../ui/MenuManager.h"
#include "../../util/Random.h"
#include "../Spline.h"
#include "../sp/Locks.h"
#include "../sp/SwitchChannels.h"
#include "Emitter.h"
#include "Player.h"
#include "PlayerWeapon.h"
#include "build.h"
#include "bullet.h"
#include "control.h"
#include "object.h"

#include <stdio.h>
#include <string.h>

#pragma fp_contract(off)



// The weapons that open locks (weapon variants)
#define WEAPON_LASER 78
#define WEAPON_LASER_2 79
#define WEAPON_DECRYPTOR 86
#define WEAPON_DECRYPTOR_2 87

#define LOCK_SPARKS ((HASHCODE)0x0c00000a)      // the laser's emitter
#define SFX_LOCK_CUT 0x249
#define SFX_DECRYPT_DIGIT 0x5db
#define SFX_DECRYPT_DONE 0x5da
#define LABEL_KEY_CODE ((Action_TranslatedText)0x02000014) // the code's message (four %c)

// The weapon variant of the bullet a hit is from (its definition read unchecked), or -1 for a hit that is not a
// bullet's
static short HitWeaponVariant(HITDATA_tag *hit) {
    obj_tag *obj = hit->hitObj;
    if (obj == NULL || obj->objectType != OBJECTTYPE_BULLET)
        return -1;
    return ((BU_tag *)obj->extraObjectData)->wpnDef->weaponVariantNum;
}

static void PlayAt(ushort sound, obj_tag *obj) {
    Sound_Play3D((Action_SFX)sound, &obj->position, 100.0f, -1.0f, -1.0f, 0, 0, 0);
}

// A lock (OBJECTTYPE_LOCK) from its keys; one with a spline starts at its first point. One marked easy starts
// unlocked at difficulty 1.
// AUTOINJECT
obj_tag* Lock_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object_xbox(sizeof(LOCK_tag), XBOX_SIZEOF(LOCK_tag), pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    LockPlacement *keys = (LockPlacement *)lvl;
    LOCK_tag *lock = (LOCK_tag *)obj->extraObjectData;
    lock->kind = (ushort)keys->kind;
    lock->key = (ushort)keys->key;
    lock->unknownC = keys->unknown34;
    lock->openSound = keys->openSound != 0 ? (ushort)keys->openSound : 0xffff;
    lock->lockedSound = keys->lockedSound != 0 ? (ushort)keys->lockedSound : 0xffff;
    lock->channel = (ushort)keys->channel;
    ushort easy = (ushort)keys->easy;
    lock->pathPos = 0.0f;
    lock->path = keys->spline;
    lock->pathFrames = keys->splinePoints != 0 ? keys->splinePoints - 1 : 0;
    double speed = (double)(int32_t)lock->pathFrames * REC_FRAME_RATE;
    lock->pathSpeed = (float)speed;
    lock->menu = 0xffff;
    lock->pathSpeed = (float)(_FRAME_RATE / (double)keys->splineLength * speed);
    obj->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX;
    obj->objectType = OBJECTTYPE_LOCK;
    obj->objGraphics = celgl;
    obj->curState = LOCKSTATE_LOCKED;
    if (lock->pathFrames != 0) {
        Spline_Interp(lock->path, 0, (ushort)lock->pathFrames, 0.0f, &obj->transformMatrix, NULL, 2);
        Vec_Copy(Mat_Position(obj->transformMatrix), &obj->position);
    }
    build_LinkToRoom(obj, 1, glb_world);
    if (easy != 0 && GameState.difficultyModifier == 1)
        Lock_Unlock(glb_players[0], obj, 1);
    return obj;
}

// The decryptor's display while it works: the lock's code, the digits shown (stars), four to find
static void Lock_StartDecrypting(LOCK_tag *lock) {
    memcpy(GlobalVars.DecoderTarget, &KeyCodes[lock->key], sizeof(KeyCodeEntry));
    memset(GlobalVars.DecoderDisplay, '*', sizeof(GlobalVars.DecoderDisplay));
    GlobalVars.DecoderNumDigits = 4;
    GlobalVars.DecoderDigitAt = 0;
}

// A byte value kept within 0 and 255
static uchar ClampByte(short value) {
    if (value < 0)
        return 0;
    if (value > 0xff)
        return 0xff;
    return (uchar)value;
}

// A frame of a lock: a locked one opens to its key; is cut by the laser (each hit takes 3 or 10 from its tint, open
// below 4, the tint coming back while not hit); or starts decrypting when the decryptor hits it. One opening moves
// along its spline and sets its channel; one open fades back and, a keypad lock, locks again when its channel goes
// off. The keypad and the decryptor work on GlobalVars' code.
// AUTOINJECT
void Lock_Update(obj_tag *obj) {
    LOCK_tag *lock = (LOCK_tag *)obj->extraObjectData;
    switch ((short)obj->curState) {
    case LOCKSTATE_LOCKED:
        if (lock->kind == LOCK_LASER) {
            for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
                obj_tag *bulletObj = hit->hitObj;
                if (bulletObj == NULL || bulletObj->objectType != OBJECTTYPE_BULLET)
                    continue;
                BU_tag *bullet = (BU_tag *)bulletObj->extraObjectData;
                if (bullet == NULL || bullet->wpnDef == NULL)
                    continue;
                short heat;
                if (bullet->wpnDef->weaponVariantNum == WEAPON_LASER)
                    heat = 3;
                else if (bullet->wpnDef->weaponVariantNum == WEAPON_LASER_2)
                    heat = 10;
                else
                    continue;
                obj->subState = 2;
                Emitter_Create(LOCK_SPARKS, &hit->hitPosition, &hit->surfaceNormal.normal, 0.0f, 0, 0, 0, 0.0f);
                obj->tweakG = ClampByte(obj->tweakG - heat);
                obj->tweakB = ClampByte(obj->tweakB - heat);
                if (obj->tweakB <= 3) {
                    PlayAt(SFX_LOCK_CUT, obj);
                    Lock_Unlock(bullet->firedByObj, obj, 0);
                    return;
                }
            }
            if (obj->subState != 0) {
                obj->subState--;
                return;
            }
            obj->tweakB = obj->tweakB + 1 > 0xff ? 0xff : obj->tweakB + 1;
            obj->tweakG = obj->tweakG + 1 > 0xff ? 0xff : obj->tweakG + 1;
            return;
        }
        if (lock->kind != LOCK_KEYPAD && lock->kind != LOCK_DECRYPT)
            return;
        if (lock->key != 0 && glb_blokes[0] != NULL && glb_blokes[0]->keys[lock->key]) {
            Lock_Unlock(glb_players[0], obj, 0);
            obj->curState = LOCKSTATE_OPENING;
            return;
        }
        for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
            short weapon = HitWeaponVariant(hit);
            if (weapon == WEAPON_DECRYPTOR || weapon == WEAPON_DECRYPTOR_2)
                obj->subState++;
        }
        if (obj->subState != 0) {
            Lock_StartDecrypting(lock);
            obj->curState = LOCKSTATE_DECRYPTING;
            obj->subState = 0;
        }
        return;

    case LOCKSTATE_OPENING:
        if (lock->channel != 0) {
            switch_channels[lock->channel] = 1;
            switch_channels_time[lock->channel] = GameState.NumFramesUnpaused;
        }
        if (lock->pathFrames != 0) {
            Spline_Interp(lock->path, 0, (ushort)lock->pathFrames, lock->pathPos, &obj->transformMatrix, NULL, 2);
            obj->transformFlags |= TRANSFORM_MOVED;
            lock->pathPos = (float)((double)FRAME_RATE_MUL * lock->pathSpeed + lock->pathPos);
        }
        if ((double)lock->pathFrames <= lock->pathPos) {
            Control_NextLOD(obj);
            obj->curState = LOCKSTATE_OPEN;
        }
        return;

    case LOCKSTATE_OPEN:
        if (obj->tweakB + 2 <= 0xff)
            obj->tweakB += 2;
        if (obj->tweakG < 0xff)
            obj->tweakG++;
        if (lock->kind == LOCK_KEYPAD && lock->channel != 0 && !switch_channels[lock->channel])
            Lock_ReLock(glb_players[0], obj);
        return;

    case LOCKSTATE_KEYPAD: {
        memcpy(GlobalVars.DecoderTarget, &KeyCodes[lock->key], sizeof(KeyCodeEntry));
        GlobalVars.DecoderNumDigits = 4;
        GlobalVars.DecoderDigitAt = 0;
        GlobalVars.keypadBLData->active = 0;
        obj->curState = LOCKSTATE_KEYPAD_OPEN;
        lock->menu = (ushort)MenuManager_Create(0x80000003, 0x40000021, GlobalVars.keypadBLData->playerNum, 1, 0x22, 0,
                                                1);
        KeyCodeEntry *code = &KeyCodes[lock->key];
        if (code->discovered) {
            char *text = Txt_GetStringFromHeap(0);
            sprintf(text, Txt_BindLabel(LABEL_KEY_CODE, 0), (uchar)code->asciiDigit[0], (uchar)code->asciiDigit[1],
                    (uchar)code->asciiDigit[2], (uchar)code->asciiDigit[3]);
            Text_AddMsg(0, 0, 1, text, 0, 0xb4);
        }
        return;
    }

    case LOCKSTATE_KEYPAD_OPEN: {
        bool right = true;
        for (ushort i = 0; i < GlobalVars.DecoderNumDigits; i++)
            right &= GlobalVars.DecoderTarget[i] == GlobalVars.DecoderDisplay[i];
        if (!MenuManager_Exists(0) && MenuManager_Exists((uchar)lock->menu))
            MenuManager_Enable(1, (uchar)lock->menu);
        if (right && GlobalVars.DecoderDigitAt == GlobalVars.DecoderNumDigits) {
            GlobalVars.keypadBLData->active = 1;
            Lock_Unlock(GlobalVars.keypadPlayer, obj, 0);
            memset(&GlobalVars, 0, sizeof(GlobalVars));
            MenuManager_Delete((uchar)lock->menu);
            obj->curState = LOCKSTATE_OPENING;
            PlayAt(lock->openSound, obj);
        } else if (!MenuManager_Exists((uchar)lock->menu)) {
            GlobalVars.keypadBLData->active = 1;
            obj->curState = LOCKSTATE_LOCKED;
        }
        return;
    }

    case LOCKSTATE_DECRYPTING: {
        // Each frame the decryptor is on it counts 1 (3 for the second), and a count past 40 for each digit still to
        // find finds the next; while it is off the count runs down by 4, and at 0 the lock is left
        GlobalVars.DecoderDisplayShown = 1;
        ushort hits = 0;
        for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
            short weapon = HitWeaponVariant(hit);
            if (weapon == WEAPON_DECRYPTOR) {
                obj->subState++;
                hits++;
            } else if (weapon == WEAPON_DECRYPTOR_2) {
                obj->subState += 3;
                hits++;
            }
        }
        if (hits == 0) {
            int count = (short)obj->subState - 4;
            obj->subState = (ushort)(count < 0 ? 0 : (count > 3000 ? 3000 : count));
            if (obj->subState == 0) {
                memset(&GlobalVars, 0, sizeof(GlobalVars));
                GlobalVars.DecoderDisplayShown = 0;
                obj->curState = LOCKSTATE_LOCKED;
            }
            return;
        }
        ushort digit = GlobalVars.DecoderDigitAt <= 3 ? GlobalVars.DecoderDigitAt : 3;
        if (obj->subState & 1) {
            uchar shown = (uchar)Rand_Rand(10) + '0';
            GlobalVars.DecoderDisplay[digit] = shown < '0' ? '0' : (shown > '9' ? '9' : shown);
        }
        if ((short)obj->subState > (4 - digit) * 40) {
            GlobalVars.DecoderDisplay[digit] = GlobalVars.DecoderTarget[digit];
            GlobalVars.DecoderDigitAt++;
            obj->subState = 5;
            Sound_PlayExt((Action_SFX)SFX_DECRYPT_DIGIT, 100.0f, 0, 0);
        }
        if (GlobalVars.DecoderDigitAt == GlobalVars.DecoderNumDigits + 1) {
            Lock_Unlock(glb_players[0], obj, 0);
            memset(&GlobalVars, 0, sizeof(GlobalVars));
            Sound_PlayExt((Action_SFX)SFX_DECRYPT_DONE, 100.0f, 0, 0);
        }
        return;
    }

    default:
        return;
    }
}

// The player uses a lock that is locked or decrypting: with its key it opens; without, a keypad lock opens its
// keypad, any other plays its locked sound. Not while a second menu is up.
// AUTOINJECT
void Lock_Activate(obj_tag *lock, obj_tag *user) {
    if (lock == NULL || user == NULL || MenuManager_Exists(1) || user->objectType != OBJECTTYPE_PLAYER)
        return;
    short state = lock->curState;
    LOCK_tag *info = (LOCK_tag *)lock->extraObjectData;
    BLData *blData = (BLData *)user->extraObjectData;
    if (state != LOCKSTATE_LOCKED && state != LOCKSTATE_DECRYPTING)
        return;
    if (info->kind == LOCK_KEYPAD && !blData->keys[info->key]) {
        lock->curState = LOCKSTATE_KEYPAD;
        GlobalVars.keypadPlayer = user;
        GlobalVars.keypadBLData = blData;
        return;
    }
    if (blData->keys[info->key]) {
        lock->curState = LOCKSTATE_OPENING;
        PlayAt(info->openSound, lock);
    } else {
        PlayAt(info->lockedSound, lock);
    }
}

// Opens a lock: the player gets its key and the lock its next model. One locked starts opening with its sound; one
// decrypting is used as if with the key. 'silent' (the easy start) only starts it opening.
// AUTOINJECT
void Lock_Unlock(obj_tag *user, obj_tag *lock, char silent) {
    if (silent) {
        short state = lock->curState;
        if (state == LOCKSTATE_LOCKED || state == LOCKSTATE_DECRYPTING)
            lock->curState = LOCKSTATE_OPENING;
        return;
    }
    if (lock == NULL || user == NULL || user->objectType != OBJECTTYPE_PLAYER)
        return;
    LOCK_tag *info = (LOCK_tag *)lock->extraObjectData;
    Player_EquipKey((BLData *)user->extraObjectData, info->key);
    short state = lock->curState;
    if (state == LOCKSTATE_LOCKED) {
        lock->curState = LOCKSTATE_OPENING;
        PlayAt(info->openSound, lock);
    } else if (state == LOCKSTATE_DECRYPTING) {
        Player_EquipKey((BLData *)user->extraObjectData, info->key);
        Lock_Activate(lock, user);
    }
    Control_NextLOD(lock);
}

// Locks a lock again: the player loses its key and it takes its next model
// AUTOINJECT
void Lock_ReLock(obj_tag *user, obj_tag *lock) {
    if (lock == NULL || user == NULL || user->objectType != OBJECTTYPE_PLAYER)
        return;
    LOCK_tag *info = (LOCK_tag *)lock->extraObjectData;
    lock->curState = LOCKSTATE_LOCKED;
    Player_UnEquipKey((BLData *)user->extraObjectData, info->key);
    Control_NextLOD(lock);
}
