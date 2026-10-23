// The codename's options pages: the controller styles, the game options, the multiplayer options, the sound and
// picture options; and the controller styles drawn as a picture's labels or as the pause menu's list
// (PS2: menu_options.cpp, menu_controller_style.cpp)

#include "ui.h"
#include "Manager.h"
#include "Menu.h"

#include "../input.h"
#include "../engine/Text.h"
#include "../game/mp/multiplayer.h"
#include "../sound/SFX.h"

#include <stdio.h>

// Not reimplemented, called by address

extern int LanguageToLoad;  // tLANGUAGE (XboxSystem.cpp)
extern uint32_t SubtitlesEnabled;

#define TXT_INVERTED ((Action_TranslatedText)0x206)
#define TXT_NORMAL   ((Action_TranslatedText)0x3bd)
#define TXT_TOGGLE   ((Action_TranslatedText)0x2a2)
#define TXT_HOLD     ((Action_TranslatedText)0x2a3)
#define TXT_HORIZONTAL ((Action_TranslatedText)0x2ba)
#define TXT_VERTICAL ((Action_TranslatedText)0x2b9)

// ---- the controller styles

#define CONTROLLER_STYLES 8

// Each style's first label: its title, its subtitle, then the lines for the buttons, consecutive
static const Action_TranslatedText style_first_label[CONTROLLER_STYLES] = {
    (Action_TranslatedText)0xdd, (Action_TranslatedText)0xef, (Action_TranslatedText)0x101,
    (Action_TranslatedText)0x113, (Action_TranslatedText)0x125, (Action_TranslatedText)0x137,
    (Action_TranslatedText)0x10001aa, (Action_TranslatedText)0x10001bc,
};

// A line of a style
static Action_TranslatedText StyleLine(uint style, int line) {
    return (Action_TranslatedText)(style_first_label[style] + line);
}

// The styles' names, for the radio
static const Action_TranslatedText style_names[CONTROLLER_STYLES] = {
    (Action_TranslatedText)0x28, (Action_TranslatedText)0x37, (Action_TranslatedText)0x46, (Action_TranslatedText)0x55,
    (Action_TranslatedText)0x64, (Action_TranslatedText)0x73, (Action_TranslatedText)0x100016d,
    (Action_TranslatedText)0x100017c,
};

// The labels round the controller's picture, and the line of the style each shows
static const struct { HASHCODE label; uchar line; } style_picture_labels[16] = {
    {SUB_P_CNCONTROLS_LINE_2, 2}, {SUB_P_CNCONTROLS_LINE_3, 3}, {SUB_P_CNCONTROLS_LINE_4, 4}, {SUB_P_CNCONTROLS_LINE_5,
        5},
    {SUB_P_CNCONTROLS_LINE_6, 6}, {SUB_P_CNCONTROLS_LINE_7, 7}, {SUB_P_CNCONTROLS_LINE_8, 8}, {SUB_P_CNCONTROLS_LINE_9,
        9},
    {SUB_P_CNCONTROLS_LINE_10, 10}, {SUB_P_CNCONTROLS_LINE_11, 11}, {SUB_P_CNCONTROLS_LINE_12, 12},
        {SUB_P_CNCONTROLS_LINE_13, 13},
    {SUB_P_CNCONTROLS_LINE_14, 14}, {SUB_P_CNCONTROLS_LINE_15, 15}, {SUB_P_CNCONTROLS_LINE_0, 0},
        {SUB_P_CNCONTROLS_LINE_1, 1},
};

