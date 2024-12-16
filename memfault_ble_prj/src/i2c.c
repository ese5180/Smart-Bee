#include "i2c.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <zephyr/drivers/sensor.h>

LOG_MODULE_REGISTER(i2c_module, LOG_LEVEL_INF);

#define I2C_NODE DT_NODELABEL(i2c1)

static const struct device *i2c_dev;

#define BMP388_LABEL "BMP388"
#define NAU7802_ADDR 0x2A

#define NAU7802_PU_CTRL 0x00
#define NAU7802_ADC_REG 0x12

static const struct device *i2c_dev;


static void bmp388_init(void)
{
    const struct device *bmp388 = DEVICE_DT_GET_ANY(bosch_bmp388);

    if (!device_is_ready(bmp388)) {
        printk("BMP388 device not ready\n");
        return;
    }

    printk("BMP388 device initialized\n");
}

int i2c_init(void)
{
    i2c_dev = DEVICE_DT_GET(I2C_NODE);
    if (!device_is_ready(i2c_dev)) {
        LOG_ERR("I2C device not ready");
        return -1;
    }

    bmp388_init();

    // Power up the NAU7802
    uint8_t reg = NAU7802_PU_CTRL;
    uint8_t pu_ctrl = 0x06;
    if (i2c_write(i2c_dev, &pu_ctrl, 1, NAU7802_ADDR) != 0) {
        LOG_ERR("Failed to initialize NAU7802");
        return -1;
    }

    printk("NAU7802 initialized\n");

    return 0;
}

static void bmp388_read(int32_t *temp_value, int32_t *pressure_value)
{
    const struct device *bmp388 = DEVICE_DT_GET_ANY(bosch_bmp388);
    struct sensor_value temp, press;

    sensor_sample_fetch(bmp388);
    sensor_channel_get(bmp388, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    sensor_channel_get(bmp388, SENSOR_CHAN_PRESS, &press);

    printk("Temperature: %.2f °C\n", sensor_value_to_double(&temp));
    printk("Pressure: %.2f hPa\n", sensor_value_to_double(&press) / 100);

    *temp_value = &temp;
    *pressure_value = &press;
}

static int32_t nau7802_read(void)
{
    uint8_t reg = NAU7802_ADC_REG;
    uint8_t data[3];
    int32_t result;

    i2c_write_read(i2c_dev, NAU7802_ADDR, &reg, 1, data, 3);

    result = (data[0] << 16) | (data[1] << 8) | data[2];
    if (result & 0x800000) {
        result |= 0xFF000000;  // Sign extend
    }

    return result;
}

void read_sensor_data(int32_t *temp_value, int32_t *pressure_value, int32_t *load_value)
{
    bmp388_read(temp_value, pressure_value);
    load_value = nau7802_read();
}