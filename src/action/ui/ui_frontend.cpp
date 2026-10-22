// The front end's boot pages and main menu: the language page, the idents, the start page, the save check before
// the main menu, the title sequence, the main menu's three buttons, the movie pages and the mini mission
// (PS2: menu_frontend.cpp)

#include "ui.h"
#include "Manager.h"
#include "Menu.h"
#include "MenuManager.h"
#include "Process.h"

#include "../game.h"
#include "../input.h"
#include "../engine/Text.h"
#include "../engine/XboxSystem.h"
#include "../game/mp/multiplayer.h"
#include "../sound/SFX.h"

#include <stdio.h>
#include <string.h>

// Not reimplemented, called by address
#define psiMovieFinished ((bool (__cdecl *)(void))0x000dcd90)

extern int LanguageToLoad;  // tLANGUAGE (XboxSystem.cpp)
extern uint8_t AskForLanguage;  // the boot flow shows the language page
extern uint32_t SubtitlesEnabled;
// XBE_GLOBAL(0x0025d7d8, 0x4)
uint32_t movie_hashcode;  // the movie P_FMVTEST picked
// XBE_GLOBAL(0x002456b0, 0x1)
uint8_t mp_good_bot_taken;  // ui_mp.cpp
// XBE_GLOBAL(0x002456b1, 0x1)
uint8_t mp_bond_bot_taken;

#define TXT_LEGAL_NOTICE ((Action_TranslatedText)0x375)
#define TXT_PRESS_START ((Action_TranslatedText)0x2fb)

#define FMV_IDENT_0738 ((HASHCODE)0x07380048)  // the second ident P_INTRO plays

#define MENU_IDLE_ATTRACT 0xa8c     // frames without input before the start page and the main menu show P_ATTRACT
#define MENU_IDLE_LANGUAGE 600      // frames without input before the language page takes the language shown

// ---- the movies a page plays (Menu_PlayMovie) ----

// AUTOINJECT
void Menu_PlayMovie(HASHCODE movie, uchar skippable, int looping, uchar stopMusic, char lockInput, uint skipDelay) {
    menu_movie_skippable = skippable;
    menu_movie_stopped_music = stopMusic;
    menu_movie_skip_delay = skipDelay;
    if (stopMusic)
        Menu_StopFrontEndMusic();
    psiStartBackgroundMovie(movie, looping, SFXGetVolume());
    Manager_SendMessage(&manager[0], MessageType_LockInput, lockInput, 0);
}

// A skippable movie stops when its delay is over, or when forced
// AUTOINJECT
void Menu_StopMovie(char force) {
    if (!menu_movie_skippable)
        return;
    if (!force && menu_movie_skip_delay > 0)
        return;
    if (menu_movie_stopped_music) {
        SFXUnPause();
        SFXUnPauseAllStreams();
    }
    psiStopBackgroundMovie();
    Manager_SendMessage(&manager[0], MessageType_LockInput, 0, 0);
}

// AUTOINJECT
void Menu_UpdateMovieCounter(void) {
    if (menu_movie_skip_delay > 0)
        menu_movie_skip_delay--;
}

// Once the movie is over: the front end's music again if it was stopped for it, and the input unlocked. True then.
// AUTOINJECT
bool psiMovieLoop(void) {
    if (!psiMovieFinished())
        return false;
    if (menu_movie_stopped_music)
        Menu_RestartFrontEndLoop();
    Manager_SendMessage(&manager[0], MessageType_LockInput, 0, 0);
    return true;
}

// ---- the language page ----

// The help line in each language, the game's strings (labels keep the pointer): English, French, German, Spanish,
// Italian, Dutch, none for American and Japanese, Swedish
static const char *const language_help_text[9] = {
    (const char *)0x0016166c, (const char *)0x00161640, (const char *)0x00161658, (const char *)0x00161608,
    (const char *)0x0016162c, (const char *)0x0016161c, NULL, NULL, (const char *)0x001615f4,
};

// The language of each C_LANGUAGE button (its id)
static const tLANGUAGE language_of_button[7] = {Lang_UK, Lang_GR, Lang_FR, Lang_IT, Lang_DU, Lang_SP, Lang_SW};