// A style shown round the controller's picture: its lines in the labels, and its own picture
// AUTOINJECT
void Menu_DisplayControllerStyle(uchar managerNum, uint style) {
    if (style >= CONTROLLER_STYLES)
        return;
    for (int i = 0; i < ARRAY_SIZE(style_picture_labels); i++)
        __Menu_Send(managerNum, style_picture_labels[i].label, MessageType_SetText, 0,
                    StyleLine(style, style_picture_labels[i].line));
    __Menu_Send(managerNum, SUB_P_CNCONTROLS_PICTURES, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    __Menu_SendEx2(managerNum, SUB_P_CNCONTROLS_PICTURES, 1 << style, MessageType_SetState, CONTROL_STATE_INERT, 0);
}

// The pause menu's list of a style (C_GCPAUSE's instance 10), and the texts its rows keep
#define STYLE_LIST C_GCPAUSE
#define STYLE_LIST_ID 10
// XBE_GLOBAL(0x002245f0, 0x1)
uint8_t style_list_next;
// XBE_GLOBAL(0x00224d98, 0x1c0)
char style_list_text[14][0x20];

// A row of the list: a button and its line, and another button and line in the second column
// AUTOINJECT
void Menu_UpdateControllerLabelString(uchar managerNum, const char *button, Action_TranslatedText line,
                                      const char *button2, Action_TranslatedText line2) {
    char *text = style_list_text[style_list_next++];
    sprintf(text, "%s %s", button, Txt_BindLabel(line, 0));
    uchar row = (uchar)__Menu_SendEx(managerNum, STYLE_LIST, STYLE_LIST_ID, MessageType_AddItem, (intptr_t)text, 0);
    if (button2 == NULL)
        return;
    text = style_list_text[style_list_next++];
    sprintf(text, "%s %s", button2, Txt_BindLabel(line2, 0));
    __Menu_SendEx(managerNum, STYLE_LIST, STYLE_LIST_ID, MessageType_ListSetCellText, row << 16 | 1, (intptr_t)text);
}

// The line under ~C of styles 3 and 5 is their ~D line too
static const uchar style_list_d_line[CONTROLLER_STYLES] = {10, 10, 10, 9, 10, 9, 10, 10};

// A style as the list's rows. The buttons of the fourth row are named differently in French, Spanish, Italian and
// Swedish.
// AUTOINJECT
void Menu_DisplayControllerStyleList(uchar managerNum, uint style) {
    style_list_next = 0;
    __Menu_SendEx(managerNum, STYLE_LIST, STYLE_LIST_ID, MessageType_ClearItems, 0, 0);
    __Menu_SendEx(managerNum, STYLE_LIST, STYLE_LIST_ID, MessageType_ListAddColumn, 52, 1);
    __Menu_SendEx(managerNum, STYLE_LIST, STYLE_LIST_ID, MessageType_ListSetColumnWidth, 0, 52);
    if (style >= CONTROLLER_STYLES)
        return;
    Menu_UpdateControllerLabelString(managerNum, "~A", StyleLine(style, 14), "~B", StyleLine(style, 12));
    Menu_UpdateControllerLabelString(managerNum, "~X", StyleLine(style, 13), "~Y", StyleLine(style, 11));
    Menu_UpdateControllerLabelString(managerNum, "~C", StyleLine(style, 9), "~D", StyleLine(style, style_list_d_line[style]));
    if (GetLanguage() == Lang_FR)
        Menu_UpdateControllerLabelString(managerNum, "~M", StyleLine(style, 2), "~N", StyleLine(style, 8));
    else if (GetLanguage() == Lang_SP)
        Menu_UpdateControllerLabelString(managerNum, "~O", StyleLine(style, 2), "~N", StyleLine(style, 8));
    else if (GetLanguage() == Lang_IT)
        Menu_UpdateControllerLabelString(managerNum, "~K", StyleLine(style, 2), "~N", StyleLine(style, 8));
    else if (GetLanguage() == Lang_SW)
        Menu_UpdateControllerLabelString(managerNum, "~Q", StyleLine(style, 2), "~P", StyleLine(style, 8));
    else
        Menu_UpdateControllerLabelString(managerNum, "~L", StyleLine(style, 2), "~R", StyleLine(style, 8));
    Menu_UpdateControllerLabelString(managerNum, "~E", StyleLine(style, 3), "~F", StyleLine(style, 15));
    Menu_UpdateControllerLabelString(managerNum, "~H", StyleLine(style, 5), "~V", StyleLine(style, 7));
    Menu_UpdateControllerLabelString(managerNum, "~T", StyleLine(style, 1), NULL, (Action_TranslatedText)0);
}

// The style being chosen is drawn as the radio turns; X inverts the look (shown, kept until A)
// XBE_GLOBAL(0x0025d7df, 0x1)
uint8_t controls_inverted;

// AUTOINJECT
bool P_CNCONTROLS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message != MessageType_PageEnter)
        return true;
    RADIO_CLEAR(managerNum, C_RBCONTROL);
    for (int style = 0; style < CONTROLLER_STYLES; style++)
        RADIO_ADD_ITEM(managerNum, C_RBCONTROL, style_names[style], style);
    RADIO_SELECT_ITEM(managerNum, C_RBCONTROL, PlayerInputs[0].controlStyle);
    Menu_DisplayControllerStyle(managerNum, PlayerInputs[0].controlStyle);
    controls_inverted = PlayerInputs[0].inverted;
    LABEL_SET_TEXT(managerNum, SUB_P_CNCONTROLS_INVERTED_TEXT, Txt_BindLabel(controls_inverted ? TXT_INVERTED : TXT_NORMAL, 0));
    return true;
}

