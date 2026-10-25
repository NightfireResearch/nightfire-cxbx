#include "SoundMap.h"

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// Two map head allocators (0x00094030, 0x00094070), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x00094030)
TreeNode* MapBuyHead() {
    return BuyMapHead<TreeNode>();
}

// FUNC_AT(0x00094070)
RefCounterNode* RefCounterMapBuyHead() {
    return BuyMapHead<RefCounterNode>();
}
