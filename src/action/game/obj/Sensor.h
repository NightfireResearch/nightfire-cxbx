#ifndef SENSOR_H_
#define SENSOR_H_

#include "../../actionhelpers.h"

typedef struct SENSOR SENSOR;

void Sensor_Init(void);
obj_tag* Sensor_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
ushort Sensor_SetBeam(obj_tag *targetObj, obj_tag *sensorObj, SENSOR *sensor);
bool Sensor_InCone(obj_tag *targetObj, obj_tag *sensorObj, SENSOR *sensor);
void Sensor_Update(obj_tag *sensorObj);


#endif // SENSOR_H_