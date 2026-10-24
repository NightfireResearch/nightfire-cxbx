#include "Menu.h"
#include "../../common/xbeClass.h"
#include "MenuCommon.h"
#include "Component.h"
#include "Manager.h"
#include "TextBox.h"
#include "Button.h"
#include "Checkbox.h"
#include "Combo.h"
#include "Label.h"
#include "List.h"
#include "Memo.h"
#include "MenuManager.h"
#include "Page.h"
#include "Radio.h"
#include "Scroll.h"
#include "Spin.h"
#include "Window.h"

#include "../game/drone/BOT.h"
#include "../input.h"
#include "../memory.h"
#include "../gfx/Font.h"
#include "../gfx/Sprite.h"
#include "../math/X87Ops.h"
#include "../sound/Sound.h"
#include "../util/Sort.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../sound/SFX.h"
#include "../math/math.h"

// XBE_GLOBAL(0x0017f478, 0x300)
const LEVEL_REWARDS rewards_table[REWARDS_TABLE_SIZE] = {
    {
        .level = HT_Level_Driving_Paris,
        .rewards = {{}, {.value = 30}, {.value = 0x1002b}, {.value = 0x40018}, {.value = 0x10034}},
        .names = {
            (Action_TranslatedText)726, (Action_TranslatedText)748, (Action_TranslatedText)0x10001f9,
            (Action_TranslatedText)754,
        },
        .icons = {HT_Texture_reward_dominique, HT_Texture_bot_jaws, HT_Texture_weapon_vanquishmissiles,
            HT_Texture_bot_renard},
    },
    {
        .level = HT_Level_CastleExterior,
        .rewards = {{}, {.value = 26}, {.value = 0x10026}, {.value = 0x40000}, {.value = 0x1002d}},
        .names = {(Action_TranslatedText)722, (Action_TranslatedText)745, (Action_TranslatedText)933, (Action_TranslatedText)753},
        .icons = {HT_Texture_reward_zoe, HT_Texture_bot_oddjob, HT_Texture_weapon_pp7,
            HT_Texture_bot_baronsamedi},
    },
    {
        .level = HT_Level_Driving_SnowMobile,
        .rewards = {{}, {.value = 27}, {.value = 0x20038}, {.value = 0x40003}, {.value = 0x10035}},
        .names = {
            (Action_TranslatedText)723, (Action_TranslatedText)734, (Action_TranslatedText)735,
            (Action_TranslatedText)0x10001fc,
        },
        .icons = {HT_Texture_reward_snowmobile, HT_Texture_mp_assassination, HT_Texture_gadget_grapple,
            HT_Texture_bot_maxzorin},
    },
    {
        .level = HT_Level_Driving_Alps,
        .rewards = {{}, {.value = 28}, {.value = 0x1002a}, {.value = 0x40006}, {.value = 0x10030}},
        .names = {(Action_TranslatedText)724, (Action_TranslatedText)746, (Action_TranslatedText)934, (Action_TranslatedText)750},
        .icons = {HT_Texture_reward_vanquish, HT_Texture_bot_scaramanga, HT_Texture_gadget_microcamera,
            HT_Texture_bot_mayday},
    },
    {
        .level = HT_Level_HendersonA,
        .rewards = {{}, {.value = 29}, {.value = 0x20039}, {.value = 0x40009}, {.value = 0x1002e}},
        .names = {(Action_TranslatedText)725, (Action_TranslatedText)736, (Action_TranslatedText)935, (Action_TranslatedText)749},
        .icons = {HT_Texture_reward_mayhew, HT_Texture_mp_uplink, HT_Texture_weapon_wintertacticalsniper,
            HT_Texture_bot_xeniaonatopp},
    },
    {
        .level = HT_Level_TowerA,
        .rewards = {{}, {.value = 35}, {.value = 0x2003a}, {.value = 0x40000}, {.value = 0x10032}},
        .names = {(Action_TranslatedText)731, (Action_TranslatedText)737, (Action_TranslatedText)933, (Action_TranslatedText)752},
        .icons = {HT_Texture_reward_kiko, HT_Texture_mp_tkoth, HT_Texture_weapon_pp7,
            HT_Texture_bot_christmasjones},
    },
    {
        .level = HT_Level_PowerStationA1,
        .rewards = {{}, {.value = 31}, {.value = 0x10028}, {.value = 0x4000c}, {.value = 0x1002f}},
        .names = {(Action_TranslatedText)727, (Action_TranslatedText)931, (Action_TranslatedText)936, (Action_TranslatedText)744},
        .icons = {HT_Texture_reward_rook, HT_Texture_bot_wailin, HT_Texture_gadget_dart,
            HT_Texture_bot_goldfinger},
    },
    {
        .level = HT_Level_Tower2A,
        .rewards = {{}, {.value = 32}, {.value = 0x2003b}, {.value = 0x40000}, {.value = 0x10036}},
        .names = {(Action_TranslatedText)728, (Action_TranslatedText)739, (Action_TranslatedText)933, (Action_TranslatedText)930},
        .icons = {HT_Texture_reward_allura, HT_Texture_mp_demolition, HT_Texture_weapon_pp7,
            HT_Texture_bot_drakesuit},
    },
    {
        .level = HT_Level_Driving_Underwater,
        .rewards = {{}, {.value = 33}, {.value = 0x10029}, {.value = 0x4000f}, {.value = 0x10031}},
        .names = {(Action_TranslatedText)729, (Action_TranslatedText)932, (Action_TranslatedText)937, (Action_TranslatedText)751},
        .icons = {HT_Texture_reward_vanquish_sub, HT_Texture_bot_niknak, HT_Texture_gadget_decryptor,
            HT_Texture_bot_elektra},
    },
    {
        .level = HT_Level_Driving_JungleA,
        .rewards = {{}, {.value = 34}, {.value = 0x2003c}, {.value = 0x40012}, {.value = 0x10027}},
        .names = {(Action_TranslatedText)730, (Action_TranslatedText)741, (Action_TranslatedText)938, (Action_TranslatedText)929},
        .icons = {HT_Texture_reward_ultralight, HT_Texture_mp_protection, HT_Texture_gadget_stunner,
            HT_Texture_bot_bond},
    },
    {
        .level = HT_Level_EvilBase,
        .rewards = {{}, {.value = 36}, {.value = 0x3003e}, {.value = 0x40015}, {.value = 0x10033}},
        .names = {(Action_TranslatedText)732, (Action_TranslatedText)742, (Action_TranslatedText)939, (Action_TranslatedText)747},
        .icons = {HT_Texture_reward_drake, HT_Texture_gadget_watch, HT_Texture_gadget_watch,
            HT_Texture_bot_pussygalore},
    },
    {
        .level = HT_Level_SpaceStationD,
        .rewards = {{}, {.value = 37}, {.value = 0x2003d}, {.value = 0x40009}, {.value = 0x10037}},
        .names = {
            (Action_TranslatedText)0x10001ff, (Action_TranslatedText)743, (Action_TranslatedText)935,
            (Action_TranslatedText)0x10001fb,
        },
        .icons = {HT_Texture_reward_bond, HT_Texture_mp_goldeneye, HT_Texture_weapon_wintertacticalsniper,
            HT_Texture_bot_bondspacesuit},
    },
};
// XBE_GLOBAL(0x002251b8, 0x20040)
char codename_buf[CODENAME_SLOTS_DEFAULT + 4096][0x20];
// XBE_GLOBAL(0x0017d588, 0x70)
DEFAULT_CODENAME def_codename[2] = {
    {
        .name = (Action_TranslatedText)667,
        .musicVolume = 75,
        .sfxVolume = 100,
        .mpSomeField2 = 1,
        .controlStyle = 7,
        .autoAim = 1,
        .crouchToggle = 1,
        .vibrationEnabled = 1,
        .crosshairsEnabled = 1,
        .autoSwitchBetterWeapon = 1,
        .input_0c = 1,
        .respawnMode = 2,
        .showNames = 1,
    },
    {
        .name = (Action_TranslatedText)529,
        .musicVolume = 75,
        .sfxVolume = 100,
        .mpSomeField2 = 1,
        .controlStyle = 7,
        .autoAim = 1,
        .crouchToggle = 1,
        .vibrationEnabled = 1,
        .crosshairsEnabled = 1,
        .autoSwitchBetterWeapon = 1,
        .input_0c = 1,
        .respawnMode = 2,
        .showNames = 1,
    },
};

// XBE_GLOBAL(0x0017d540, 0x48)
MENU_LS ls = {.codename = {0, 0, 0, 1, 1, 1}};

