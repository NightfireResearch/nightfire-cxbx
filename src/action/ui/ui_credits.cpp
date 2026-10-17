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
// XBE_GLOBAL(0x00224f68, 0x4)
static CreditsEntry * credits_table;

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
// XBE_GLOBAL(0x0025ed7c, 0x4)
static int credits_music_volume; // the music volume on entry, put back on leaving
// XBE_GLOBAL(0x0025ed80, 0x1)
static uchar credits_fading; // the exit has started
// XBE_GLOBAL(0x0025ed84, 0x4)
static HASHCODE credits_page_from;
// XBE_GLOBAL(0x0025ed88, 0x4)
static uint credits_line; // the next line of the table to show
// XBE_GLOBAL(0x0025ed8c, 0x4)
static uint credits_control_index; // the next control pair to use
// XBE_GLOBAL(0x0025ed90, 0x4)
static CreditsEntry * credits;
// XBE_GLOBAL(0x0025ed94, 0x4)
static uint credits_frame;
// XBE_GLOBAL(0x0025ed98, 0x4)
static uint credits_count;

#define CREDITS_FRAMES_PER_LINE 14
#define CREDITS_NUM_CONTROLS    26

// Fonts per style: the name of the label's font plus these suffixes (MessageType_SetFont)
// XBE_GLOBAL(0x0015eb18, 0x5)
#define credits_format_name    ((const char *)0x0015eb18)
// XBE_GLOBAL(0x00161954, 0x5)
#define credits_format_company ((const char *)0x00161954)
// XBE_GLOBAL(0x0016195c, 0x5)
static const char credits_format_role[] = "\xff\x03\xfe\x03"; // the renderer's formatting codes, byte for byte

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

// AUTOGEN
bool __stdcall psiMovieFinished(void);
// AUTOGEN
void __stdcall SFXUnPause(void);
// AUTOGEN
void __stdcall SFXUnPauseAllStreams(void);
// AUTOGEN
undefined __cdecl Menu_PlayMovie(HASHCODE param_1, undefined1 param_2, char param_3, char param_4, char param_5, undefined4 param_6);

// Menu_PlayMovie's record of the movie it started, read back here when the attract movie ends. Both are also
// used by Menu_StopMovie, psiMovieLoop and the P_TRAILER / P_WINGAME handlers, so they stay the game's.
#define menu_movie_playing      U8_AT(0x0025d7bd)   // Menu_PlayMovie's 2nd argument: a movie page is showing one
#define menu_movie_stopped_music U8_AT(0x0025d7be)  // its 4th: the front-end music was stopped for it

// Which movie the attract page plays: flipped on every entry, so coming back to it from the main menu alternates
// between the trailer and the title sequence. Only P_ATTRACT uses it (PS2: a function-local static, movie_291);
// zero at startup.
// XBE_GLOBAL(0x0025dde0, 0x1)
static uint8_t attract_show_trailer;

// The attract page: a full-screen movie over the front end. It goes back (MessageType_Back) when the movie ends
// or a controller is newly connected (bSkipAttract, set by psiInput_MapInputs).
// AUTOINJECT
bool P_ATTRACT_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        // arg2 is the page it was entered from. The toggle flips whatever the page, but only matters from P_MAIN;
        // from anywhere else it is always the trailer. The original inlines Menu_PlayMovie here (the same six
        // stores and calls, argument for argument), so calling it is the same thing.
        attract_show_trailer = (attract_show_trailer == 0);
        HASHCODE movie = FMV_TRAILER;
        if (arg2 == P_MAIN && !attract_show_trailer)
            movie = FMV_TITLES;
        // stops the front-end music, not looped, locks input
        Menu_PlayMovie(movie, 1, 0, 1, 1, 0);
    }
    else if (message == MessageType_PageUpdate) {
        if (psiMovieFinished()) {
            if (menu_movie_stopped_music)
                Menu_RestartFrontEndLoop();
            Manager_SendMessage(&manager[0], MessageType_LockInput, 0, 0);
        }
        else if (!bSkipAttract) {
            return true;
        }
        // Ended or skipped. (When it ended by itself this unlocks input a second time - as the original.)
        if (menu_movie_playing) {
            if (menu_movie_stopped_music) {
                SFXUnPause();
                SFXUnPauseAllStreams();
            }
            psiStopBackgroundMovie();
            Manager_SendMessage(&manager[0], MessageType_LockInput, 0, 0);
        }
        Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
    }
    return true;
}
