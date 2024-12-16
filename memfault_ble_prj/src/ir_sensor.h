#ifndef IR_SENSOR_H
#define IR_SENSOR_H

#include <zephyr/drivers/gpio.h>

#define IR_SENSOR1_PIN 8  // P1.08
#define IR_SENSOR2_PIN 9  // P1.09

int ir_sensor_init(void);
bool ir_sensor_read(uint8_t sensor_pin);
void ir_sensor_update_bee_count(void);
int ir_sensor_get_bee_count(void);

#endif // IR_SENSOR_H