// XBE_GLOBAL(0x0025d7bc, 0x1)
uint8_t menu_music_playing;
// XBE_GLOBAL(0x0025d7bd, 0x1)
uint8_t menu_movie_skippable;
// XBE_GLOBAL(0x0025d7be, 0x1)
uint8_t menu_movie_stopped_music;
// XBE_GLOBAL(0x0025d7c0, 0x4)
uint32_t menu_movie_skip_delay;
// XBE_GLOBAL(0x0025d7dd, 0x1)
uint8_t cn_modified;

// XBE_GLOBAL(0x0025d7de, 0x1)
uint8_t cn_secret_mode;
// Menu (Send, SendEx, SendMessage), Iris (Start, Play), Wheel etc

// AUTOINJECT
int Menu_GetLevelIndex(HASHCODE level) {

    for(int i = 0; i < ARRAY_SIZE(sp_level); i++) {
        if(sp_level[i].identifier == level) {
            return i;
        }
    }
    return -1;
}

// AUTOINJECT
bool Menu_IsDrivingLevel(HASHCODE level) {
  switch(level) {
      case HT_Level_Driving_Paris:
      case HT_Level_Driving_Underwater:
      case HT_Level_Driving_JungleA:
      case HT_Level_Driving_SnowMobile:
      case HT_Level_Driving_Alps:
          return true;
  }
  return false;
}

// ---- the iris: a picture of a camera's iris that closes and opens over a wheel (PS2: menu_frontend.cpp)

// Its state: the frame of the ten (above 10 it is over), the ticks (the picture changes on even ones), a frame held,
// and whether it is running
// XBE_GLOBAL(0x0025d7ac, 0x4)
int iris_tick;
// XBE_GLOBAL(0x0025d7b0, 0x4)
int iris_frame;
// XBE_GLOBAL(0x0025d7b8, 0x1)
uint8_t iris_hold;
// XBE_GLOBAL(0x0025d7b9, 0x1)
uint8_t iris_active;

#define IRIS_FRAMES 10

// Starts the iris: 0 from the middle; 1 the whole of it; 2 the whole of it unless one is running, without marking it
// running (a wheel's turn); 3 held at the middle; 4 its last frame. Its first frame is drawn.
// AUTOINJECT
void Menu_StartIris(MENU_IRISOPS op, uchar managerNum, uint label) {
    if (op == 2 && iris_active)
        return;
    switch (op) {
        case 0:
            iris_frame = 5;
            iris_active = 1;
            iris_hold = 0;
            break;
        case 1:
            iris_active = 1;
            iris_frame = 1;
            iris_hold = 0;
            break;
        case 2:
            iris_active = 0;
            iris_frame = 1;
            iris_hold = 0;
            break;
        case 3:
            iris_active = 1;
            iris_hold = 1;
            iris_frame = 5;
            break;
        case 4:
            iris_frame = IRIS_FRAMES;
            iris_active = 1;
            iris_hold = 0;
            break;
    }
    iris_tick = 0;
    Menu_PlayIris(0, managerNum, label);
    iris_tick = -1;
}

// A frame of the iris: on even ticks the label shows the frame's picture and, when ticking and not held, the next
// frame follows
// AUTOINJECT
void Menu_PlayIris(char tick, uchar managerNum, uint label) {
    if (iris_frame > IRIS_FRAMES) {
        iris_active = 0;
        return;
    }
    if (tick)
        iris_tick++;
    if (iris_tick % 2 != 0)
        return;
    M_CONTROL *iris = CONTROL_GET(managerNum, (HASHCODE)label);
    if (iris == NULL)
        return;
    switch (iris_frame) {
        case 1:
        case 9:
            // Its pictures (MessageType_SetIcon): frames 1 to 5 close it to the middle, 6 to 9 open it again
            __Menu_SendMessage(iris, MessageType_SetIcon, HT_Texture_iris_1, 0);
            __Menu_SendMessage(iris, MessageType_SetIconRect, 0, 0x800080);
            break;
        case 2:
        case 8:
            __Menu_SendMessage(iris, MessageType_SetIcon, HT_Texture_iris_2, 0);
            __Menu_SendMessage(iris, MessageType_SetIconRect, 0, 0x800080);
            break;
        case 3:
        case 7:
            __Menu_SendMessage(iris, MessageType_SetIcon, HT_Texture_iris_3, 0);
            __Menu_SendMessage(iris, MessageType_SetIconRect, 0, 0x800080);
            break;
        case 4:
        case 6:
            __Menu_SendMessage(iris, MessageType_SetIcon, HT_Texture_iris_4, 0);
            __Menu_SendMessage(iris, MessageType_SetIconRect, 0, 0x800080);
            break;
        case 5:
            __Menu_SendMessage(iris, MessageType_SetIcon, HT_Texture_iris_5, 0);
            __Menu_SendMessage(iris, MessageType_SetIconRect, 0, 0x800080);
            break;
        case 10:
            __Menu_SendMessage(iris, MessageType_SetIcon, HT_Texture_iris_1, 0);
            __Menu_SendMessage(iris, MessageType_SetIconRect, 0x400040, 0x10001);
            break;
    }
    if (tick && !iris_hold)
        iris_frame++;
}

// The whole iris, input locked, and the page (with the GoPage flags of the load or save in hand) as it closes
// AUTOINJECT
void Menu_ChangePageCloseIris(HASHCODE page, uchar managerNum, uint label) {
    Menu_StartIris(1, managerNum, label);
    M_CONTROL *mgr = &manager[managerNum].control;
    __Menu_SendDelayedMessage(1, mgr, MessageType_LockInput, 1, 0);
    __Menu_SendDelayedMessage(10, mgr, MessageType_GoPage, page, ls.returnPageArg);
}

// ---- the front end's music: two loops, played together

// Starts it again, unless it is playing
// AUTOINJECT
void __stdcall Menu_RestartFrontEndLoop(void) {
    if (menu_music_playing)
        return;
    SFXRemove(NULL, SFX_MUSIC_FRONT_END_LOOP_01);
    SFXRemove(NULL, SFX_MUSIC_FRONT_END_LOOP_02);
    SFXStart(SFX_MUSIC_FRONT_END_LOOP_01, NULL);
    SFXStart(SFX_MUSIC_FRONT_END_LOOP_02, NULL);
    menu_music_playing = 1;
}

// AUTOINJECT
void __stdcall Menu_StopFrontEndMusic(void) {
    SFXRemove(NULL, SFX_MUSIC_FRONT_END_LOOP_01);
    SFXRemove(NULL, SFX_MUSIC_FRONT_END_LOOP_02);
    menu_music_playing = 0;
}

// ---- option boxes: a message in a box over the page, with an answer to pick or none (PS2: menu_handlers.cpp)

// The answer, which Menu_UpdateOptionBox takes: 0 none yet; without an answer to pick 1 (A) or 2 (B), with one 3 or 4
// XBE_GLOBAL(0x00224f5c, 0x4)
uint optionbox_answer;
// XBE_GLOBAL(0x00224f60, 0x4)
uint optionbox_type;     // the caller's, to tell its boxes apart
// XBE_GLOBAL(0x00224f64, 0x1)
uint8_t optionbox_has_answer;

#define TXT_PRESS_A_TO_CONTINUE ((Action_TranslatedText)0x266)
#define TXT_PRESS_A_OR_B_TO_CANCEL ((Action_TranslatedText)0x10001d6)

// The box opens as an overlay, its height fitted to the text's lines and the answer, centred down the screen, and is
// drawn at once with the cursor on its answers
// AUTOINJECT
void Menu_CreateOptionBox(uchar managerNum, const char *text, uint type, char hasAnswer, char canCancel) {
    optionbox_has_answer = hasAnswer;
    optionbox_type = type;
    LABEL_SET_TEXT(managerNum, SUB_P_MESSAGEBOX_TEXT, text);
    __Menu_Send(managerNum, C_LBMSGOPTIONS, MessageType_ClearItems, 0, 0);
    __Menu_Send(managerNum, C_LBMSGOPTIONS, MessageType_ListSetColumnAlign, 0, ALIGN_CENTRE);
    if (hasAnswer)
        __Menu_Send(managerNum, C_LBMSGOPTIONS, MessageType_AddItem,
                    (intptr_t)Txt_BindLabel(canCancel ? TXT_PRESS_A_OR_B_TO_CANCEL : TXT_PRESS_A_TO_CONTINUE, 0), 3);
    M_MANAGER *mgr = &manager[managerNum];
    Manager_SendMessage(mgr, MessageType_GoPage, P_MESSAGEBOX, 2);
    M_BUTTON *box = (M_BUTTON *)CONTROL_GET(managerNum, SUB_P_MESSAGEBOX_BOX);
    M_MEMO *memo = (M_MEMO *)CONTROL_GET(managerNum, SUB_P_MESSAGEBOX_TEXT);
    M_LIST *answers = (M_LIST *)CONTROL_GET(managerNum, C_LBMSGOPTIONS);
    __Menu_SendMessage(&memo->control, MessageType_MemoSetOption282, 0, 0);
    __Menu_SendMessage(&memo->control, MessageType_SetLineSpacing, 100, 0);
    uchar numLines = (uchar)__Menu_SendMessage(&memo->control, MessageType_MemoGetNumLines, 0, 0);
    uchar numAnswers = (uchar)__Menu_Send(managerNum, C_LBMSGOPTIONS, MessageType_GetNumItems, 0, 0);
    ushort textHeight = (ushort)((uchar)Ftol(memo->lineHeight) * numLines);
    box->control.height = (short)(textHeight + numAnswers * 21 + (numAnswers != 0 ? 26 : 0) + 28);
    short top = (short)Ftol((480.0 - box->control.height) * 0.5);
    box->control.y = top;
    memo->control.y = top + 10;
    answers->control.y = textHeight + top + 30;
    mgr->takingInput = 0;
    Memo_Update(mgr, (M_PAGE *)memo->control.parent, memo);
    List_Update(mgr, (M_PAGE *)answers->control.parent, answers);
    Button_Update(mgr, (M_PAGE *)box->control.parent, box);
    mgr->takingInput = 1;
    Manager_SendMessage(mgr, MessageType_SetCursor, C_LBMSGOPTIONS, 0);
}

