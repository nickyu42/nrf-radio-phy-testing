#include <zephyr/sys/printk.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/gpio.h>

// #include <zephyr/bluetooth/bluetooth.h>
// #include <zephyr/bluetooth/conn.h>
// #include <zephyr/bluetooth/gatt.h>
// #include <zephyr/bluetooth/uuid.h>
// #include <zephyr/bluetooth/services/bas.h>
// #include <zephyr/bluetooth/services/hrs.h>

// #include "service.h"

// #include "bluetooth.h"

// #define DEVICE_NAME CONFIG_BT_DEVICE_NAME
// #define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

// static void start_advertising_coded(struct k_work *work);
// // static void notify_work_handler(struct k_work *work);

// static K_WORK_DEFINE(start_advertising_worker, start_advertising_coded);
// // static K_WORK_DELAYABLE_DEFINE(notify_work, notify_work_handler);

#include "radio.h"

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

#define STACKSIZE 256
#define PRIORITY 7

static void clock_init(void)
{
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr)
	{
		printk("Unable to get the Clock manager\n");
		return;
	}

	sys_notify_init_spinwait(&clk_cli.notify);

	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0)
	{
		printk("Clock request failed: %d\n", err);
		return;
	}

	do
	{
		err = sys_notify_fetch_result(&clk_cli.notify, &res);
		if (!err && res)
		{
			printk("Clock could not be started: %d\n", res);
			return;
		}
	} while (err);

	printk("Clock has started\n");
}

// static struct bt_le_ext_adv *adv;

// static const struct bt_data ad[] = {
// 	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
// 	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
// 				  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
// 				  BT_UUID_16_ENCODE(BT_UUID_DIS_VAL)),
// 	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN)};

// static void connected(struct bt_conn *conn, uint8_t conn_err)
// {
// 	int err;
// 	struct bt_conn_info info;
// 	char addr[BT_ADDR_LE_STR_LEN];

// 	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

// 	if (conn_err)
// 	{
// 		printk("Connection failed (err %d)\n", conn_err);
// 		return;
// 	}

// 	err = bt_conn_get_info(conn, &info);
// 	if (err)
// 	{
// 		printk("Failed to get connection info (err %d)\n", err);
// 	}
// 	else
// 	{
// 		printk("Connected: %s\n", addr);
// 	}

// 	// dk_set_led_on(CON_STATUS_LED);
// }

// static void disconnected(struct bt_conn *conn, uint8_t reason)
// {
// 	printk("Disconnected (reason 0x%02x)\n", reason);

// 	k_work_submit(&start_advertising_worker);

// 	// dk_set_led_off(CON_STATUS_LED);
// }

// BT_CONN_CB_DEFINE(conn_callbacks) = {
// 	.connected = connected,
// 	.disconnected = disconnected,
// };

// static int create_advertising_coded(void)
// {
// 	int err;
// 	struct bt_le_adv_param param =
// 		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONNECTABLE |
// 								 BT_LE_ADV_OPT_EXT_ADV |
// 								 BT_LE_ADV_OPT_CODED,
// 							 BT_GAP_ADV_FAST_INT_MIN_2,
// 							 BT_GAP_ADV_FAST_INT_MAX_2,
// 							 NULL);

// 	err = bt_le_ext_adv_create(&param, NULL, &adv);
// 	if (err)
// 	{
// 		printk("Failed to create advertiser set (err %d)\n", err);
// 		return err;
// 	}

// 	printk("Created adv: %p\n", adv);

// 	err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
// 	if (err)
// 	{
// 		printk("Failed to set advertising data (err %d)\n", err);
// 		return err;
// 	}

// 	return 0;
// }

// static void start_advertising_coded(struct k_work *work)
// {
// 	int err;

// 	err = bt_le_ext_adv_start(adv, NULL);
// 	if (err)
// 	{
// 		printk("Failed to start advertising set (err %d)\n", err);
// 		return;
// 	}

// 	printk("Advertiser %p set started\n", adv);
// }
void status_led(void)
{
	if (!gpio_is_ready_dt(&led0) || !gpio_is_ready_dt(&led1))
	{
		printk("Could not enable LED GPIO\n");
		return 0;
	}

	if (gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE) < 0 || gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE) < 0)
	{
		printk("Could not configure LED GPIO\n");
		return 0;
	}

	gpio_pin_set_dt(&led0, 0);

	uint8_t ctr = 0;
	uint8_t hb_ctr = 0;

	while (true)
	{
		if (is_active_lifetime > 0 && ctr >= 50)
		{
			gpio_pin_toggle_dt(&led0);
			ctr = 0;
		}

		if (is_active_lifetime > 0)
		{
			ctr++;
			is_active_lifetime -= 10;
		}

		if (is_active_lifetime == 10)
		{
			gpio_pin_set_dt(&led0, 0);
		}

		hb_ctr++;
		if (hb_ctr >= 50)
		{
			gpio_pin_toggle_dt(&led1);
			hb_ctr = 0;
		}

		k_msleep(10);
	}
}

void hb(void)
{
	// if (!gpio_is_ready_dt(&led1))
	// {
	// 	printk("Could not enable LED GPIO\n");
	// 	return 0;
	// }

	// if (gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE) < 0)
	// {
	// 	printk("Could not configure LED GPIO\n");
	// 	return 0;
	// }

	while (true)
	{
		gpio_pin_toggle_dt(&led1);
		k_msleep(500);
	}
}

int main(void)
{
	int err;

	printk("Starting High Frequency Clock\n");
	clock_init();

	// printk("Enabling bluetooth");
	// err = bt_enable(NULL);
	// if (err)
	// {
	// 	printk("Bluetooth init failed (err %d)\n", err);
	// 	return 0;
	// }
	// printk("Bluetooth initialized\n");

	// my_service_init();

	// err = create_advertising_coded();
	// if (err)
	// {
	// 	printk("Advertising failed to create (err %d)\n", err);
	// 	return 0;
	// }

	// k_work_submit(&start_advertising_worker);
	// k_work_schedule(&notify_work, K_NO_WAIT);

	// err = bluetooth_init();
	// if (err)
	// {
	// 	printk("Main: bluetooth init failed (err %d)\n", err);
	// 	return 0;
	// }

	return 0;
}

K_THREAD_DEFINE(status_led_id, 256, status_led, NULL, NULL, NULL,
				PRIORITY, 0, 0);
// K_THREAD_DEFINE(hb_id, 512, hb, NULL, NULL, NULL,
// 				8, 0, 0);