// AUTOINJECT
bool C_RBCONTROL_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
        case MessageType_Scroll:
            Menu_DisplayControllerStyle(managerNum, RADIO_GET_VALUE_OF(control));
            break;
        case MessageType_Select:
            cn_modified = 1;
            Menu_ChangeControllerStyle(0, RADIO_GET_VALUE_OF(control));
            PlayerInputs[0].inverted = controls_inverted;
            Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
            break;
        case MessageType_AltSelect2:
            controls_inverted = !controls_inverted;
            LABEL_SET_TEXT(managerNum, SUB_P_CNCONTROLS_INVERTED_TEXT,
                           Txt_BindLabel(controls_inverted ? TXT_INVERTED : TXT_NORMAL, 0));
            break;
    }
    return true;
}

// ---- the options pages: radios of two or more choices, each the value of a setting

// A two-choice radio over one of the first player's settings (a byte)
typedef struct {
    HASHCODE radio;
    Action_TranslatedText first;
    int firstValue;
    Action_TranslatedText second;
    int secondValue;
    uchar PlayerInput::*setting;
} OPTION_RADIO;

static const OPTION_RADIO game_options[8] = {
    {SUB_P_CNOPTIONS_VIBRATION, MP_ON, 1, MP_OFF, 0, &PlayerInput::vibrationEnabled},
    {SUB_P_CNOPTIONS_AUTOAIM, MP_ON, 1, MP_OFF, 0, &PlayerInput::autoAim},
    {SUB_P_CNOPTIONS_CROSSHAIRS, MP_ON, 1, MP_OFF, 0, &PlayerInput::crosshairsEnabled},
    {SUB_P_CNOPTIONS_CROUCH, TXT_TOGGLE, 1, TXT_HOLD, 0, &PlayerInput::crouchToggle},
    {SUB_P_CNOPTIONS_MANUAL_AIM, TXT_TOGGLE, 1, TXT_HOLD, 0, &PlayerInput::manualAimToggle},
    {SUB_P_CNOPTIONS_WEAPON_AUTO_SWITCH, MP_ON, 1, MP_OFF, 0, &PlayerInput::autoSwitchBetterWeapon},
    {SUB_P_CNOPTIONS_FLASHING_OBJECTS, MP_ON, 2, MP_OFF, 0, &PlayerInput::flashingObjects},
    {SUB_P_CNOPTIONS_HUD_ALWAYS_ON, MP_ON, 0, MP_OFF, 1, &PlayerInput::hudVisibility},
};

static void Option_Fill(uchar managerNum, const OPTION_RADIO *option, int value) {
    RADIO_CLEAR(managerNum, option->radio);
    RADIO_ADD_ITEM(managerNum, option->radio, option->first, option->firstValue);
    RADIO_ADD_ITEM(managerNum, option->radio, option->second, option->secondValue);
    RADIO_SELECT_ITEM(managerNum, option->radio, value);
}

// The game options: vibration, auto-aim, crosshairs, crouch and aim (toggle or hold), switching to a better weapon,
// the flashing of objects (2 for on), and the HUD (0 for on)
// AUTOINJECT
bool P_CNOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_PageEnter) {
        for (int i = 0; i < ARRAY_SIZE(game_options); i++)
            Option_Fill(managerNum, &game_options[i], PlayerInputs[0].*game_options[i].setting);
    }
    else if (message == MessageType_Select) {
        cn_modified = 1;
        for (int i = 0; i < ARRAY_SIZE(game_options); i++)
            PlayerInputs[0].*game_options[i].setting = (uchar)RADIO_GET_VALUE(managerNum, game_options[i].radio);
        Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
    }
    return true;
}

