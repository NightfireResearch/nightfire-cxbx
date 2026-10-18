#ifndef ACTION_DEVTOOLS_MENUPROBE_H_
#define ACTION_DEVTOOLS_MENUPROBE_H_

// The menu message log and the scripted input replay (MenuProbe.cpp; settings.ini MenuLog / MenuScript).
// Called at the end of Inject(), after the function patches it stands in front of.
void MenuProbe_Install(void);
// Once a frame from Game_Run, before the game's own update: the teleports and frame dump, and the per-frame shadows.
void Devtools_Tick(void);

#endif // ACTION_DEVTOOLS_MENUPROBE_H_
