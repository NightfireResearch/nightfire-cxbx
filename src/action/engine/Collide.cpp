#include "Collide.h"

#include <string.h>

#define HitHeap (*(LLISTINFO_tag*)0x001ddc60)

// AUTOGEN
bool Collide_RayIntersect(_VECTOR *position, _VECTOR *endPosition, cel_tag *cel, obj_tag* obj, obj_tag* param_5, HITDATA_tag** hitList, char param_7, uint maybeFlags, ushort param_9);
// AUTOGEN
bool Intersect_ConeSphere(_VECTOR *coneApex, float *coneDirection, float coneCos, float coneSin, _VECTOR *sphereCenter);

// AUTOINJECT
void Collide_FreeHitList(HITDATA_tag **hitList) {

    // If the list has already been freed, return
    if(hitList == NULL)
        return;

    // Iterate through the list and add each element to the HitHeap
    LLNODE_tag* head = (LLNODE_tag*)*hitList;
    while(head != NULL) {
        LLNODE_tag* next = (head->next);
        LList_Add(&HitHeap, (LLNODE_tag*)head);
        head = next;
    }

    // Discard all entries from the list
    *hitList = NULL;

    // ?? don't update the count?!

}

#define HitAllocCnt U32_AT(0x001dec24)

// AUTOINJECT
HITDATA_tag* Coll_GetFreeHit(void) {

    // Try to obtain from the heap
    HITDATA_tag* node = (HITDATA_tag*)LList_Cut(&HitHeap);
    if(node == NULL) {

        // Heap is empty and the maximum number of allocations has been reached  
        if(HitAllocCnt > 1000)
            return NULL;

        // Heap is empty but we haven't reached the maximum number of allocations yet
        HitAllocCnt += 0x40;
        LList_AllocnNodes(&HitHeap, 0x40);
        node = (HITDATA_tag*)LList_Cut(&HitHeap);
    }

    memset(node, 0, sizeof(HITDATA_tag)); 
    return node;
}


// AUTOINJECT
bool Collide_LineOfSight(_VECTOR *param_1, _VECTOR *param_2, cel_tag *param_3, obj_tag *param_4, obj_tag *param_5, uint param_6) {
  HITDATA_tag *hitData = NULL;
  bool intersects = Collide_RayIntersect(param_1, param_2, param_3, param_4, param_5, &hitData, 0, param_6 | 4, 0x10);
  if (intersects) {
    // Discard the results, we only care whether an intersection occurred
    Collide_FreeHitList(&hitData);
  }
  return !intersects;
}

// Are the points on opposite sides of the plane, and does their line segment intersect the region bounded?
// AUTOINJECT
bool Collide_RayTriangle(_VECTOR *ptStart,_VECTOR *ptEnd,_VECTOR *vtx1,_VECTOR *vtx2,_VECTOR *vtx3, float *distanceOut) {
  _VECTOR intersectPt;
  plane_equ_tag planeEq;
  
  Plane_PlaneEq(&planeEq, vtx1, vtx2, vtx3);
  float dist = Vec_Dot(ptEnd, &planeEq.normal);
  if (dist != 0.0f) {
    float vDist = -(DistancePointToPlane(ptStart, &planeEq) / dist);
    *distanceOut = vDist;
    auxVec_AddMulR32(ptStart, ptEnd, vDist, &intersectPt);
    return vecutil_point_on_poly(&intersectPt, vtx1, vtx2, vtx3, &planeEq);
  }

  return false;
}

// AUTOGEN
void Collide_FilterBullets(HITDATA_tag **hitList, ushort flags);

// AUTOGEN
float Collide_GetDamageNObjects(HITDATA_tag *hitDatas, obj_tag **objectList, ushort *objectListCountOut, ushort maxObjectsInList);

// {

//   HITDATA_tag *hitData = *hitList;

//   if(hitData == NULL)
//     return;

//   bool needsSort = false;

//   for(; hitData != NULL; hitData = hitData->next) {
    
//     obj_tag *hitter = hitData->hitter;

//     if(hitter == NULL)
//         continue;
    
//     if(hitter->objectType == OBJECTTYPE_BULLET) {
//       // TODO: Some extra condition based on the flags
//       BU_tag* bullet = (BU_tag*)hitter->extraObjectData;

//       if(bullet->wpnDef->someFlags & flags) {
//         hitData->someSortField = 1e+08;
//         needsSort = true;  
//       }

//     }
    
//   }




//   if(needsSort)
//     Collide_Sort(hitData);

// }