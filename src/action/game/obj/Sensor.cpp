#include "Sensor.h"

// Scanning cameras with conical beams, eg in evil base and office, but NOT the lasers which trigger the alarm - presumably some kind of Trigger instead

typedef struct SENSOR {
    LLNODE_tag llnode;
    char unknown[100-8];
} SENSOR;

static_assert(sizeof(SENSOR) == 0x64, "Wrong size for SENSOR");

typedef struct {
    ObjectCreationData_Basic baseData;
    HASHCODE beamHashcode;
    char unknown1[0x48-4-0x2c];
    HASHCODE bodyHashcode;
} Create_Sensor_Params;

static_assert(offsetof(Create_Sensor_Params, beamHashcode) == 0x2c, "Bad offset of beamHashcode");
static_assert(offsetof(Create_Sensor_Params, bodyHashcode) == 0x48, "Bad offset of bodyHashcode");


// WIP
obj_tag* Sensor_Create(_VECTOR *pos,_VECTOR *rot,level_tag *lvl,celglist_tag *celgl) {

    Create_Sensor_Params* createParams = (Create_Sensor_Params*)lvl;

    obj_tag* obj = control_create_object(sizeof(SENSOR), pos, rot, NULL);

    if(obj == NULL)
        return NULL;

    SENSOR* sensor = (SENSOR*)obj->extraObjectData;

    // TODO: Load extra data for sensor object

    if(createParams->beamHashcode != 0) {

        printf("SENSOR: Beam is 0x%08x\n", createParams->beamHashcode); // eg 0x02000360: Camera Beam

    }

    if(createParams->bodyHashcode != 0) {
        
        printf("SENSOR: Body is 0x%08x\n", createParams->bodyHashcode); // eg 0x02000652: Camera Sensor Hub

    }

    return obj;

}


