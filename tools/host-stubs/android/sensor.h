// Host-side stub of NDK <android/sensor.h> for tools/syntax-check.sh only.
// NOT part of the device build — the real NDK header is used there. Only the
// declarations the engine's hooks reference are needed to type-check.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ASensorManager ASensorManager;
typedef struct ASensor ASensor;
typedef const ASensor **ASensorList;

int ASensorManager_getSensorList(ASensorManager *manager, ASensorList *list);
const ASensor *ASensorManager_getDefaultSensor(ASensorManager *manager, int type);
const char *ASensor_getName(const ASensor *sensor);
const char *ASensor_getVendor(const ASensor *sensor);
const char *ASensor_getStringType(const ASensor *sensor);
int ASensor_getType(const ASensor *sensor);

#ifdef __cplusplus
}
#endif