// The sound's language for a text language: French, German or Spanish, otherwise English (name ours)
// AUTOINJECT
void Menu_SetSoundLanguage(int language) {
    switch (language) {
        case Lang_FR: SFXSetLanguage(1); break;
        case Lang_GR: SFXSetLanguage(2); break;
        case Lang_SP: SFXSetLanguage(3); break;
        default: SFXSetLanguage(0); break;
    }
}

// The button of a language, -1 for none (name ours)
// AUTOINJECT
int Menu_GetLanguageButton(int language) {
    switch (language) {
        case Lang_UK: return 0;
        case Lang_FR: return 2;
        case Lang_GR: return 1;
        case Lang_SP: return 5;
        case Lang_IT: return 3;
        case Lang_DU: return 4;
        case Lang_SW: return 6;
    }
    return -1;
}

// The languages the page offers, a bit per language (0 until the page is first entered)
// XBE_GLOBAL(0x0025d7cc, 0x4)
uint language_flags;
// XBE_GLOBAL(0x0017d604, 0x4) readonly
static const uint language_flags_default = 0x13f;   // English, French, German, Spanish, Italian, Dutch, Swedish

// Unless it must ask, the language is known and the page goes straight on to the idents. Otherwise the cursor
// starts on the language's button, and the buttons of the languages offered are spaced out down the middle of the
// page from the first's place; the others are hidden. With no input for a while the language shown is taken.
// AUTOINJECT
bool P_LANGUAGE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        __Menu_Send(managerNum, C_LANGUAGE, MessageType_SetPulse, 1, 0);
        ls.done = 0;
        for (int i = 0; i < 4; i++)
            PlayerInputs[i].controllerPort = (undefined1)i;
        if (!AskForLanguage && LanguageToLoad != 9) {
            Txt_SetLanguage((tLANGUAGE)LanguageToLoad);
            Menu_SetSoundLanguage(LanguageToLoad);
            Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_INTRO, 1);
            return true;
        }

        int button = Menu_GetLanguageButton(LanguageToLoad);
        if (button == -1)
            button = 0;
        M_CONTROL *focus = (M_CONTROL *)__Menu_SendEx(managerNum, C_LANGUAGE, button, MessageType_GetControl, 0, 0);
        __Menu_SendMessage(&manager[managerNum].control, MessageType_SetCursor, 0, (int)focus);
        if ((uint)LanguageToLoad < ARRAY_SIZE(language_help_text) && language_help_text[LanguageToLoad] != NULL)
            LABEL_SET_TEXT(managerNum, SUB_P_LANGUAGE_HELP_TEXT, language_help_text[LanguageToLoad]);

        M_CONTROL *shown[16];
        int numShown = 0;
        if (language_flags == 0)
            language_flags = language_flags_default;
        int top = ((M_CONTROL *)__Menu_SendEx(managerNum, C_LANGUAGE, 0, MessageType_GetControl, 0, 0))->y;
        int spacing = ((M_CONTROL *)__Menu_SendEx(managerNum, C_LANGUAGE, 1, MessageType_GetControl, 0, 0))->y - top;
        __Menu_SendEx(managerNum, C_LANGUAGE, 3, MessageType_GetControl, 0, 0);
        for (int language = 0; language < 16; language++) {
            int id = Menu_GetLanguageButton(language);
            if (id == -1)
                continue;
            M_CONTROL *flag = (M_CONTROL *)__Menu_SendEx(managerNum, C_LANGUAGE, id, MessageType_GetControl, 0, 0);
            if (language_flags & (1 << language))
                shown[numShown++] = flag;
            else
                __Menu_SendMessage(flag, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        }
        top += (7 - numShown) * (spacing / 2);
        for (int i = 0; i < numShown; i++)
            shown[i]->y = (short)(top + spacing * i);
    }
    else if (message == MessageType_PageUpdate && Menu_GetNoInputCount() == MENU_IDLE_LANGUAGE) {
        Txt_SetLanguage((tLANGUAGE)LanguageToLoad);
        Menu_SetSoundLanguage(LanguageToLoad);
        Language_SaveChoice();
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_INTRO, 1);
    }
    return true;
}

