#include <zephyr/sys/printk.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>

#include "radio.h"
#include "bluetooth.h"
#include "flash.h"
#include "service.h"

#define STATUS_THREAD_STACKSIZE 256
#define STATUS_THREAD_PRIORITY 7

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

static void clock_init(void)
{
	printk("clock_init: Starting High Frequency Clock\n");
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr)
	{
		printk("clock_init: Unable to get the Clock manager\n");
		return;
	}

	sys_notify_init_spinwait(&clk_cli.notify);

	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0)
	{
		printk("clock_init: Clock request failed: %d\n", err);
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

	printk("clock_init: Clock has started\n");
}

void status_led_thread(void)
{
	if (!gpio_is_ready_dt(&led0) || !gpio_is_ready_dt(&led1))
	{
		printk("status_led_thread: Could not enable status LEDs\n");
		return;
	}

	if (gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE) < 0 || gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE) < 0)
	{
		printk("status_led_thread: Could not configure status LEDs\n");
		return;
	}

	// Turn LEDs off initially
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

int main(void)
{
	clock_init();
	bluetooth_init();
	fs_init();

	// fs_erase(fs_flash_device, 1);
	// uint8_t buf[11];
	// buf[0] = 0xbb;
	// buf[1] = 0xcc;
	// fs_write_packet(fs_flash_device, buf, 11);
	// fs_write_packet(fs_flash_device, buf, 11);
	// fs_write_packet(fs_flash_device, buf, 11);
	uint8_t buf[64];
	flash_read(fs_flash_device, 0, buf, 64);
	printk("%x %x %x %x\n", buf[0], buf[1], buf[2], buf[3]);
	printk("%x %x %x %x\n", buf[16 + 0], buf[16 + 1], buf[16 + 2], buf[16 + 3]);
	printk("%x %x %x %x\n", buf[32 + 0], buf[32 + 1], buf[32 + 2], buf[32 + 3]);

	printk("main: Init done\n");

	while (true)
	{
		k_sleep(K_SECONDS(2));

		if (indicate_active)
		{
			send_all_logs();
			indicate_active = false;
		}
	}
	
	return 0;
}

K_THREAD_DEFINE(status_led_thread_id, STATUS_THREAD_STACKSIZE, status_led_thread, NULL, NULL, NULL,
				STATUS_THREAD_PRIORITY, 0, 0);