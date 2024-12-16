#ifndef I2C_H
#define I2C_H

#include <zephyr/drivers/i2c.h>

#define SENSOR1_ADDR 0x77
#define SENSOR2_ADDR 0x2A

int i2c_init(void);
void read_sensor_data(int32_t *sensor1_value, int32_t *sensor2_value, int32_t *sensor3_value);
#endif // I2C_H