// AUTOINJECT
void Menu_CreateOptionBoxLabel(uchar managerNum, Action_TranslatedText text, uint type, char hasAnswer, char canCancel) {
    Menu_CreateOptionBox(managerNum, Txt_BindLabel(text, 0), type, hasAnswer, canCancel);
}

// The answer since the last call, and the box's type
// AUTOINJECT
uint Menu_UpdateOptionBox(uint *type) {
    uint answer = optionbox_answer;
    optionbox_answer = 0;
    if (type != NULL)
        *type = optionbox_type;
    return answer;
}

// The box's answers: A or B closes it
// AUTOINJECT
bool C_LBMSGOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select) {
        optionbox_answer = optionbox_has_answer ? 3 : 1;
        Manager_SendMessage(&manager[managerNum], MessageType_Back, 1, 0);
    }
    else if (message == MessageType_BackOnControl) {
        Manager_SendMessage(&manager[managerNum], MessageType_Back, 1, 0);
        optionbox_answer = optionbox_has_answer ? 4 : 2;
    }
    return true;
}

typedef struct {
  M_CONTROL* control;
  uint dispatchOnFrameNum;
  uint param1;
  intptr_t param2;   // the message's arguments, which may be addresses (a text)
  intptr_t param3;
} DelayedMessage;
LAYOUT_32(sizeof(DelayedMessage) == 0x14, "DelayedMessage is 0x14 bytes");

// XBE_GLOBAL(0x00224540, 0x4)
static uint32_t menu_delay_frame;
// XBE_GLOBAL(0x00223b40, 0xa00)
static DelayedMessage menu_delay_msg[128];

// AUTOINJECT
undefined4 __Menu_SendDelayedMessage(uint duration,M_CONTROL *control,uint arg1,intptr_t arg2,intptr_t arg3) {

  for(int i = 0; i < ARRAY_SIZE(menu_delay_msg); i++) {

    // Is the item at this index expired?
    if(menu_delay_msg[i].dispatchOnFrameNum < menu_delay_frame) {

      // Insert this item and return
      menu_delay_msg[i].control = control;
      menu_delay_msg[i].dispatchOnFrameNum = menu_delay_frame + duration;
      menu_delay_msg[i].param1 = arg1;
      menu_delay_msg[i].param2 = arg2;
      menu_delay_msg[i].param3 = arg3;

      return true;

    }

  }

  return false;
}

// AUTOINJECT
void __Menu_SendDelayed(int delayDuration, byte managerNum, HASHCODE controlHashcode, undefined4 arg1, intptr_t arg2, intptr_t arg3) {

  M_CONTROL* control = CONTROL_GET(managerNum, controlHashcode);
  
  if(control == NULL)
    return;

  __Menu_SendDelayedMessage(delayDuration, control, arg1, arg2, arg3);

}

// AUTOINJECT
void Menu_ProcessDelayedMessages(void) {

  menu_delay_frame++;

  for(int i = 0; i < ARRAY_SIZE(menu_delay_msg); i++) {
    DelayedMessage msg = menu_delay_msg[i];
    if(msg.dispatchOnFrameNum == menu_delay_frame) {
      __Menu_SendMessage(msg.control, msg.param1, msg.param2, msg.param3);
    }
  }

}

// Forgets every pending delayed message. Called by Manager_SendMessage when a page is opened as an overlay
// (GoPage with flag 2), and by C_LBERROPTIONS_Handler. Only each slot's frame number is cleared, not the rest:
// that is enough, because __Menu_SendDelayedMessage takes any slot whose frame is below menu_delay_frame and
// Menu_ProcessDelayedMessages dispatches only on an exact match with the (already incremented) frame counter.
// menu_delay_frame itself is left alone.
// AUTOINJECT
void __stdcall Menu_ClearDelayedMessages(void) {
  for(int i = 0; i < ARRAY_SIZE(menu_delay_msg); i++)
    menu_delay_msg[i].dispatchOnFrameNum = 0;
}

// The bot's character is on MI6's side
// AUTOINJECT
bool Menu_IsBotGood(uint idx) {
    BOT_stats_t *stats = BOT_getDefaultStats(idx);
    return stats->isBad == 0;
}

// Size unclear
// XBE_GLOBAL(0x002250b8, 0x100)
static char buf_171[0x100];

