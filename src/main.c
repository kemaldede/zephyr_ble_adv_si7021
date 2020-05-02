#include <zephyr/types.h>
#include <stddef.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <drivers/sensor.h>
#include <drivers/gpio.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>

#include <device.h>
#include <drivers/i2c.h>

#include "logging/log.h"

#define DEVICE_NAME "SmartSensor"
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)


static struct device* dev_si7021;

static u8_t bTNotify;
static u8_t bHNotify;

static void T_ccc_cfg_changed(const struct bt_gatt_attr *attr, u16_t value)
{
	bTNotify = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

static void H_ccc_cfg_changed(const struct bt_gatt_attr *attr, u16_t value)
{
	bHNotify = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}
// read temperature
static s32_t T_vals[] = {0, 0};
static ssize_t read_T(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, u16_t len, u16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 sizeof(T_vals));
}	

// read humidity
static s32_t H_vals[] = {0, 0};
static ssize_t read_H(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, u16_t len, u16_t offset)
{
	const char *value = attr->user_data;

	return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
				 sizeof(H_vals));
}	


BT_GATT_SERVICE_DEFINE(th_svc,
		BT_GATT_PRIMARY_SERVICE(BT_UUID_ESS),
		BT_GATT_CHARACTERISTIC(BT_UUID_TEMPERATURE, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ, read_T, NULL,T_vals),
		BT_GATT_CCC(T_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE ),
		BT_GATT_CHARACTERISTIC(BT_UUID_HUMIDITY, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_READ, read_H, NULL,H_vals),
		BT_GATT_CCC(H_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE ),
);


static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_SHORTENED, DEVICE_NAME, DEVICE_NAME_LEN),
};



static void connected(struct bt_conn *conn, u8_t err)
{
	if (err) {
		printk("Connection failed (err %u)\n", err);
	} else {
		printk("Connected\n");
	}
}

static void disconnected(struct bt_conn *conn, u8_t reason)
{
	printk("Disconnected (reason %u)\n", reason);
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static void bt_ready(int err)
{
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}


void update_sensor_data()
{

    // get sensor data
    struct sensor_value temp, humidity;

    sensor_sample_fetch(dev_si7021);
    sensor_channel_get(dev_si7021, SENSOR_CHAN_AMBIENT_TEMP, &temp);	
    sensor_channel_get(dev_si7021, SENSOR_CHAN_HUMIDITY, &humidity);

    printk("temp: %d.%06d; humidity: %d.%06d\n",temp.val1, temp.val2,humidity.val1, humidity.val2);

    T_vals[0] = temp.val1;
    T_vals[1] = temp.val2;
    H_vals[0] = humidity.val1;
    H_vals[1] = humidity.val2;
}



void main(void)
{
	int err;
	
	dev_si7021 = device_get_binding("SI7006");

	if (dev_si7021 == NULL) {
		printk("No device found; did initialization fail?\n");
		return;
	}

	k_sleep(500);
	update_sensor_data();

    // set up BLE
	err = bt_enable(bt_ready);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_conn_cb_register(&conn_callbacks);
	
	while (1) {
		k_sleep(2*MSEC_PER_SEC);

		// update 
		update_sensor_data();
		
		// notify 
		bt_gatt_notify(NULL, &th_svc.attrs[2], T_vals, sizeof(T_vals));
        bt_gatt_notify(NULL, &th_svc.attrs[4], H_vals, sizeof(H_vals));
	}

}

