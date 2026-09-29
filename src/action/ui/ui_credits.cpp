#include "ui.h"
#include "Manager.h"
#include "Menu.h"

#include "../engine/Text.h"
#include "../sound/SFX.h"
#include "../game.h"

// The credits: a table of 578 lines (data/credits.csv, generated into CreditsData.inc - see docs/ui/credits.md)
// that P_CREDITS scrolls up the screen through 26 recycled pairs of text controls, one new line every 14 frames.


// A line of the credits as written in the source: a text label, or a literal where the label is TXT_NONE (the
// names are not translated, and not in the text banks).
#define TXT_NONE 0xffffffffu
typedef struct {
    uchar centred, lstyle, rstyle;
    uint llabel, rlabel;       // Action_TranslatedText
    const char *ltext, *rtext;
} CreditsLine;

static const CreditsLine credits_lines[] = {
#include "CreditsData.inc"
};
#define CREDITS_NUM_LINES 578
static_assert(ARRAY_SIZE(credits_lines) == CREDITS_NUM_LINES, "the credits have 578 lines");

// The table, built on the first Menu_SetupCredits after Menu_InitCredits (the labels resolved in the language
// then loaded) and kept.
#define credits_table (*(CreditsEntry **)0x00224f68)

// AUTOINJECT
void __stdcall Menu_InitCredits(void) {
    credits_table = NULL;
}

// The original builds all 578 entries in a local array on every call, resolving each label, and copies it to the
// heap only the first time; since later copies are thrown away, building the table once is the same thing.
// AUTOINJECT
CreditsEntry* Menu_SetupCredits(uint *numLines_out) {
    if (credits_table == NULL) {
        CreditsEntry *table = (CreditsEntry *)Menu_Malloc(sizeof(CreditsEntry) * CREDITS_NUM_LINES);
        for (int i = 0; i < CREDITS_NUM_LINES; i++) {
            const CreditsLine *line = &credits_lines[i];
            table[i].txt_left = line->llabel != TXT_NONE ? Txt_BindLabel((Action_TranslatedText)line->llabel, 0) : line->ltext;
            table[i].txt_right = line->rlabel != TXT_NONE ? Txt_BindLabel((Action_TranslatedText)line->rlabel, 0) : line->rtext;
            table[i].modifiers_left = line->lstyle;
            table[i].modifiers_right = line->rstyle;
            table[i].centred = line->centred;
        }
        credits_table = table;
    }
    *numLines_out = CREDITS_NUM_LINES;
    return credits_table;
}

// The page's state
#define credits_music_volume   (*(int *)0x0025ed7c)    // the music volume on entry, put back on leaving
#define credits_fading         (*(uchar *)0x0025ed80)  // the exit has started
#define credits_page_from      (*(HASHCODE *)0x0025ed84)
#define credits_line           (*(uint *)0x0025ed88)   // the next line of the table to show
#define credits_control_index  (*(uint *)0x0025ed8c)   // the next control pair to use
#define credits                (*(CreditsEntry **)0x0025ed90)
#define credits_frame          (*(uint *)0x0025ed94)
#define credits_count          (*(uint *)0x0025ed98)

#define CREDITS_FRAMES_PER_LINE 14
#define CREDITS_NUM_CONTROLS    26

// Fonts per style: the name of the label's font plus these suffixes (MessageType_SetFont)
#define credits_format_name    ((const char *)0x0015eb18)
#define credits_format_company ((const char *)0x00161954)
#define credits_format_role    ((const char *)0x0016195c)

static const char *CreditsFormat(uchar style) {
    if (style == 1)
        return credits_format_company;
    if (style == 2)
        return credits_format_role;
    return credits_format_name;
}

// AUTOGEN
void Process_Create(M_CONTROL *control, M_MESSAGE *msg, undefined4 delay);