// AUTOINJECT
void Menu_UpdateWheel(uchar managerNum, M_CONTROL *ctrl, M_ITEM *itemList, HASHCODE wheel, HASHCODE icon, HASHCODE descriptionLabel, HASHCODE iris, bool doAnimation) {

  int iVar2;
  const char *text;
  const char *teamLabel;
  const char *descText;
  Action_TranslatedText AVar7;

  // The index of the item in the list that is currently selected, and its 2 surrounding items
  int idxUp2;
  int idxUp1;
  const int idxMid = __Menu_SendMessage(ctrl,0x40,0,0);
  int idxDown1;
  int idxDown2;

  // 5 strings, representing the middle item, and its 2 surrounding items
  const char* strUp2;
  const char* strUp1;
  const char* strMid;
  const char* strDown1;
  const char* strDown2;
  
  if (ctrl == NULL) 
    return;

  if (ctrl->type == ControlType_Scroll) {
    ((M_SCROLL *)ctrl)->wrap = 0;
  }

  const int last_item_idx = __Menu_SendMessage(ctrl,MessageType_GetMax,0,0);

  if (icon != 0) {
    if (doAnimation) {
      __Menu_SendDelayed(10,managerNum, icon, MessageType_SetIcon, itemList[idxMid].iconHashcode,0);
      __Menu_SendDelayed(10,managerNum, icon, MessageType_SetColour, itemList[idxMid].enabled ? 0x808080ff : 0x60606060,0);
    }
    else {
      __Menu_Send(managerNum, icon, MessageType_SetIcon, itemList[idxMid].iconHashcode, 0);
      LABEL_SET_COLOUR(managerNum, icon, itemList[idxMid].enabled ? 0x808080ff : 0x60606060);
    }
  }

  if (descriptionLabel != 0) { 

    if (manager[managerNum].currentPage->control.hashcode == P_MPBOTCHOOSE) {

      // Special case when on the MP bot selection menu

      if (!itemList[idxMid].enabled) {
        // Use the disabled description
        text = Txt_BindLabel(itemList[idxMid].descriptionWhenDisabled,0);
      }
      else {
        // Merge their team name into the description
        text = Txt_BindLabel(Menu_IsBotGood(itemList[idxMid].identifier) ? MP_TEAM_MI6 : MP_TEAM_PHOENIX, 0);
        teamLabel = Txt_BindLabel(MP_TEAM, 0);
        descText = Txt_BindLabel(itemList[idxMid].description, 0);
        sprintf(buf_171, "%s\n%s : %s", descText, teamLabel, text);
        text = buf_171;
      }
    }

    // Every other menu uses the item list directly - either the normal or disabled description
    else {
      text = Txt_BindLabel(itemList[idxMid].enabled ? itemList[idxMid].description : itemList[idxMid].descriptionWhenDisabled, 0);
    }

    LABEL_SET_TEXT(managerNum, descriptionLabel, text);

  }

  // Work out the indices of the items to display in the wheel, managing the ends of the array appropriately
  idxUp1 = idxMid - 1;
  idxUp2 = idxMid - 2;
  idxDown1 = idxMid + 1;
  idxDown2 = idxMid + 2;
  if(idxUp1 < 0) {
    idxUp1 = 999;
  }
  if(idxUp2 < 0) {
    idxUp2 = 999;
  }
  if(idxDown1 > last_item_idx) {
    idxDown1 = 999;
  }
  if(idxDown2 > last_item_idx) {
    idxDown2 = 999;
  }
  
  // Obtain and set the strings up
  strUp2 = (idxUp2 == 999) ? " " : Txt_BindLabel(itemList[idxUp2].title, 0);
  strUp1 = (idxUp1 == 999) ? " " : Txt_BindLabel(itemList[idxUp1].title, 0);
  strMid = (idxMid == 999) ? " " : Txt_BindLabel(itemList[idxMid].title, 0);
  strDown1 = (idxDown1 == 999) ? " " : Txt_BindLabel(itemList[idxDown1].title, 0);
  strDown2 = (idxDown2 == 999) ? " " : Txt_BindLabel(itemList[idxDown2].title, 0);

  __Menu_SendEx(managerNum, wheel, 0, MessageType_SetText, (intptr_t)strUp2, NULL);
  __Menu_SendEx(managerNum, wheel, 1, MessageType_SetText, (intptr_t)strUp1, NULL);
  __Menu_SendEx(managerNum, wheel, 2, MessageType_SetText, (intptr_t)strMid, NULL);
  __Menu_SendEx(managerNum, wheel, 3, MessageType_SetText, (intptr_t)strDown1, NULL);
  __Menu_SendEx(managerNum, wheel, 4, MessageType_SetText, (intptr_t)strDown2, NULL);
  
  // Handle greyed-out disabled items. 3 entries representing the distance from the middle (greys out gradually to the edge)
  const int colourDisabled[3] = {0x48484880, 0x48484840, 0x48484820};
  const int colourEnabled[3] = {0x7d6d5aff, 0x7d6d5a80, 0x7d6d5a40};

  // The original colours all four outer slots, an empty one (999) from item 999 past the list's end
  // (docs/original-bug-fixes.md); the fix leaves an empty slot's colour as it was.
#if FIX_ORIGINAL_GAME_BUGS
  if(idxUp2 != 999)
    __Menu_SendEx(managerNum, wheel, 0, MessageType_SetColour, itemList[idxUp2].enabled ? colourEnabled[2] : colourDisabled[2], NULL);
  if(idxUp1 != 999)
    __Menu_SendEx(managerNum, wheel, 1, MessageType_SetColour, itemList[idxUp1].enabled ? colourEnabled[1] : colourDisabled[1], NULL);
  if(idxDown1 != 999)
    __Menu_SendEx(managerNum, wheel, 3, MessageType_SetColour, itemList[idxDown1].enabled ? colourEnabled[1] : colourDisabled[1], NULL);
  if(idxDown2 != 999)
    __Menu_SendEx(managerNum, wheel, 4, MessageType_SetColour, itemList[idxDown2].enabled ? colourEnabled[2] : colourDisabled[2], NULL);
#else
  __Menu_SendEx(managerNum, wheel, 0, MessageType_SetColour, itemList[idxUp2].enabled ? colourEnabled[2] : colourDisabled[2], NULL);
  __Menu_SendEx(managerNum, wheel, 1, MessageType_SetColour, itemList[idxUp1].enabled ? colourEnabled[1] : colourDisabled[1], NULL);
  __Menu_SendEx(managerNum, wheel, 3, MessageType_SetColour, itemList[idxDown1].enabled ? colourEnabled[1] : colourDisabled[1], NULL);
  __Menu_SendEx(managerNum, wheel, 4, MessageType_SetColour, itemList[idxDown2].enabled ? colourEnabled[2] : colourDisabled[2], NULL);
#endif

  // The middle item can't be offscreen, no need to check
  __Menu_SendEx(managerNum, wheel, 2, MessageType_SetColour, itemList[idxMid].enabled ? colourEnabled[0] : colourDisabled[0], NULL);

  if (doAnimation) {
    // Inlined Menu_StartIris(2, managerNum, iris)
    // DAT_0025d7b9 = '\0';
    // DAT_0025d7b8 = 0;
    // DAT_0025d7b0 = 1;
    // DAT_0025d7ac = 0;
    // Menu_PlayIris('\0',managerNum,iris);
    // DAT_0025d7ac = 0xffffffff;
    Menu_StartIris(2, managerNum, iris);
  }

}

// AUTOINJECT
void Menu_DeleteSprite(sprite* spr) {
    if(spr == NULL)
        return;

    Sprite_Delete(spr);
    
}

// Moves a list or a wheel to the item with the value; with none, the control to its first (no list, nothing)
// AUTOINJECT
bool Menu_SelectItemInControl(M_CONTROL *control, M_ITEM *list, ushort size, int value) {
    if (list == NULL)
        return false;
    for (ushort i = 0; i < size; i++) {
        if (list[i].identifier != (uint)value)
            continue;
        if (control->type == ControlType_List) {
            __Menu_SendMessage(control, MessageType_SelectIndex, i, 0);
            return true;
        }
        if (control->type == ControlType_Scroll) {
            __Menu_SendMessage(control, MessageType_SetValue, i, 0);
            return true;
        }
        return false;
    }
    __Menu_SendMessage(control, MessageType_SetValue, 0, 0);
    return false;
}

// AUTOINJECT
M_ITEM* Menu_GetItemFromHash(M_ITEM *list, int value, uint numItems) {
    for (uint i = 0; i < numItems; i++)
        if (list[i].identifier == (uint)value)
            return &list[i];
    return NULL;
}

extern char menu_unlock_everything;

// AUTOINJECT
void Menu_AddItemsToControl(M_CONTROL *control, M_ITEM *itemList, ushort numItems, ushort firstItemIdx,
                            uchar unlockEverything) {

  if(itemList == NULL)
    return;

  __Menu_SendMessage(control, MessageType_ClearItems, 0, 0);

  for(int i = firstItemIdx; i < numItems; i++) {
    M_ITEM* item = &itemList[i];
    if(unlockEverything || menu_unlock_everything || item->enabled) {
      __Menu_SendMessage(control, MessageType_AddItem, (intptr_t)Txt_BindLabel(item->title, 0), (int)item->identifier);
    }
  }

}

// AUTOINJECT
void Menu_ClearStack(M_MANAGER *mgr) {

    // Each entry is a page and its focused control (Manager_SendMessage, MessageType_GoPage)
    while (!Stack_IsEmpty(&mgr->stack)) {
        void *entry = Stack_Top(&mgr->stack);
        Stack_Pop(&mgr->stack);
        Menu_Free(&entry, 8);
    }

}

// AUTOINJECT
void Menu_ChangeControllerStyle(ushort playerNum, int controllerStyle) {
  if (playerNum == 0xffff) {
    Input_ChangeControllerStyle(0, controllerStyle);
    Input_ChangeControllerStyle(1, controllerStyle);
    Input_ChangeControllerStyle(2, controllerStyle);
    Input_ChangeControllerStyle(3, controllerStyle);
  } else {
    Input_ChangeControllerStyle(playerNum, controllerStyle);
  }
}

// ---- the framework: skins, finding controls, dispatch, input, sprites, sounds (PS2: menu.cpp)

#pragma fp_contract(off)

// Empty on the Xbox, where the linker shares one empty function among several
static void Window_Update(M_MANAGER *mgr, M_PAGE *page, M_CONTROL *window) {}

// Every manager's skin sets
// XBE_GLOBAL(0x00223a90, 0x10)
LINKEDLIST component_list;
// Frames counted by Menu_AlphaUpdate, and the frame and controller of the last action Menu_GetAction reported
// XBE_GLOBAL(0x00223aa0, 0x4)
uint32_t menu_frame;
// XBE_GLOBAL(0x00223aa4, 0x4)
uint32_t menu_last_action;
// XBE_GLOBAL(0x0017c540, 0x4)
uint32_t menu_last_controller = 4294967295;  // -1 at boot
// The highlight's pulse (Menu_AlphaUpdate)
// XBE_GLOBAL(0x00223b2c, 0x2)
int16_t menu_alpha;
// XBE_GLOBAL(0x00223b30, 0x4)
float menu_alpha_phase;
// The sounds the page allows (Menu_EnableSounds): MENU_SOUND_*
// XBE_GLOBAL(0x00223b34, 0x4)
uint32_t menu_sounds;
// The text's throb (Menu_UpdateTextFmt). Nothing reads the scale.
// XBE_GLOBAL(0x00223b38, 0x4)
float menu_text_scale;
// XBE_GLOBAL(0x00223b3c, 0x4)
float menu_text_phase;
// The text format of a sprite Menu_InitializeSprite gives a text (also List_Init's)
#define MenuTextFormat FontFormat2Scaled

// What Menu_GetFirstControl hands back when a page has nothing to focus on
// XBE_GLOBAL(0x00224558, 0x94)
M_BASECONTROL menu_no_control;
#define NO_CONTROL_HASHCODE ((HASHCODE)0x1fffffff)