// A language's button: the cursor on it shows the help line in its language and makes it the language; selecting it
// takes the language
// AUTOINJECT
bool C_LANGUAGE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_GainFocus) {
        if ((uint)control->id < ARRAY_SIZE(language_of_button)) {
            LanguageToLoad = language_of_button[control->id];
            LABEL_SET_TEXT(managerNum, SUB_P_LANGUAGE_HELP_TEXT, language_help_text[LanguageToLoad]);
        }
    }
    else if (message == MessageType_Select) {
        if ((uint)control->id < ARRAY_SIZE(language_of_button))
            LanguageToLoad = language_of_button[control->id];
        Txt_SetLanguage((tLANGUAGE)LanguageToLoad);
        Menu_SetSoundLanguage(LanguageToLoad);
        Language_SaveChoice();
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_INTRO, 1);
    }
    return true;
}

// ---- the idents, the start page, the save check, the title sequence ----

// XBE_GLOBAL(0x0025ddd8, 0x4)
uint intro_step;

// The two idents, one after the other, then the start page
// AUTOINJECT
bool P_INTRO_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        intro_step = 0;
    }
    else if (message == MessageType_PageUpdate) {
        if (intro_step > 0 && intro_step < 4 && !psiMovieLoop())
            return true;
        switch (intro_step++) {
            case 0:
                Menu_PlayMovie(FMV_IDENT_EAGAMES_EN, 0, 0, 1, 1, 0);
                break;
            case 1:
                Menu_PlayMovie(FMV_IDENT_0738, 0, 0, 1, 1, 0);
                break;
            default:
                Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_START, 1);
                break;
        }
    }
    return true;
}

// XBE_GLOBAL(0x0025dde1, 0x1)
uint8_t start_shown;     // the start page has been entered before
// XBE_GLOBAL(0x0025dde8, 0x180)
char start_legal_text[0x180];

// The legal notice and "Press START". The first time, the page takes no input for 240 frames and the prompt
// appears only then. With no input for a while, the attract movie.
// AUTOINJECT
bool P_START_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        for (int i = 0; i < 4; i++)
            PlayerInputs[i].controllerPort = (undefined1)i;
        Menu_RestartFrontEndLoop();
        mp_good_bot_taken = 0;
        mp_bond_bot_taken = 0;
        sprintf(start_legal_text, "%s", Txt_BindLabel(TXT_LEGAL_NOTICE, 0));
        LABEL_SET_TEXT(managerNum, SUB_P_START_LEGAL_TEXT, start_legal_text);
        __Menu_Send(managerNum, SUB_P_START_LEGAL_TEXT, MessageType_SetLineSpacing, 90, 0);
        if (!start_shown) {
            ls.relatedToMainMenuSound = 1;
            LABEL_SET_TEXT(managerNum, SUB_P_START_PROMPT_TEXT, NULL);
            __Menu_SendMessage(control, MessageType_SetInputDelay, 240, 0);
            __Menu_SendDelayed(240, managerNum, SUB_P_START_PROMPT_TEXT, MessageType_SetText,
                               (int)Txt_BindLabel(TXT_PRESS_START, 0), 0);
            start_shown = 1;
        } else {
            __Menu_SendMessage(control, MessageType_SetInputDelay, 0, 0);
            LABEL_SET_TEXT(managerNum, SUB_P_START_PROMPT_TEXT, Txt_BindLabel(TXT_PRESS_START, 0));
        }
        Menu_ResetNoInputCount();
    }
    else if (message == MessageType_PageUpdate && Menu_GetNoInputCount() == MENU_IDLE_ATTRACT) {
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_ATTRACT, 0);
    }
    return true;
}

// The mini mission (the first mission, played straight from the start page) is for a console with no codename saved
// XBE_GLOBAL(0x0017c558, 0x1) readonly
static const uint8_t run_mini_mission = 1;

