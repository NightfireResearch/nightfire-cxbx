#include "Script.h"


typedef enum ScriptCmd {
    ScriptCmd_EndScript=4,
    ScriptCmd_SetSomeFloat=5,
    ScriptCmd_DoSomeThingFromAWord=6,
    ScriptCmd_EntityStart=7,
    ScriptCmd_EntityEnd=9,
    ScriptCmd_AnimStart=10,
    ScriptCmd_AnimEnd=12,
    ScriptCmd_CameraStart=13,
    ScriptCmd_CameraEnd=15,
    ScriptCmd_EventHandler=18,
    ScriptCmd_FadeStart=19,
    ScriptCmd_SpriteStart=20,
    ScriptCmd_SpriteEnd=21,
    ScriptCmd_SoundStart=22,
    ScriptCmd_SoundEnd=23,
    ScriptCmd_LightStart=24,
    ScriptCmd_LightEnd=26,
    ScriptCmd_SubScriptStart=27,
    ScriptCmd_SubScriptEnd=29,
    ScriptCmd_TextStart=30
} ScriptCmd;



// This is just a static variable?
#define Cmd_160 U8_AT(0x002793d0)

void Script_Run(SCRIPTINFO *scriptInfo) {

    


    switch(Cmd_160) {

        
        default:
            //?
    }

    // Advance the read head
    sstream->streamBuffer++;

}

// AUTOGEN
void Script_SetPosRot(SCRIPTINFO *param_1, _MATRIX *param_2);

// AUTOGEN
void Script_SetColour(SCRIPTINFO *param_1, undefined1 clr_r, undefined1 clr_g, undefined1 clr_b);