#define COMPONENTSET_NEXT_ID 999    // Component_AddSet: the next id free

#define MENU_SEND_ALL 0xffffffffu   // the hashcode that matches every control
#define MENU_SEND_STEPS 1000        // pages and controls a send visits before it gives up


#define MENU_SOUND_SELECT    1      // sounds 0-2 and 8 play only with their bit
#define MENU_SOUND_ALT1      2
#define MENU_SOUND_ALT2      4
#define MENU_SOUND_BACK      8

// ---- skins (components)

// AUTOINJECT
void Component_InitComponents(void) {
    LinkedList_Init(&component_list, 0);
}

// A new set of skins for a manager
// AUTOINJECT
MENU_COMPONENTSET* Component_AddSet(uchar managerNum, ushort id) {
    MENU_COMPONENTSET *set = (MENU_COMPONENTSET *)Menu_Malloc(sizeof(MENU_COMPONENTSET));
    if (id == COMPONENTSET_NEXT_ID)
        set->id = component_list.count + 1;
    else
        set->id = id;
    set->managerNum = managerNum;
    LinkedList_Add(&component_list, &set->prev, set);
    return set;
}

// AUTOINJECT
void Component_DestroyAll(uchar managerNum) {
    MENU_COMPONENTSET *set = (MENU_COMPONENTSET *)LinkedList_First(&component_list);
    while (set != NULL) {
        if (set->managerNum == managerNum) {
            for (int i = 0; i < COMPONENTS_PER_SET; i++) {
                if (set->components[i].pieces != NULL)
                    Menu_Free((void **)&set->components[i].pieces, 1);
            }
            LinkedList_Remove(&component_list, &set->prev, set);
            MENU_COMPONENTSET *freed = set;
            Menu_Free((void **)&freed, 1);
        }
        // Read from the block just freed
        set = set->next;
    }
}

// AUTOINJECT
MENU_COMPONENTPIECE* Component_AddComponent(MENU_COMPONENT *component, short left, short top, short right, short bottom,
                                            HASHCODE texture, short count) {
    component->left = left;
    component->top = top;
    component->right = right;
    component->bottom = bottom;
    component->texture = texture;
    component->count = count;
    if (count != 0)
        component->pieces = (MENU_COMPONENTPIECE *)Menu_Malloc(count * sizeof(MENU_COMPONENTPIECE));
    else
        component->pieces = NULL;
    return component->pieces;
}

// AUTOINJECT
void Component_InitInstance(MENU_COMPONENTPIECE *piece, short x, short y, short width, short height, short u, short v,
                            short uw, short vh, uint flags, uint colour, float stretchWidth, float stretchHeight) {
    piece->info.posX = x;
    piece->info.posY = y;
    piece->info.onscreenWidth = width;
    piece->info.onscreenHeight = height;
    piece->info.spritesheetX = u;
    piece->info.spritesheetY = v;
    piece->info.spritesheetWidth = uw;
    piece->info.spritesheetHeight = vh;
    piece->info.flags = 0;
    piece->info.textRef = (Action_TranslatedText)-1;
    piece->info.depth = 0xffff;
    piece->info.format = NULL;
    piece->info.textureHashcode = (HASHCODE)0;
    piece->info.linkedViewer = 0;
    piece->info.colour = colour;
    piece->stretchWidth = stretchWidth;
    piece->stretchHeight = stretchHeight;
    piece->flags = flags;
}

// A control's skin: M_CONTROL::skin's set and index
// AUTOINJECT
MENU_COMPONENT* Component_GetHandle(M_CONTROL *control) {
    for (MENU_COMPONENTSET *set = (MENU_COMPONENTSET *)LinkedList_First(&component_list); set != NULL; set = set->next) {
        if (set->id == control->skin >> 16)
            return &set->components[control->skin & 0xffff];
    }
    return NULL;
}

// What a stretched piece takes of the room: its share, between none and all of it. The share is tested below 0 as
// computed, and against the room as stored.
static int Component_Stretch(int room, float share) {
    double stretched = room * (double)share;
    float stored = (float)stretched;
    if (stretched < 0.0)
        return 0;
    if (stored > (double)room)
        return Ftol(room);
    return Ftol(stored);
}

// Lays the control's skin out in a rectangle (-1: the control's own) for a draw state: the fixed pieces along the
// edges their flags give, the stretched pieces sharing what room is left; pieces not shown in that state hidden. The
// sprites are made the first time.
// AUTOINJECT
void Component_SetupInstance(M_CONTROL *control, uint state, int x, int y, int width, int height) {
    M_PAGE *page = Menu_GetPage(control);
    if (page == NULL)
        return;
    if (x == -1)
        x = control->x;
    if (y == -1)
        y = control->y;
    if (width == -1)
        width = control->width;
    if (height == -1)
        height = control->height;
    MENU_COMPONENT *component = Component_GetHandle(control);
    if (component == NULL)
        return;

    // The room the fixed pieces leave
    int right = width;
    int left = 0;
    int top = 0;
    int bottom = height;

    if (control->sprites == NULL) {
        if (component->count == 0)
            return;
        control->sprites = (sprite **)Menu_Malloc(component->count * sizeof(sprite *));
        control->numSprites = component->count;
        for (int i = 0; i < control->numSprites; i++) {
            component->pieces[i].info.textureHashcode = component->texture;
            control->sprites[i] = Menu_CreateSprite(&component->pieces[i].info);
        }
    }
    if (component->pieces == NULL)
        return;

    for (int i = 0; i < control->numSprites; i++) {
        MENU_COMPONENTPIECE *piece = &component->pieces[i];
        if ((piece->flags & state) == 0) {
            if (control->sprites[i] != NULL)
                control->sprites[i]->depth = SPRITE_HIDDEN;
            continue;
        }

        if (piece->flags & PIECE_WIDTH_FROM_HEIGHT) {
            if (piece->stretchHeight == 0.0f)
                piece->info.onscreenWidth = control->sprites[i]->onscreenHeight;
            else
                piece->info.onscreenWidth = Ftol((bottom - top) * (double)piece->stretchHeight);
        }

        short pieceWidth;
        if (piece->stretchWidth == 0.0f) {
            if (!(piece->flags & PIECE_KEEP_ROOM_X)) {
                if (piece->flags & PIECE_RIGHT) {
                    int edge = width - piece->info.onscreenWidth - piece->info.posX;
                    if (edge < right)
                        right = edge;
                } else {
                    int edge = piece->info.posX + piece->info.onscreenWidth;
                    if (edge > left)
                        left = edge;
                }
            }
            pieceWidth = piece->info.onscreenWidth;
        } else {
            pieceWidth = Component_Stretch(right - left, piece->stretchWidth);
        }

        if (piece->flags & PIECE_HEIGHT_FROM_WIDTH) {
            if (piece->stretchWidth == 0.0f)
                piece->info.onscreenHeight = piece->info.onscreenWidth;
            else
                piece->info.onscreenHeight = Ftol((right - left) * (double)piece->stretchWidth);
        }

        short pieceHeight;
        if (piece->stretchHeight == 0.0f) {
            if (!(piece->flags & PIECE_KEEP_ROOM_Y)) {
                if (piece->flags & PIECE_BOTTOM) {
                    int edge = height - piece->info.onscreenHeight - piece->info.posY;
                    if (edge < bottom)
                        bottom = edge;
                } else {
                    int edge = piece->info.posY + piece->info.onscreenHeight;
                    if (edge > top)
                        top = edge;
                }
            }
            pieceHeight = piece->info.onscreenHeight;
        } else {
            pieceHeight = Component_Stretch(bottom - top, piece->stretchHeight);
        }

        // A fixed piece of a focused control pulses; the bytes carry into each other
        uint colour = piece->info.colour;
        if ((state & 0x20) && piece->stretchWidth == 0.0f && piece->stretchHeight == 0.0f) {
            int alpha = Menu_AlphaGet();
            colour = ((colour & 0xff0000) + (alpha << 16)) | ((colour & 0xff000000) + (alpha << 24)) |
                     ((colour & 0xff00) + (alpha << 8)) | (colour & 0xff);
        } else if (state & 0xc0) {
            Menu_AlphaReset();
        }

        uint spriteFlags;
        if (piece->flags & PIECE_BLEND_1)
            spriteFlags = SPRITE_BLEND_1;
        else
            spriteFlags = (piece->flags & PIECE_BLEND_2) ? SPRITE_BLEND_2 : 0;

        int spriteY;
        if (piece->flags & PIECE_BOTTOM)
            spriteY = page->control.y - piece->info.posY - pieceHeight + y + height;
        else if (piece->flags & PIECE_CENTRE_Y)
            spriteY = page->control.y + y + ((height - pieceHeight) >> 1);
        else
            spriteY = page->control.y + y + piece->info.posY;
        int spriteX;
        if (piece->flags & PIECE_RIGHT)
            spriteX = page->control.x - piece->info.posX - pieceWidth + x + width;
        else if (piece->flags & PIECE_CENTRE_X)
            spriteX = page->control.x + x + ((width - pieceWidth) >> 1);
        else
            spriteX = page->control.x + x + piece->info.posX;

        Menu_InitializeSprite(control->sprites[i], (uchar)(control->depth - page->control.depth), spriteX, spriteY,
                              pieceWidth, pieceHeight, NULL, component->texture, colour, 0, piece->info.spritesheetX,
                              piece->info.spritesheetY, piece->info.spritesheetWidth,
                              piece->info.spritesheetHeight, spriteFlags);
    }
}