// The subtitles are on but for English, French, German, Spanish and American; whether the mini mission will run
// AUTOINJECT
uchar Menu_WillRunMiniMission(void) {
    if (LanguageToLoad >= Lang_UK && (LanguageToLoad <= Lang_SP || LanguageToLoad == Lang_USA)) {
        SubtitlesEnabled = 0;
        def_codename[0].subtitles = 0;
        def_codename[1].subtitles = 0;
    } else {
        SubtitlesEnabled = 1;
        def_codename[0].subtitles = 1;
        def_codename[1].subtitles = 1;
    }
    if (run_mini_mission) {
        if (ls.numCodenames == CODENAME_SLOTS_DEFAULT) {
            ls.doMiniMission = 1;
            return 1;
        }
        ls.doMiniMission = 0;
    }
    return 0;
}

// With no codename saved, the first mission is loaded under the default codename
// AUTOINJECT
uchar Menu_RunMiniMission(uchar managerNum) {
    if (run_mini_mission) {
        if (ls.numCodenames == CODENAME_SLOTS_DEFAULT) {
            PlayerInputs[0].controllerPort = (undefined1)Menu_GetLastController();
            strcpy(ls.codename, Txt_BindLabel(def_codename[1].name, 0));
            ls.doMiniMission = 1;
            GameState.NextLevelHashcode = HT_Level_Driving_Paris;
            GameState.difficultyModifier = 1;
            MPSettings.isMultiplayer = 0;
            MPSettings.numPlayers = 1;
            MPSettings.numPlayersAndBots = 1;
            MPSettings.maybeIsTeamGame = 0;
            ResetMap_LevelToLoad(HT_Level_Driving_Paris, false, false);
            GameState.BaseMapHashCode = 0;
            GameFlow_PushState(7, 80.0f, 0xff);
            SFXFadeDown(1);
            MenuManager_Delete(managerNum);
            return 1;
        }
        ls.doMiniMission = 0;
    }
    return 0;
}

// Never set on the Xbox: the frames counted while the loading panel shows before the mini mission
// XBE_GLOBAL(0x0025df68, 0x1)
uint8_t parisenum_countdown;

// Between the start page and the title sequence: the first saved codename is looked for, then either the mini mission
// runs (after the free space is checked: a box if there is too little) or the title sequence follows
// AUTOINJECT
bool P_PARISENUM_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        ls.listStale = 1;
        ls.canSave = 1;
        parisenum_countdown = 0;
        ls.busy = 1;
        ls.slot = LS_SLOT_NEW;
        ls.returnPage = (HASHCODE)0;
        ls.returnPageArg = 0;
        ls.operation = LS_OPERATION_ENUM_FIRST;
        ls.done = 0;
        ls.control = NULL;
        ls.field17_0x42 = 0;
        Menu_UpdateMessageBox(managerNum, 0, 0);
    }
    else if (message == MessageType_PageUpdate) {
        if (parisenum_countdown != 0 && ++parisenum_countdown != 0) {
            if (parisenum_countdown <= 3)
                __Menu_Send(managerNum, SUB_P_NFMAP_LOADING_PANEL, MessageType_SetState, CONTROL_STATE_INERT, 0);
            else if (parisenum_countdown == 4)
                Menu_RunMiniMission(managerNum);
        }
        switch (Menu_UpdateOptionBox(NULL)) {
            case 1:     // ~A: carry on without saving
                Menu_RunMiniMission(managerNum);
                break;
            case 2:     // ~B: free some space
                Menu_GoToDashboard();
                break;
        }
        if (!ls.done) {
            Menu_UpdateMessageBox(managerNum, 4, 0);
            return true;
        }
        ls.done = 0;
        if (Menu_WillRunMiniMission() && !Menu_CheckSaveSpace(managerNum, &ls))
            return true;
        if (!Menu_RunMiniMission(managerNum))
            Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_ESTHERO, 1);
    }
    return true;
}

// XBE_GLOBAL(0x0025dddc, 0x4)
uint esthero_frame;

// The title sequence, skippable after 180 frames, then the main menu
// AUTOINJECT
bool P_ESTHERO_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        esthero_frame = 0;
    }
    else if (message == MessageType_PageUpdate) {
        if (esthero_frame == 1)
            Menu_PlayMovie(FMV_TITLES, 1, 0, 1, 1, 180);
        esthero_frame++;
        if (esthero_frame > 1 && psiMovieLoop())
            __Menu_SendMessage(&manager[managerNum].control, MessageType_GoPage, P_MAIN, 1);
    }
    return true;
}

