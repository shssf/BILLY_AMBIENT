#ifndef LIGHT_SENSOR_SUPPORT_H
#define LIGHT_SENSOR_SUPPORT_H

#ifdef __cplusplus
extern "C" {
#endif

void light_sensor_init();
void light_sensor_dump(void);

#ifdef __cplusplus
}
#endif

bool light_sensor_is_light();
int light_sensor_get_value();

#endif // LIGHT_SENSOR_SUPPORT_H 