// AUTOINJECT
void Component_Free(M_CONTROL *control) {
    if (control->sprites == NULL)
        return;
    for (int i = 0; i < control->numSprites; i++)
        Menu_DeleteSprite(control->sprites[i]);
    Menu_Free((void **)&control->sprites, 1);
    control->sprites = NULL;
}

// ---- finding controls

static M_PAGE* Menu_NextPage(M_PAGE *page) {
    if (page->pageLink[0] == NULL)
        return NULL;
    return (M_PAGE *)((char *)page->pageLink[0] - offsetof(M_PAGE, pageLink));
}

// Whether the control is the one the cursor is over: of the controls under it (edges included), the lowest depth. 0 or
// 1 in the whole register, which its callers pass on.
// AUTOINJECT
int Menu_CursorOverMe(M_MANAGER *mgr, M_CONTROL *control) {
    M_PAGE *page = mgr->currentPage;
    if (page == NULL)
        return 0;
    short x = mgr->cursorSprite->positionX - page->control.x;
    short y = mgr->cursorSprite->positionY - page->control.y;
    M_CONTROL *under[32];
    int last = -1;
    for (M_CONTROL *c = (M_CONTROL *)LinkedList_First(&page->controls); c != NULL; c = c->next) {
        if (c->type == ControlType_Window || c->state != CONTROL_STATE_SHOWN)
            continue;
        if (x >= c->x && x <= c->x + c->width && y >= c->y && y <= c->y + c->height)
            under[++last] = c;
    }
    if (last < 0)
        return 0;
    M_CONTROL *top = under[0];
    for (int i = 0; i <= last; i++) {
        if (under[i]->depth < top->depth)
            top = under[i];
    }
    return top == control;
}

// The control under the cursor (edges excluded): the smallest, then the lowest depth; else Menu_GetFirstControl
// AUTOINJECT
M_CONTROL* Menu_GetControl(M_MANAGER *mgr) {
    M_PAGE *page = mgr->currentPage;
    if (page == NULL)
        return Menu_GetFirstControl(mgr);
    short x = mgr->cursorSprite->positionX - page->control.x;
    short y = mgr->cursorSprite->positionY - page->control.y;
    M_CONTROL *under[32];
    int smallness[32];
    int last = -1;
    for (M_CONTROL *c = (M_CONTROL *)LinkedList_First(&page->controls); c != NULL; c = c->next) {
        if (x > c->x && x < c->x + c->width && y > c->y && y < c->y + c->height &&
            c->state == CONTROL_STATE_SHOWN && c->type != ControlType_Window) {
            under[++last] = c;
            smallness[last] = (SCREEN_HEIGHT - c->height) + (SCREEN_WIDTH - c->width);
        }
    }
    if (last < 0)
        return Menu_GetFirstControl(mgr);
    uchar best = 0;
    for (int i = 0; i <= last; i++) {
        if (smallness[i] > smallness[best] ||
            (smallness[i] == smallness[best] && under[i]->depth < under[best]->depth))
            best = i;
    }
    return under[best];
}

#define NO_NEIGHBOUR 9999
#define MENU_NEIGHBOURS 64

// Moves the cursor from the control under it to the nearest in a direction (MENU_DIR_*) among the controls that
// overlap it across that direction; with none there, round to the farthest the other way. The cursor goes to the
// middle of the control, 10 pixels larger.
// AUTOINJECT
M_CONTROL* Menu_FindControl(M_MANAGER *mgr, uchar direction) {
    MENU_NEIGHBOUR nearest[MENU_NEIGHBOURS];
    MENU_NEIGHBOUR wrapped[MENU_NEIGHBOURS];
    ushort numNearest = 0;
    ushort numWrapped = 0;
    M_CONTROL *from = Menu_GetControl(mgr);
    M_CONTROL *c = (M_CONTROL *)LinkedList_First(&mgr->currentPage->controls);
    if (from == NULL) {
        from = c;
        if (from == NULL)
            return NULL;
    }
    nearest[0].control = from;
    wrapped[0].control = from;

    // Only the nearest are counted against the table; the wrap-around list is not bounded
    for (; c != NULL && numNearest < MENU_NEIGHBOURS - 1; c = c->next) {
        if (c == from || c->state != CONTROL_STATE_SHOWN || c->type == ControlType_Window)
            continue;
        int width = c->width;
        int height = c->height;
        if (width * height == 0)
            continue;
        // From the other control's corner to this one's sides
        float left = from->x - c->x;
        float right = from->width - c->x + from->x;
        float top = from->y - c->y;
        float bottom = from->height - c->y + from->y;
        float fwidth = width;
        bool acrossX = left < fwidth && right > 0.0f;
        bool acrossY = bottom > 0.0f && height > top;

        short nearCost = NO_NEIGHBOUR;
        short wrapCost = NO_NEIGHBOUR;
        switch (direction) {
            case MENU_DIR_UP:
                if (height < top && acrossX)
                    nearCost = Ftol(fabs((double)top) * 2 + fabs((double)left) * 0.1);
                if (bottom < 0.0f && acrossX)
                    wrapCost = Ftol(bottom * 2.0 + fabs((double)left) * 0.1);
                break;
            case MENU_DIR_DOWN:
                if (top < height && acrossX)
                    nearCost = Ftol(fabs((double)bottom) * 2 + fabs((double)left) * 0.1);
                if (top > height && acrossX)
                    wrapCost = Ftol(top * -2.0f + fabs((double)left) * 0.1);
                break;
            case MENU_DIR_LEFT:
                if (left > fwidth && acrossY)
                    nearCost = Ftol(fabs((double)top) + fabs((double)left));
                if (right < 0.0f && acrossY)
                    wrapCost = Ftol(fabs((double)top) + right);
                break;
            case MENU_DIR_RIGHT:
                if (left < fwidth && acrossY)
                    nearCost = Ftol(fabs((double)top) + fabs((double)right));
                if (left > fwidth && acrossY)
                    wrapCost = Ftol(fabs((double)top) - left);
                break;
        }
        if (nearCost != NO_NEIGHBOUR) {
            nearest[numNearest].control = c;
            nearest[numNearest].cost = nearCost;
            numNearest++;
        }
        if (wrapCost != NO_NEIGHBOUR) {
            wrapped[numWrapped].control = c;
            wrapped[numWrapped].cost = wrapCost;
            numWrapped++;
        }
    }

    QuickSort(nearest, numNearest, sizeof(MENU_NEIGHBOUR), Compare_MenuCtrl, NULL);
    if (nearest[0].control == from) {
        QuickSort(wrapped, numWrapped, sizeof(MENU_NEIGHBOUR), Compare_MenuCtrl, NULL);
        nearest[0] = wrapped[0];
    }
    M_CONTROL *to = nearest[0].control;
    sprite *cursor = mgr->cursorSprite;
    cursor->positionX = to->x + mgr->currentPage->control.x + (to->width >> 1);
    cursor->positionY = (to->height >> 1) + to->y + mgr->currentPage->control.y;
    cursor->onscreenWidth = to->width + 10;
    cursor->onscreenHeight = to->height + 10;
    cursor->depth = (uchar)to->depth + 1;
    return to;
}

// The page's focused control, or its first that can take focus, or else a stand-in off the screen
// AUTOINJECT
M_CONTROL* Menu_GetFirstControl(M_MANAGER *mgr) {
    M_CONTROL *focused = mgr->currentPage->focused;
    if (focused != NULL && focused->hashcode != NO_CONTROL_HASHCODE)
        return focused;
    memset(&menu_no_control, 0, sizeof(menu_no_control));
    menu_no_control.control.type = 0xff;
    menu_no_control.control.hashcode = NO_CONTROL_HASHCODE;
    menu_no_control.control.x = -100;
    menu_no_control.control.y = -100;
    menu_no_control.control.width = 0;
    menu_no_control.control.height = 0;
    for (M_CONTROL *c = (M_CONTROL *)LinkedList_First(&mgr->currentPage->controls); c != NULL; c = c->next) {
        if (c->type != ControlType_Window && c->state == CONTROL_STATE_SHOWN)
            return c;
    }
    return &menu_no_control.control;
}