// AUTOINJECT
bool P_CREDITS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    switch (message) {
    case MessageType_PageEnter:
        credits_frame = 0;
        credits = Menu_SetupCredits(&credits_count);
        credits_control_index = 0;
        credits_line = 0;
        credits_page_from = (HASHCODE)arg2;
        __Menu_Send(managerNum, SUB_P_CREDITS_LEFT_TEXT, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_Send(managerNum, SUB_P_CREDITS_RIGHT_TEXT, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_Send(managerNum, SUB_P_CREDITS_FADE, MessageType_SetColour, 0, 0);
        credits_fading = 0;
        __Menu_Send(managerNum, SUB_P_CREDITS_BACKGROUND, MessageType_SetColour,
                    GameState.WeaponUpgradeRelated ? 0x404040ff : 0xff, 0);
        credits_music_volume = SFXMusicGetVolume();
        Menu_StopFrontEndMusic();
        SFXStartMusic(0x27, 0);
        break;

    case MessageType_PageLeave:
        SFXStopMusic();
        SFXMusicSetVolume(credits_music_volume);
        Menu_RestartFrontEndLoop();
        break;

    case MessageType_PageUpdate: {
        if (credits == NULL)
            break;
        credits_frame++;
        if (credits_frame % CREDITS_FRAMES_PER_LINE == 0 && credits_line < credits_count) {
            // The next line enters at the bottom, in the pair of controls the oldest line has scrolled off in:
            // two columns of 254 pixels, or the left one 524 wide for a centred line.
            M_CONTROL *left = (M_CONTROL *)__Menu_SendEx(managerNum, SUB_P_CREDITS_LEFT_TEXT, credits_control_index, MessageType_GetControl, 0, 0);
            M_CONTROL *right = (M_CONTROL *)__Menu_SendEx(managerNum, SUB_P_CREDITS_RIGHT_TEXT, credits_control_index, MessageType_GetControl, 0, 0);
            if (left == NULL || right == NULL)
                break;
            const CreditsEntry *entry = &credits[credits_line];
            left->y = 0x200;
            right->y = 0x200;
            __Menu_SendMessage(left, MessageType_SetState, CONTROL_STATE_INERT, 0);
            __Menu_SendMessage(left, MessageType_SetText, (int)entry->txt_left, 0);
            __Menu_SendMessage(left, MessageType_SetFont, (int)CreditsFormat(entry->modifiers_left), 0);
            __Menu_SendMessage(right, MessageType_SetFont, (int)CreditsFormat(entry->modifiers_right), 0);
            if (!entry->centred) {
                left->width = 0xfe;
                right->width = 0xfe;
                __Menu_SendMessage(right, MessageType_SetState, CONTROL_STATE_INERT, 0);
                __Menu_SendMessage(right, MessageType_SetText, (int)entry->txt_right, 0);
            } else {
                left->width = 0x20c;
                __Menu_SendMessage(right, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
            }
            credits_control_index = (credits_control_index + 1) % CREDITS_NUM_CONTROLS;
            credits_line++;
        }

        // The music fades out over the last 50 lines, and is silent by the time the last one appears.
        if (credits_frame / CREDITS_FRAMES_PER_LINE > credits_count - 50) {
            double volume = (double)credits_music_volume * (double)(1.0f / 700.0f) *
                            (double)(int)(credits_count * CREDITS_FRAMES_PER_LINE - credits_frame);
            if (volume < 0.0)
                volume = 0.0;
            SFXMusicSetVolume((int)volume);
        }

        // 18 lines after the last, fade to black and leave: to the main menu after the end of the game, otherwise
        // back where the credits were entered from.
        if (!credits_fading && credits_frame / CREDITS_FRAMES_PER_LINE > credits_count + 18) {
            credits_fading = 1;
            M_CONTROL *mgr = (M_CONTROL *)&manager[managerNum];
            if (credits_page_from == P_WINGAME)
                __Menu_SendDelayedMessage(0x13, mgr, MessageType_GoPage, P_MAIN, 0);
            else
                __Menu_SendDelayedMessage(0x13, mgr, MessageType_Back, 0, 0);
            __Menu_SendDelayedMessage(0x13, mgr, MessageType_LockInput, 0, 0);   // unlocked again
            M_MESSAGE fade = {};
            fade.type = 0xe1;
            fade.arg1 = 0xff;
            fade.arg2 = 0;
            Process_Create(CONTROL_GET(managerNum, SUB_P_CREDITS_FADE), &fade, 0xf);
        }

        for (uint i = 0; i < CREDITS_NUM_CONTROLS; i++) {
            M_CONTROL *left = (M_CONTROL *)__Menu_SendEx(managerNum, SUB_P_CREDITS_LEFT_TEXT, i, MessageType_GetControl, 0, 0);
            M_CONTROL *right = (M_CONTROL *)__Menu_SendEx(managerNum, SUB_P_CREDITS_RIGHT_TEXT, i, MessageType_GetControl, 0, 0);
            if (left != NULL && right != NULL) {
                left->y -= 2;
                right->y -= 2;
            }
        }
        break;
    }

    case MessageType_ControlCreated:
        Menu_InitCredits();
        break;
    }
    return true;
}
