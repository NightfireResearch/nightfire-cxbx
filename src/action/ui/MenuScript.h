#ifndef MENUSCRIPT_H_
#define MENUSCRIPT_H_

#include "ui.h"

void Script_PrepareScripting(M_CONTROL *control);
M_SCRIPT* Script_AddScript(M_CONTROL *control, int id);
M_KEYFRAME* Script_AddKeyFrame(M_SCRIPT *script, M_KEYFRAME *key);
M_MESSAGE* Script_AddMessage(M_KEYFRAME *key, M_MESSAGE *msg);
M_SCRIPT* Script_Select(M_CONTROL *control, int id);
bool Script_Play(M_CONTROL *control, int id, char atEnd);
bool Script_PlayDefault(M_CONTROL *control, uint message);
bool Script_ExitLoop(M_CONTROL *control);
void Script_Stop(M_CONTROL *control);
double Script_CalculateSpline(int p0, int p1, int p2, int p3, float t);
void Script_InterpolateSpline(M_CONTROL *control, M_SCRIPT *script, float t);
void Script_InterpolateLine(M_CONTROL *control, M_SCRIPT *script, float t);
void Script_DelKeyFrame(M_SCRIPT *script, M_KEYFRAME *key);
void Script_DelScript(M_CONTROL *control, M_SCRIPT *script);
void Script_RunFrame(M_MANAGER *mgr, M_CONTROL *control);
void Script_DelAllScripts(M_CONTROL *control);

#endif // MENUSCRIPT_H_