// The types that sit on a page (inline on the Xbox)
static bool Menu_Assert(M_CONTROL *control) {
    if (control == NULL || control->parent == NULL)
        return false;
    switch (control->type) {
        case ControlType_Button:
        case ControlType_Checkbox:
        case ControlType_Combo:
        case ControlType_Label:
        case ControlType_List:
        case ControlType_Memo:
        case ControlType_Radio:
        case ControlType_Scroll:
        case ControlType_Spin:
        case ControlType_Text:
        case ControlType_72:
            return true;
    }
    return false;
}

// The page a control is on. A Window's own page is never taken: Menu_Assert refuses Windows.
// AUTOINJECT
M_PAGE* Menu_GetPage(M_CONTROL *control) {
    if (!Menu_Assert(control))
        return NULL;
    if (control->type == ControlType_Window)
        return ((M_WINDOW *)control)->page;
    for (M_CONTROL *owner = control->parent; owner != NULL; owner = owner->parent) {
        if (owner->type == ControlType_Page)
            return (M_PAGE *)owner;
    }
    return NULL;
}

// ---- dispatch

// To the control's type. Nothing is sent to a control about itself.
// AUTOINJECT
intptr_t __Menu_SendMessage(M_CONTROL *control, uint message, intptr_t arg1, intptr_t arg2) {
    if (control == NULL || control == (M_CONTROL *)arg1)
        return 0;
    switch (control->type) {
        case ControlType_Button: return Button_SendMessage((M_BUTTON *)control, message, arg1, arg2);
        case ControlType_Checkbox: return Checkbox_SendMessage((M_CHECKBOX *)control, message, arg1, arg2);
        case ControlType_Combo: return Combo_SendMessage((M_COMBO *)control, message, arg1, arg2);
        case ControlType_Label: return Label_SendMessage((M_LABEL *)control, message, arg1, arg2);
        case ControlType_List: return List_SendMessage((M_LIST *)control, message, arg1, arg2);
        case ControlType_Manager: return Manager_SendMessage((M_MANAGER *)control, (MessageType)message, arg1, arg2);
        case ControlType_Memo: return Memo_SendMessage((M_MEMO *)control, message, arg1, arg2);
        case ControlType_Page: return Page_SendMessage((M_PAGE *)control, message, arg1, arg2);
        case ControlType_Radio: return Radio_SendMessage((M_RADIO *)control, message, arg1, arg2);
        case ControlType_Scroll: return Scroll_SendMessage((M_SCROLL *)control, message, arg1, arg2);
        case ControlType_Spin: return Spin_SendMessage((M_SPIN *)control, message, arg1, arg2);
        case ControlType_Text: return Text_SendMessage((M_TEXTBOX *)control, message, arg1, arg2);
        case ControlType_Window: return Window_SendMessage((M_WINDOW *)control, message, arg1, arg2);
    }
    return 0;
}

// AUTOINJECT
void Menu_Update(M_MANAGER *mgr, M_PAGE *page, M_CONTROL *control) {
    switch (control->type) {
        case ControlType_Button: Button_Update(mgr, page, (M_BUTTON *)control); break;
        case ControlType_Checkbox: Checkbox_Update(mgr, page, (M_CHECKBOX *)control); break;
        case ControlType_Combo: Combo_Update(mgr, page, (M_COMBO *)control); break;
        case ControlType_Label: Label_Update(mgr, page, (M_LABEL *)control, false); break;
        case ControlType_List: List_Update(mgr, page, (M_LIST *)control); break;
        case ControlType_Memo: Memo_Update(mgr, page, (M_MEMO *)control); break;
        case ControlType_Page: Page_Update(mgr, (M_PAGE *)control, 1); break;
        case ControlType_Radio: Radio_Update(mgr, page, (M_RADIO *)control); break;
        case ControlType_Scroll: Scroll_Update(mgr, page, (M_SCROLL *)control); break;
        case ControlType_Spin: Spin_Update(mgr, page, (M_SPIN *)control); break;
        case ControlType_Text: TextBox_Update(mgr, page, (M_TEXTBOX *)control); break;
        case ControlType_Window: Window_Update(mgr, page, control); break;
    }
}

// A player's action (-1: any player's) as Input_Action reports it, noting the frame and controller of one reported
ushort Menu_GetAction(short player, int action, uchar flags) {
    if (player == -1) {
        ushort pressed = 0;
        for (ushort i = 0; i < 4; i++) {
            ushort before = pressed;
            pressed |= Input_Action(i, (GameActions_tag)action, flags);
            if (pressed != before) {
                menu_last_controller = i;
                menu_last_action = menu_frame;
            }
        }
        return pressed;
    }
    if (player >= 4)
        return 0;
    if (!(PlayerInputs[player].actions[action] & flags))
        return 0;
    ushort value = Ftol(PlayerInputs[player].fChannels[action] * 100.0);
    if (value != 0) {
        menu_last_controller = player;
        menu_last_action = menu_frame;
    }
    return value;
}

// An action for the menus: select and start are ignored while back is held, back while either is; in a menu of
// status above 3 start and select stand for each other.
// AUTOINJECT
ushort Menu_InputAction(short player, int action, int flags) {
    if (action == ACTION_MENU_SELECT || action == ACTION_PAUSE) {
        if (Menu_GetAction(player, ACTION_MENU_BACK, ACTION_HELD))
            return 0;
    }
    if (action == ACTION_MENU_BACK) {
        if (Menu_GetAction(player, ACTION_MENU_SELECT, ACTION_HELD) ||
            Menu_GetAction(player, ACTION_PAUSE, ACTION_HELD))
            return 0;
    } else if ((action == ACTION_MENU_SELECT || action == ACTION_PAUSE) &&
               MenuManager_GetStatus() > MENU_STATUS_PAUSE) {
        if (Menu_GetAction(player, ACTION_MENU_SELECT, flags) || Menu_GetAction(player, ACTION_PAUSE, flags))
            return 1;
    }
    return Menu_GetAction(player, action, flags);
}

// To every control of every page of the manager with that hashcode (MENU_SEND_ALL: every control); the last result
// that is not 0. After MENU_SEND_STEPS pages and controls it gives up and answers 0.
// AUTOINJECT
uintptr_t __Menu_Send(uchar managerNum, HASHCODE hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    uintptr_t result = 0;
    uint steps = 0;
    for (M_PAGE *page = (M_PAGE *)LinkedList_First(&manager[managerNum].pages); page != NULL; page = Menu_NextPage(page)) {
        if (++steps > MENU_SEND_STEPS)
            return 0;
        for (M_CONTROL *c = (M_CONTROL *)LinkedList_First(&page->controls); c != NULL; c = c->next) {
            if (++steps > MENU_SEND_STEPS)
                return 0;
            if (c->hashcode == hashcode || (uint)hashcode == MENU_SEND_ALL) {
                uintptr_t answer = __Menu_SendMessage(c, message, arg1, arg2);
                if (answer != 0)
                    result = answer;
            }
        }
    }
    return result;
}

// To the controls with that hashcode and id on the current page; if the last of them answers 0, to those on the page
// under an overlay. The last one's result.
// AUTOINJECT
intptr_t __Menu_SendEx(byte managerNum, HASHCODE hashcode, uint id, uint message, intptr_t arg1, intptr_t arg2) {
    M_MANAGER *m = &manager[managerNum];
    intptr_t result = 0;
    uint steps = 0;
    if (m->currentPage == NULL)
        return 0;
    for (M_CONTROL *c = (M_CONTROL *)LinkedList_First(&m->currentPage->controls); c != NULL; c = c->next) {
        if (++steps > MENU_SEND_STEPS)
            return 0;
        if ((c->hashcode == hashcode || (uint)hashcode == MENU_SEND_ALL) && c->id == id)
            result = __Menu_SendMessage(c, message, arg1, arg2);
    }
    if (result != 0)
        return result;
    if (m->underPage == NULL)
        return 0;
    for (M_CONTROL *c = (M_CONTROL *)LinkedList_First(&m->underPage->controls); c != NULL; c = c->next) {
        if (++steps > MENU_SEND_STEPS)
            return 0;
        if ((c->hashcode == hashcode || (uint)hashcode == MENU_SEND_ALL) && c->id == id)
            result = __Menu_SendMessage(c, message, arg1, arg2);
    }
    return result;
}

