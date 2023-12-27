#include "helpers.h"

void Input_Update(void);
unsigned short __cdecl Input_Action(short player,unsigned int action,unsigned char flags);
float __cdecl Input_Actionf(short playerNum,unsigned int action,unsigned char flags);
void __cdecl Input_ClearAction(short playerNum,unsigned int action);
void __cdecl Input_SetAction(short playerNum,unsigned int action,unsigned char val);
