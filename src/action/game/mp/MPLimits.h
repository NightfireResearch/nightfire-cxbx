#ifndef MPLIMITS_H
#define MPLIMITS_H

// The multiplayer agents: the human players take slots 0 to NUM_PLAYERS - 1, the bots the slots after them
// (MPSettings.Player[], MPGame.players[], Control_Plr2Ind). The records sized from these keep their size asserts,
// so a change here shows every layout that moves.
constexpr int NUM_PLAYERS = 4;
constexpr int NUM_BOTS = 6;
constexpr int NUM_AGENTS = NUM_PLAYERS + NUM_BOTS;

#endif // MPLIMITS_H