// To the controls with that hashcode on the current page whose id, read as a sum of the bits 0x80 down to 1, has the
// bit
// AUTOINJECT
intptr_t __Menu_SendEx2(byte managerNum, HASHCODE hashcode, uint bit, uint message, intptr_t arg1, intptr_t arg2) {
    M_PAGE *page = manager[managerNum].currentPage;
    if (page == NULL)
        return 0;
    intptr_t result = 0;
    uint steps = 0;
    for (M_CONTROL *c = (M_CONTROL *)LinkedList_First(&page->controls); c != NULL; c = c->next) {
        if (++steps > MENU_SEND_STEPS)
            return 0;
        if (c->hashcode != hashcode && (uint)hashcode != MENU_SEND_ALL)
            continue;
        int rest = c->id;
        for (int b = 7; b >= 0; b--) {
            uint value = 1u << b;
            int remainder = (int)((uint)rest - value);
            if (remainder < 0)
                continue;
            if (value == bit) {
                result = __Menu_SendMessage(c, message, arg1, arg2);
                break;
            }
            rest = remainder;
        }
    }
    return result;
}

// ---- sprites, memory and the rest

// Sets a sprite up in the menus' viewer: u 0xffff keeps its texture rectangle, a colour of 0 is grey
// AUTOINJECT
void Menu_InitializeSprite(sprite *spr, uchar priority, short x, short y, short width, short height, char *text,
                           HASHCODE texture, uint colour, uint colour2, ushort u, ushort v, ushort uw, ushort vh,
                           ushort flags) {
    if (spr == NULL)
        return;
    if (texture != 0)
        hashtable_set_sprite(spr, texture);
    spr->depth = priority;
    spr->positionX = x;
    spr->positionY = y;
    spr->onscreenWidth = width;
    spr->onscreenHeight = height;
    spr->format = NULL;
    spr->linkedViewer = VIEWER_MENU;
    spr->flags = flags;
    if (u != 0xffff) {
        spr->spritesheetX = u;
        spr->spritesheetY = v;
        spr->spritesheetWidth = uw;
        spr->spritesheetHeight = vh;
    }
    if (text != NULL) {
        spr->text = text;
        spr->format = MenuTextFormat;
        spr->length = strlen(text);
    } else {
        spr->text = NULL;
    }
    spr->colourTint = colour != 0 ? colour : 0x7f7f7fc0;
    spr->shadowColour = colour2;
}

// FUNC_AT(0x00072c30)
void Menu_ResizeControl(M_CONTROL *control, short x, short y, short width, short height) {
    control->x = x;
    control->y = y;
    control->width = width;
    control->height = height;
}

// FUNC_AT(0x00072c60)
void Menu_ResizeControl(M_CONTROL *control, M_CONTROL *from) {
    control->x = from->x;
    control->y = from->y;
    control->width = from->width;
    control->height = from->height;
}

// Cuts the sprite's text to what fits in the width, and measures what is left into its spritesheet size; the width
// AUTOINJECT
int Menu_ClipString(sprite *spr, int width, char *format) {
    if (spr == NULL || spr->text == NULL)
        return 0;
    spr->length = Font_GetTextCountExtent(spr->text, format, &spr->scale, (float)width);
    GPOINT extent = {0.0f, 0.0f};
    Font_GetTextExtent(spr->text, format, &extent, &spr->scale, (ushort)spr->length);
    spr->spritesheetWidth = Ftol(extent.x);
    spr->spritesheetHeight = Ftol(extent.y);
    return Ftol(extent.x);
}

// AUTOINJECT
sprite* Menu_CreateSprite(SpriteInfo *info) {
    if (info == NULL)
        return Sprite_Create();
    info->linkedViewer = VIEWER_MENU;
    return Sprite_Create2(info);
}

// AUTOINJECT
void* Menu_Malloc(int size) {
    return Mem_Malloc(size, MALLOC_MENU, 0);
}

// The callers' type is not used
// AUTOINJECT
void Menu_Free(void **data, undefined4 type) {
    Mem_Free(data);
}

// AUTOINJECT
void Menu_SetAction(short player, int action, uchar state) {
    if (player == -1) {
        Input_SetAction(0, (GameActions_tag)action, state);
        Input_SetAction(1, (GameActions_tag)action, state);
        Input_SetAction(2, (GameActions_tag)action, state);
        Input_SetAction(3, (GameActions_tag)action, state);
    } else {
        Input_SetAction(player, (GameActions_tag)action, state);
    }
}

// AUTOINJECT
uint Menu_GetLastController(void) {
    return menu_last_controller;
}

// AUTOINJECT
uint Menu_GetNoInputCount(void) {
    return menu_frame - menu_last_action;
}

// AUTOINJECT
void Menu_ResetNoInputCount(void) {
    menu_last_action = menu_frame;
}

// The highlight's pulse: 64 sin, a step of 0.2 a frame
// AUTOINJECT
void Menu_AlphaUpdate(void) {
    double phase = (double)menu_alpha_phase + 0.2f;
    menu_frame++;
    menu_alpha_phase = (float)phase;
    if (phase >= TWO_PI)
        menu_alpha_phase = 0.0f;
    menu_alpha = Ftol((double)(FSin(menu_alpha_phase) * Extended(64.0f)));
}

// AUTOINJECT
void Menu_AlphaReset(void) {
    menu_alpha_phase = 0.0f;
}

// AUTOINJECT
short Menu_AlphaGet(void) {
    return menu_alpha;
}

// The sounds each page allows
// AUTOINJECT
void Menu_EnableSounds(HASHCODE page) {
    switch ((uint)page) {
        case P_MAIN:
        case P_START:
        case P_PROGRESSIVESCAN:
            menu_sounds = MENU_SOUND_SELECT;
            break;
        case P_CNCONTROLS:
            menu_sounds = MENU_SOUND_SELECT | MENU_SOUND_ALT2 | MENU_SOUND_BACK;
            break;
        case P_MPDEBRIEFING:
            menu_sounds = MENU_SOUND_SELECT | MENU_SOUND_ALT1;
            break;
        case P_NFRESULTS:
        case P_NFBONUS:
            menu_sounds = MENU_SOUND_SELECT | MENU_SOUND_ALT1 | MENU_SOUND_ALT2;
            break;
        case P_NFSTATS:
        case P_DSRECORDS:
        case P_DSREWARDS:
        case P_DSGADGETS:
        case P_DSWEAPONS:
        case P_CNDRIVINGCONTROLS:
            menu_sounds = MENU_SOUND_BACK;
            break;
        case SUB_P_NFMAP_WINDOW:
        case P_NFMAP:
        case P_NFSELECT:
        case P_PAUSE:
            menu_sounds = MENU_SOUND_SELECT | MENU_SOUND_ALT1 | MENU_SOUND_BACK;
            break;
        default:
            menu_sounds = MENU_SOUND_SELECT | MENU_SOUND_BACK;
            break;
    }
}

// 0-2 select, alternative selects; 3-6 a scroll's steps; 7 the focus moving; 8 back
// AUTOINJECT
void Menu_PlaySound(uint sound) {
    switch (sound) {
        case 0:
            if (menu_sounds & MENU_SOUND_SELECT)
                Sound_PlayExt(SFX_FRONT_END_MENU_ITEM_SELECT, 100.0f, 0, 0);
            break;
        case 1:
            if (menu_sounds & MENU_SOUND_ALT1)
                Sound_PlayExt(SFX_FRONT_END_MENU_ITEM_SELECT, 100.0f, 0, 0);
            break;
        case 2:
            if (menu_sounds & MENU_SOUND_ALT2)
                Sound_PlayExt(SFX_FRONT_END_MENU_ITEM_SELECT, 100.0f, 0, 0);
            break;
        case 3:
        case 5:
            Sound_PlayExt(SFX_FRONT_END_MENU_ITEM_UP, 100.0f, 0, 0);
            break;
        case 4:
        case 6:
            Sound_PlayExt(SFX_FRONT_END_MENU_ITEM_DOWN, 100.0f, 0, 0);
            break;
        case 7:
            Sound_PlayExt(SFX_FRONT_END_MENU_ITEM_SELECT, 100.0f, 0, 0);
            break;
        case 8:
            if (menu_sounds & MENU_SOUND_BACK)
                Sound_PlayExt(SFX_FRONT_END_MENU_ITEM_DOWN, 100.0f, 0, 0);
            break;
    }
}

// The text's throb: 1 + sin / 32, a step of 0.1 a frame
// AUTOINJECT
void Menu_UpdateTextFmt(void) {
    double phase = (double)menu_text_phase + 0.1f;
    menu_text_phase = (float)phase;
    if (phase >= TWO_PI)
        menu_text_phase = 0.0f;
    menu_text_scale = (float)(FSin(menu_text_phase) * Extended(0.03125f) + Extended(1.0f));
}

// A sprite's text at its normal size
// AUTOINJECT
void Menu_GetNormalTextFmt(sprite *spr) {
    if (spr == NULL)
        return;
    spr->scale.x = 1.0f;
    spr->scale.y = 1.0f;
}