// ---- the main menu ----

// A process that fades a label's colour to colour over 15 frames
static void Menu_FadeLabel(uchar managerNum, HASHCODE label, uint colour) {
    M_MESSAGE fade = {};
    fade.type = MESSAGE_PROCESS;
    fade.arg1 = colour;
    fade.arg2 = 0;
    Process_Create(CONTROL_GET(managerNum, label), &fade, 15, 0);
}

// The page fades in from black, without input meanwhile. With no input for a while, the attract movie.
// AUTOINJECT
bool P_MAIN_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        Menu_ResetNoInputCount();
        for (int i = 0; i < 4; i++)
            PlayerInputs[i].controllerPort = (undefined1)i;
        Menu_RestartFrontEndLoop();
        LABEL_SET_COLOUR(managerNum, SUB_P_MAIN_FADE, 0xff);
        M_CONTROL *mgr = &manager[managerNum].control;
        __Menu_SendMessage(mgr, MessageType_LockInput, 1, 0);
        __Menu_SendDelayedMessage(15, mgr, MessageType_LockInput, 0, 0);
        __Menu_Send(managerNum, SUB_P_MAIN_TEXT_225, MessageType_SetLineSpacing, 100, 0);
        Menu_FadeLabel(managerNum, SUB_P_MAIN_FADE, 0);
        ls.canSave = 1;
    }
    else if (message == MessageType_PageUpdate && Menu_GetNoInputCount() == MENU_IDLE_ATTRACT) {
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_ATTRACT, 0);
    }
    return true;
}

// The main menu's buttons: the page fades to black, and their page follows
static void Menu_GoFromMain(uchar managerNum, HASHCODE page) {
    M_CONTROL *mgr = &manager[managerNum].control;
    __Menu_SendMessage(mgr, MessageType_LockInput, 1, 0);
    Menu_FadeLabel(managerNum, SUB_P_MAIN_FADE, 0xff);
    __Menu_SendDelayedMessage(19, mgr, MessageType_GoPage, page, 0);
}

// AUTOINJECT
bool C_GONIGHTFIRE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_Select)
        Menu_GoFromMain(managerNum, P_NFSELECT);
    return true;
}

// AUTOINJECT
bool C_GOMULTIPLAYER_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_Select)
        Menu_GoFromMain(managerNum, P_MPJOIN);
    return true;
}

// AUTOINJECT
bool C_GOCODENAMES_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_Select)
        Menu_GoFromMain(managerNum, P_CNSELECT);
    return true;
}

// ---- the movie pages ----

// P_FMVTEST's list (docs/ui/movie-selector.md)
#define SUB_P_FMVTEST_LIST ((HASHCODE)0x10000229)

