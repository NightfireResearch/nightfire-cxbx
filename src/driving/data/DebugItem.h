#ifndef DRIVING_DATA_DEBUGITEM_H_
#define DRIVING_DATA_DEBUGITEM_H_

// The debug menu items' small virtual methods (0x000379e0..0x00037ac0): the PS2 build's DebugItem and DebugData
// (names from it), the bases of DebugVariable<unsigned int>; the classes are DebugVariables.h's. Their vtables:
//   DebugItem (0x0018bc78, 20 slots): 0 the deleting destructor, 1-4 Increase, Decrease, IncreaseAxis2,
//     DecreaseAxis2 (empty), 5 Increment, 6 Decrement, 7-8 SetToMin, SetToMax (empty), 9 AsString, 10
//     Axis2AsString, 11 Debounce (false), 12 RenderCustomAt (empty), 13 Activate, 14 SubMenuNamespace, 15 Name,
//     16 GetItem (this; OBB::Construct's code), 17 IsHidden, 18 IsIndexed (false), 19 GetIndexer (null);
//   DebugVariable<unsigned int> (0x0018bd00, 21 slots): the same with its own Increase, Decrease, SetToMin,
//     SetToMax, AsString, Debounce, DebugData's IsIndexed and GetIndexer, and 20 SetFromString.
// The Xbox has no debug menu: nothing calls these through the vtables. GetIndexer's code is also a getter the
// linker shares with RVehicle::PostLoad, which calls it directly for RSceneObj::Desc().

#include "DebugVariables.h"

#endif // DRIVING_DATA_DEBUGITEM_H_
