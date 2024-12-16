#include "ir_sensor.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>

LOG_MODULE_REGISTER(ir_sensor, LOG_LEVEL_INF);

#define GPIO_NODE DT_NODELABEL(gpio1)

static const struct device *gpio_dev;
static int bee_count = 0;
static bool sensor1_last_state = false;
static bool sensor2_last_state = false;

int ir_sensor_init(void)
{
    int ret;

    gpio_dev = DEVICE_DT_GET(GPIO_NODE);
    if (!device_is_ready(gpio_dev)) {
        LOG_ERR("GPIO device not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure(gpio_dev, IR_SENSOR1_PIN, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure IR sensor 1 GPIO pin");
        return ret;
    }

    ret = gpio_pin_configure(gpio_dev, IR_SENSOR2_PIN, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure IR sensor 2 GPIO pin");
        return ret;
    }

    LOG_INF("IR sensors initialized");
    return 0;
}

bool ir_sensor_read(uint8_t sensor_pin)
{
    return gpio_pin_get(gpio_dev, sensor_pin) == 0;  // Assuming active-low sensors
}

void ir_sensor_update_bee_count(void)
{
    bool sensor1_current_state = ir_sensor_read(IR_SENSOR1_PIN);
    bool sensor2_current_state = ir_sensor_read(IR_SENSOR2_PIN);
    LOG_INF("IR 1 State: %x", sensor1_current_state);
    LOG_INF("IR 2 State: %x", sensor2_current_state);

    LOG_INF("Bee Count: %d", bee_count);

    // Bee entering
    if (sensor1_current_state && !sensor1_last_state && !sensor2_current_state) {
        bee_count++;
        // LOG_INF("xxxxxxBee entered. Count: %d", bee_count);
        LOG_INF("Bee entered. Count: %d", bee_count);
    }
    // Bee exiting
    else if (sensor2_current_state && !sensor2_last_state && !sensor1_current_state) {
        bee_count--;
        // LOG_INF("yyyyyyBee entered. Count: %d", bee_count);
        LOG_INF("Bee exited. Count: %d", bee_count);
    }

    sensor1_last_state = sensor1_current_state;
    sensor2_last_state = sensor2_current_state;
}

int ir_sensor_get_bee_count(void)
{
    return bee_count;
}

