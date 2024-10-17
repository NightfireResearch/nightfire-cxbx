long Handler_HandleMessage(uchar param_1, M_CONTROL *param_2, uint param_3, int param_4, int param_5);
undefined4 C_SBDOSSIER_Handler(uchar param_1,M_CONTROL *param_2,uint param_3,uint param_4,int param_5,int param_6);
undefined4 __cdecl P_DOSSIER_Handler(uchar param_1, M_CONTROL* param_2, uint param_3, uint param_4, int param_5, int param_6);

typedef enum {

	UIEvent_Scroll = 0x49, // Scroll (vertical?) event - fired by both d-pad and left analog stick
	UIEvent_Select = 0x4b, // Selecting an item - fired by A or Start button
	UIEvent_Enter = 0x54, // Entering / Loading the menu page?
	FORCE_U32 = 0x7fffffff
} UIEvent;