// The handicaps offered, in quarters
static const struct { const char *text; int value; } handicaps[8] = {
    {"-75", -75}, {"-50", -50}, {"-25", -25}, {"0", 0}, {"+25", 25}, {"+50", 50}, {"+75", 75}, {"+100", 100},
};

// The multiplayer options: an on/off setting, the handicap, auto-aim
// AUTOINJECT
bool P_CNMPOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_PageEnter) {
        RADIO_CLEAR(managerNum, SUB_P_CNMPOPTIONS_RADAR);
        RADIO_ADD_ITEM(managerNum, SUB_P_CNMPOPTIONS_RADAR, MP_ON, 1);
        RADIO_ADD_ITEM(managerNum, SUB_P_CNMPOPTIONS_RADAR, MP_OFF, 0);
        RADIO_SELECT_ITEM(managerNum, SUB_P_CNMPOPTIONS_RADAR, MPSettings.Player[0].SomeField2);
        RADIO_CLEAR(managerNum, SUB_P_CNMPOPTIONS_HANDICAP);
        for (int i = 0; i < ARRAY_SIZE(handicaps); i++)
            __Menu_Send(managerNum, SUB_P_CNMPOPTIONS_HANDICAP, MessageType_AddItem, (intptr_t)handicaps[i].text,
                        handicaps[i].value);
        RADIO_SELECT_ITEM(managerNum, SUB_P_CNMPOPTIONS_HANDICAP, MPSettings.Player[0].HealthModifier);
        RADIO_CLEAR(managerNum, SUB_P_CNMPOPTIONS_AUTOAIM);
        RADIO_ADD_ITEM(managerNum, SUB_P_CNMPOPTIONS_AUTOAIM, MP_ON, 1);
        RADIO_ADD_ITEM(managerNum, SUB_P_CNMPOPTIONS_AUTOAIM, MP_OFF, 0);
        RADIO_SELECT_ITEM(managerNum, SUB_P_CNMPOPTIONS_AUTOAIM, PlayerInputs[0].autoAimMultiplayer);
    }
    else if (message == MessageType_Select) {
        cn_modified = 1;
        MPSettings.Player[0].SomeField2 = RADIO_GET_VALUE(managerNum, SUB_P_CNMPOPTIONS_RADAR);
        MPSettings.Player[0].HealthModifier = RADIO_GET_VALUE(managerNum, SUB_P_CNMPOPTIONS_HANDICAP);
        PlayerInputs[0].autoAimMultiplayer = (uchar)RADIO_GET_VALUE(managerNum, SUB_P_CNMPOPTIONS_AUTOAIM);
        Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
    }
    return true;
}

#define AV_VOLUME_STEPS 20      // the volume scrolls' range, a fifth of the volume each

// The volumes as the page found them, put back when it is left any way but by A or a button that leads on
// XBE_GLOBAL(0x0025ed70, 0x4)
int av_music_volume;
// XBE_GLOBAL(0x0025ed74, 0x4)
int av_sfx_volume;
// XBE_GLOBAL(0x0025ed78, 0x1)
uint8_t av_leaving;

