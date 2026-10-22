#ifndef ACTION_DEVTOOLS_MENUPROBE_H_
#define ACTION_DEVTOOLS_MENUPROBE_H_

// The menu message log and the scripted input replay (MenuProbe.cpp; settings.ini MenuLog / MenuScript).
// Called at the end of Inject(), after the function patches it stands in front of.
void MenuProbe_Install(void);
// Once a frame from Game_Run, before the game's own update: the teleports and frame dump, and the per-frame shadows.
void Devtools_Tick(void);

// The pads' poll and the menu handlers' messages as our code makes them: through the probe's stand-ins while a
// script replays (the poll) or the messages are logged or sent to originals, else straight to ours. The originals'
// calls reach the same stand-ins through their entries.
void MenuProbe_PollDevices(void);
bool MenuProbe_HandleMessage(unsigned char managerNum, struct M_CONTROL *control, unsigned message, int arg1,
                             int arg2);

#endif // ACTION_DEVTOOLS_MENUPROBE_H_
