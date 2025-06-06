#include "Locks.h"

#include "../../util/Random.h"

typedef struct {
    char asciiDigit[4];
    bool discovered;
  } KeyCodeEntry;
  #define KeyCodes (*(KeyCodeEntry (*)[51])0x0029aaf8)
  
  // No need to inject, only called from function below
  void Locks_Init(void) {
    
    // Seed the random number generator
    for(int i = 0; i < GameState.NumFramesUnpaused * 10 & 0x1ff; i++) {
        Rand_Random();
    }
  
    // Set up each key code entry
    for(int i = 0; i < ARRAY_SIZE(KeyCodes); i++) {
  
      for (int digit = 0; digit < 4; digit++) {
        KeyCodes[i].asciiDigit[digit] = '0' + (char)(Rand_Random() % 10); // Original Game Bug: Previously was % 9, so '9' would never be in a keycode
      }
  
      KeyCodes[i].discovered = false;
    
    }
  
  }
  