// The sound and picture options: the volumes (heard as they change), subtitles, the split screen's layout, and
// buttons to the screen adjustment, the credits, the trailer and the defaults
// AUTOINJECT
bool P_CNAVOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_PageEnter:
        av_leaving = 0;
        if ((HASHCODE)arg2 != P_CNMENU)
            break;
        av_sfx_volume = SFXGetVolume() / 5;
        av_music_volume = SFXMusicGetVolume() / 5;
        __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_MUSIC_VOLUME, MessageType_SetRange, 0, AV_VOLUME_STEPS);
        __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_MUSIC_VOLUME, MessageType_ScrollNoWrap, 0, 0);
        __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_MUSIC_VOLUME, MessageType_SetValue, av_music_volume, 0);
        __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_SFX_VOLUME, MessageType_SetRange, 0, AV_VOLUME_STEPS);
        __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_SFX_VOLUME, MessageType_ScrollNoWrap, 0, 0);
        __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_SFX_VOLUME, MessageType_SetValue, av_sfx_volume, 0);
        RADIO_CLEAR(managerNum, SUB_P_CNAVOPTIONS_SUBTITLES);
        RADIO_ADD_ITEM(managerNum, SUB_P_CNAVOPTIONS_SUBTITLES, MP_ON, 1);
        RADIO_ADD_ITEM(managerNum, SUB_P_CNAVOPTIONS_SUBTITLES, MP_OFF, 0);
        RADIO_SELECT_ITEM(managerNum, SUB_P_CNAVOPTIONS_SUBTITLES, SubtitlesEnabled);
        RADIO_CLEAR(managerNum, SUB_P_CNAVOPTIONS_SPLIT_SCREEN);
        RADIO_ADD_ITEM(managerNum, SUB_P_CNAVOPTIONS_SPLIT_SCREEN, TXT_HORIZONTAL, 0);
        RADIO_ADD_ITEM(managerNum, SUB_P_CNAVOPTIONS_SPLIT_SCREEN, TXT_VERTICAL, 1);
        RADIO_SELECT_ITEM(managerNum, SUB_P_CNAVOPTIONS_SPLIT_SCREEN, MultiplayerLayout_LeftRightOrTopBtm);
        RADIO_CLEAR(managerNum, SUB_P_CNAVOPTIONS_SPEAKER);
        __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_SPEAKER, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_WIDESCREEN, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        break;
    case MessageType_PageLeave:
        if (!av_leaving) {
            SFXMusicSetVolume(av_music_volume * 5);
            SFXSetVolume(av_sfx_volume * 5);
        }
        break;
    case MessageType_PageUpdate:
        SFXMusicSetVolume(__Menu_Send(managerNum, SUB_P_CNAVOPTIONS_MUSIC_VOLUME, MessageType_GetValue, 0, 0) * 5);
        SFXSetVolume(__Menu_Send(managerNum, SUB_P_CNAVOPTIONS_SFX_VOLUME, MessageType_GetValue, 0, 0) * 5);
        break;
    case MessageType_Select:
        switch (((M_CONTROL *)arg2)->hashcode) {
        case SUB_P_CNAVOPTIONS_SCREEN_ADJUST:
            av_leaving = 1;
            __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_SCREEN_ADJUST, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
            Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_SCREENADJUST, 0);
            __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_SCREEN_ADJUST, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
            break;
        case SUB_P_CNAVOPTIONS_CREDITS:
        case SUB_P_CNAVOPTIONS_TRAILER:
            av_leaving = 1;
            break;
        case SUB_P_CNAVOPTIONS_DEFAULTS:
            __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_MUSIC_VOLUME, MessageType_SetValue, 15, 0);
            __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_SFX_VOLUME, MessageType_SetValue, 20, 0);
            // as Menu_WillRunMiniMission: subtitles but for English, French, German, Spanish and American
            __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_SUBTITLES, MessageType_SelectItemByValue,
                        LanguageToLoad >= Lang_UK && (LanguageToLoad <= Lang_SP || LanguageToLoad == Lang_USA) ? 0 : 1, 0);
            RADIO_SELECT_ITEM(managerNum, SUB_P_CNAVOPTIONS_SPLIT_SCREEN, 0);
            break;
        default:
            cn_modified = 1;
            av_music_volume = __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_MUSIC_VOLUME, MessageType_GetValue, 0, 0);
            av_sfx_volume = __Menu_Send(managerNum, SUB_P_CNAVOPTIONS_SFX_VOLUME, MessageType_GetValue, 0, 0);
            SubtitlesEnabled = RADIO_GET_VALUE(managerNum, SUB_P_CNAVOPTIONS_SUBTITLES);
            MultiplayerLayout_LeftRightOrTopBtm = RADIO_GET_VALUE(managerNum, SUB_P_CNAVOPTIONS_SPLIT_SCREEN);
            Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
            break;
        }
        break;
    }
    return true;
}
