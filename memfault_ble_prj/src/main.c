#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/conn.h>
#include <bluetooth/services/mds.h>
#include <zephyr/settings/settings.h>
#include <dk_buttons_and_leds.h>
#include <memfault/core/data_export.h>

#include <memfault/metrics/metrics.h>
#include <memfault/core/trace_event.h>
#include "memfault/metrics/platform/overrides.h"

#ifdef CONFIG_BLEFUNCTION
#include "ble.h"
#endif

#ifdef CONFIG_BLEFUNCTION
#include "i2c.h"
#endif

#ifdef CONFIG_BLEFUNCTION
#include "ir_sensor.h"
#endif

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define MEMFAULT_EVENT_INCLUDE_DEVICE_SERIAL 1

#define STACKSIZE 1024
#define PRIORITY 7

#define RUN_STATUS_LED DK_LED1
#define CON_STATUS_LED DK_LED2
/*Button to be controlled*/
#define USER_BUTTON DK_BTN1_MSK
/*Led to be controlled */
#define USER_LED DK_LED3
/*Interval for status led */
#define RUN_LED_BLINK_INTERVAL 1000
/*Interval at which the sensor data is sent*/
#define NOTIFY_INTERVAL 3000

LOG_MODULE_REGISTER(Sensor_Data, LOG_LEVEL_INF);

static bool app_button_state;
static int32_t temp_sensor_value = 100;
static int32_t pressure_sensor_value = 98;
static int32_t load_cell_value = 300;
static uint32_t bee_count = 0;

static struct bt_conn *mds_conn;
/*Function to simulate data*/
// static void simulate_data(void)
// {
// 	temp_sensor_value++;
// 	load_cell_value++;
// 	if (temp_sensor_value == 200) {
// 		temp_sensor_value = 100;
// 	}

// 	if (load_cell_value == 500) {
// 		load_cell_value = 500;
// 	}
// }

/*Thread to send sensor data*/
void send_data_thread(void)
{
	while(1){
		read_sensor_data(&temp_sensor_value, &pressure_sensor_value, &load_cell_value);
		ir_sensor_update_bee_count();
		bee_count = ir_sensor_get_bee_count();
		LOG_INF("**************************************");
		LOG_INF("Temperature value: %u°C", temp_sensor_value);
		LOG_INF("Pressure value: %u hPa", pressure_sensor_value);
		LOG_INF("Load Cell value: %u g", load_cell_value);
		LOG_INF("Bee Count: %u", bee_count);
		LOG_INF("**************************************");
		/* Send notification, the function sends notifications only if a client is subscribed */
		my_lbs_send_sensor_notify(temp_sensor_value);

		my_lbs_send_sensor2_notify(load_cell_value);

		MEMFAULT_METRIC_SET_UNSIGNED(hive_temperature, temp_sensor_value);
		MEMFAULT_METRIC_SET_UNSIGNED(hive_pressure, pressure_sensor_value);
		MEMFAULT_METRIC_SET_UNSIGNED(honey_collected, load_cell_value);
		MEMFAULT_METRIC_SET_UNSIGNED(bee_count, bee_count);
        memfault_metrics_heartbeat_debug_trigger();

		k_sleep(K_MSEC(NOTIFY_INTERVAL));
	}
		
}

/*Parmeters for advertising data*/
static const struct bt_le_adv_param *adv_param = BT_LE_ADV_PARAM(
	(BT_LE_ADV_OPT_CONNECTABLE |
	 BT_LE_ADV_OPT_USE_IDENTITY), /* Connectable advertising and use identity address */
	800, /* Min Advertising Interval 500ms (800*0.625ms) */
	801, /* Max Advertising Interval 500.625ms (801*0.625ms) */
	NULL); /* Set to NULL for undirected advertising */

/*Data to be sent during advertising */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_MDS_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/* Define the application callback function for controlling the LED */
static void app_led_cb(bool led_state)
{
	dk_set_led(USER_LED, led_state);
}

/* Define the application callback function for reading the state of the button */
static bool app_button_cb(void)
{
	return app_button_state;
}

/* Declare a varaible app_callbacks of type my_lbs_cb and initiate its members to the applications call back functions we developed in steps 8.2 and 9.2. */
static struct my_lbs_cb app_callbacks = {
	.led_cb = app_led_cb,
	.button_cb = app_button_cb,
};

static bool mds_access_enable(struct bt_conn *conn)
{
	if (mds_conn && (conn == mds_conn)) {
		return true;
	}

	return false;
}

static const struct bt_mds_cb mds_cb = {
	.access_enable = mds_access_enable,
};

