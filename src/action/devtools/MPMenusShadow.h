#ifndef ACTION_DEVTOOLS_MPMENUSSHADOW_H_
#define ACTION_DEVTOOLS_MPMENUSSHADOW_H_

// Compares the original Menu_PrepareBots, Menu_StoreMPSettings and Menu_RestoreMPSettings with ours, on synthetic
// bot, settings and controller states (MPMenusShadow.cpp).
void MPMenusShadow_Run(void);

// MPMenusShadow_Run when settings.ini has MPMenusShadow=on ([Settings]).
void MPMenusShadow_Install(void);

#endif // ACTION_DEVTOOLS_MPMENUSSHADOW_H_
