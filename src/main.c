#include <zephyr/sys/printk.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/gpio.h>

#include "radio.h"
#include "bluetooth.h"
#include "timeslot.h"

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

#define STATUS_THREAD_STACKSIZE 256
#define STATUS_THREAD_PRIORITY 7

static struct radio_test_config test_config;

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

void status_thread(void)
{
	if (!gpio_is_ready_dt(&led0) || !gpio_is_ready_dt(&led1))
	{
		printk("Could not enable LEDs\n");
		return 0;
	}

	if (gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE) < 0 || gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE) < 0)
	{
		printk("Could not configure LEDs\n");
		return 0;
	}

	gpio_pin_set_dt(&led0, 0);
	gpio_pin_set_dt(&led1, 0);

	uint8_t ctr = 0;
	uint8_t heartbeat_ctr = 0;

	while (true)
	{
		if (radio_is_active_counter > 0 && ctr >= 50)
		{
			gpio_pin_toggle_dt(&led0);
			ctr = 0;
		}

		if (radio_is_active_counter > 0)
		{
			ctr++;
			radio_is_active_counter -= 10;
		}

		if (radio_is_active_counter == 10)
		{
			gpio_pin_set_dt(&led0, 0);
		}

		heartbeat_ctr++;
		if (heartbeat_ctr >= 80)
		{
			gpio_pin_toggle_dt(&led1);
			heartbeat_ctr = 0;
		}

		k_msleep(10);
	}
}

static void start_tx(void)
{
	printk("Starting TX\n");
	memset(&test_config, 0, sizeof(test_config));
	test_config.type = MODULATED_TX;
	test_config.mode = NRF_RADIO_MODE_BLE_LR125KBIT;
	test_config.params.modulated_tx.txpower = NRF_RADIO_TXPOWER_POS8DBM;
	test_config.params.modulated_tx.channel = 0;
	test_config.params.modulated_tx.pattern = TRANSMIT_PATTERN_11110000;
	radio_test_start(&test_config);
}

static void start_rx(void)
{
	printk("Starting RX\n");
	memset(&test_config, 0, sizeof(test_config));
	test_config.type = RX;
	test_config.mode = NRF_RADIO_MODE_BLE_LR125KBIT;
	test_config.params.rx.channel = 0;
	test_config.params.rx.pattern = TRANSMIT_PATTERN_11110000;
	radio_test_start(&test_config);
	return 0;
}

int main(void)
{
	int err;

	printk("Starting High Frequency Clock\n");
	clock_init();

	bluetooth_init();

	// start_radio_timeslot(false);

	return 0;
}

K_THREAD_DEFINE(status_thread_id, STATUS_THREAD_STACKSIZE, status_thread, NULL, NULL, NULL,
				STATUS_THREAD_PRIORITY, 0, 0);