// The movies P_FMVTEST lists. The space station's arrival names a movie not on the disc (the one played is
// FMV_INTRO_SPACE_STATION).
static const struct { const char *name; HASHCODE movie; } fmvtest_movies[] = {
    {"Castle - Halo Jump", (HASHCODE)FMV_INTRO_STOLEN_HARDWARE},
    {"Castle - Gondola", (HASHCODE)FMV_OUTRO_HELICOPTER_CRASH},
    {"Estate - Meet Mayhew", (HASHCODE)FMV_INTRO_MAYHEW_ENTRY},
    {"Estate - Mayhew Death", (HASHCODE)FMV_OUTRO_MAYHEW_DEAD},
    {"Tower Infil - Intro", (HASHCODE)FMV_INTRO_PHOENIX_TOWER},
    {"Tower Infil - Outro", (HASHCODE)FMV_OUTRO_PARACHUTE_OFF_TOWER},
    {"Nuclear Plant - Interview", (HASHCODE)FMV_INTRO_POWERPLANT},
    {"Nuclear Plant - Entering Vent", (HASHCODE)FMV_OUTRO_THROUGH_VENT_KIKO},
    {"Tower Escape - Dom Death", (HASHCODE)FMV_INTRO_PHOENIX_TOWER_2},
    {"Tower Escape - Lobby Rescue", (HASHCODE)FMV_OUTRO_LOBBY_ESCAPE},
    {"Evil Base - Descent All", (HASHCODE)FMV_INTRO_ISLAND_RADIOTOWER},
    {"Evil Base - Kiko Death", (HASHCODE)FMV_OUTRO_KIKO_ROCKETLAUNCH},
    {"Space Station - Arrival", (HASHCODE)0x07100017},
    {"Arrival - Happy Ending", (HASHCODE)FMV_OUTRO_ESCAPE_POD_END_GAME},
    {"Menu - BG Loop", (HASHCODE)FMV_BACKGROUND_MAINMENU},
    {"Menu - FE Loop", (HASHCODE)FMV_BACKGROUND_MAINMENU2},
    {"Menu - EA Logo", (HASHCODE)FMV_IDENT_EAGAMES_EN},
    {"Menu - MGM Logo", FMV_IDENT_0738},
    {"Menu - DAD Trailer", (HASHCODE)FMV_TRAILER_DIEANOTHERDAY},
    {"Menu - Attract Movie", (HASHCODE)FMV_TRAILER},
    {"Menu - Esthero", (HASHCODE)FMV_TITLES},
    {"Menu - Making Of", (HASHCODE)FMV_BTS_DOCUMENTARY},
};

// A list of movies, the one selected played on P_FMVPLAYER. No page leads here (docs/ui/movie-selector.md).
// AUTOINJECT
bool P_FMVTEST_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        __Menu_Send(managerNum, SUB_P_FMVTEST_LIST, MessageType_ClearItems, 0, 0);
        for (int i = 0; i < ARRAY_SIZE(fmvtest_movies); i++)
            __Menu_Send(managerNum, SUB_P_FMVTEST_LIST, MessageType_AddItem, (int)fmvtest_movies[i].name,
                        fmvtest_movies[i].movie);
    }
    else if (message == MessageType_Select) {
        movie_hashcode = __Menu_Send(managerNum, SUB_P_FMVTEST_LIST, MessageType_GetSelectedItemValue, 0, 0);
        __Menu_SendMessage(&manager[managerNum].control, MessageType_GoPage, P_FMVPLAYER, 0);
    }
    return true;
}

// XBE_GLOBAL(0x0025df6c, 0x4)
uint fmvplayer_frame;

// The movie P_FMVTEST picked, then back (docs/ui/movie-selector.md)
// AUTOINJECT
bool P_FMVPLAYER_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        fmvplayer_frame = 0;
    }
    else if (message == MessageType_PageUpdate) {
        if (fmvplayer_frame == 1 && movie_hashcode != 0)
            Menu_PlayMovie((HASHCODE)movie_hashcode, 1, 0, 1, 1, 0);
        fmvplayer_frame++;
        if (fmvplayer_frame > 1 && psiMovieLoop())
            __Menu_SendMessage(&manager[managerNum].control, MessageType_Back, 0, 0);
    }
    return true;
}

// The trailer (a button of P_CNAVOPTIONS), back when it ends or on A. The original inlines Menu_PlayMovie,
// psiMovieLoop and Menu_StopMovie here, store for store.
// AUTOINJECT
bool P_TRAILER_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    switch (message) {
        case MessageType_Select:
            Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
            break;
        case MessageType_PageEnter:
            Menu_PlayMovie(FMV_TRAILER_DIEANOTHERDAY, 1, 0, 1, 1, 0);
            break;
        case MessageType_PageUpdate:
            if (psiMovieLoop()) {
                Menu_StopMovie(1);
                Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
            }
            break;
    }
    return true;
}

// After the last mission: its movie, then the credits. As P_TRAILER, the original inlines the movie functions.
// AUTOINJECT
bool P_WINGAME_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        Menu_PlayMovie(FMV_BTS_DOCUMENTARY, 1, 0, 1, 1, 0);
    }
    else if (message == MessageType_PageUpdate && psiMovieLoop()) {
        Menu_StopMovie(1);
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_CREDITS, 1);
    }
    return true;
}