static void button_changed(uint32_t button_state, uint32_t has_changed)
{

	uint32_t buttons = button_state & has_changed;

	if (buttons & DK_BTN1_MSK) {
		MEMFAULT_TRACE_EVENT_WITH_LOG(button_1_state_changed, "Sensor 1 Reading: %x",
					      temp_sensor_value);

		printk("Sensor 1 Reading has been tracked, button state: %x\n",
		       temp_sensor_value);

		MEMFAULT_METRIC_SET_UNSIGNED(hive_temperature, temp_sensor_value);
		memfault_metrics_heartbeat_debug_trigger();
	}

	if (has_changed & DK_BTN2_MSK) {
		MEMFAULT_TRACE_EVENT_WITH_LOG(button_2_state_changed, "Sensor 2 Reading: %x",
					      load_cell_value);

		printk("Sensor 2 Reading has been tracked, button state: %x\n",
		       load_cell_value);

		MEMFAULT_METRIC_SET_UNSIGNED(honey_collected, load_cell_value);
		memfault_metrics_heartbeat_debug_trigger();
	}

	if (buttons & DK_BTN4_MSK) {
		volatile uint32_t i;

		printk("Division by zero will now be triggered\n");
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiv-by-zero"
		i = 1 / 0;
#pragma GCC diagnostic pop
		ARG_UNUSED(i);
	}
}

/*Callback function to be called when connected */
static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed (err %u)\n", err);
		return;
	}

	printk("Connected\n");

	if (conn == mds_conn) {
		mds_conn = conn;
	}

	dk_set_led_on(CON_STATUS_LED);
}

/*Callback function to be called when disconnected*/
static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected (reason %u)\n", reason);

	dk_set_led_off(CON_STATUS_LED);

	if (conn == mds_conn) {
		mds_conn = NULL;
	}
}

/*Callback fucntion to be called when security changed*/
static void on_security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		LOG_INF("Security changed: %s level %u\n", addr, level);
		if (!mds_conn) {
			mds_conn = conn;
		}
	} else {
		LOG_INF("Security failed: %s level %u err %d\n", addr, level, err);
	}
}

/*Register the connected and disconnected callbacks*/
struct bt_conn_cb connection_callbacks = {
	/*BLE pairing successful*/
	.connected = on_connected,
	/*BLE connection diconnected*/
	.disconnected = on_disconnected,
	/*Security protocol changed */
	.security_changed = on_security_changed,
};

/*Function to print the passkey for central device*/
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Passkey for %s: %06u\n", addr, passkey);
}

/*Function to nptify when the pairing process has failed*/
static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Pairing cancelled: %s\n", addr);
}

/*Declare the authentication callback structure*/
static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};


static int init_button(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Cannot init buttons (err: %d)\n", err);
	}

	return err;
}

int main(void)
{	
	int blink_status = 0;
	int err;

	LOG_INF("Starting Application Code");

	err = dk_leds_init();
	if (err) {
		LOG_ERR("LEDs init failed (err %d)\n", err);
		return -1;
	}

	err = ir_sensor_init();
	if (err) {
		LOG_ERR("IR sensor init failed (err %d)\n", err);
		return -1;
	}

	err = init_button();
	if (err) {
		printk("Button init failed (err %d)\n", err);
		return -1;
	}

	err = i2c_init();
    if (err) {
        LOG_ERR("I2C init failed (err %d)\n", err);
        return -1;
    }

	err = bt_mds_cb_register(&mds_cb);
	if (err) {
		printk("Memfault Diagnostic service callback registration failed (err %d)\n", err);
		return 0;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)\n", err);
		return -1;
	}

	// Add a short delay, to ensure that the Bluetooth controller is fully initialized before starting advertising.
	k_sleep(K_MSEC(100));

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		LOG_INF("Failed to register authorization callbacks.\n");
		return -1;
	}
	
	bt_conn_cb_register(&connection_callbacks);

        err = my_lbs_init(&app_callbacks);
	if (err) {
		printk("Failed to init LBS (err:%d)\n", err);
		return -1;
	}

	LOG_INF("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_load();
		if (err) {
			printk("Failed to load settings (err %d)\n", err);
			return 0;
		}
	}

	err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)\nRetrying...\n", err);
		err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
		if (err) {
			LOG_ERR("Advertising failed to start (err %d)\n", err);
			return -1;
		}
	}

	LOG_INF("Advertising successfully started\n");

	while(1){
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}

/*Thread defined to send sensor data*/
K_THREAD_DEFINE(send_data_thread_id, STACKSIZE, send_data_thread, NULL, NULL, NULL, PRIORITY, 0, 0);