#include <zephyr/sys/printk.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/gpio.h>

#include "radio.h"
#include "bluetooth.h"
#include "flash.h"

#define STATUS_THREAD_STACKSIZE 256
#define STATUS_THREAD_PRIORITY 7

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

static void clock_init(void)
{
	printk("Starting High Frequency Clock\n");
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

void status_led_thread(void)
{
	if (!gpio_is_ready_dt(&led0) || !gpio_is_ready_dt(&led1))
	{
		printk("Could not enable status LEDs\n");
		return;
	}

	if (gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE) < 0 || gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE) < 0)
	{
		printk("Could not configure status LEDs\n");
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
	// uint8_t buf[32];

	clock_init();
	// bluetooth_init();

	// --------

	const uint8_t expected[] = {0x55, 0xaa, 0x66, 0x99};
	const size_t len = sizeof(expected);

	uint8_t buf[11];
	buf[0] = 0xaa;
	buf[1] = 0xbb;
	buf[2] = 0xcc;
	buf[3] = 0xdd;
	buf[4] = 0xee;
	int rc;

	const struct device *flash_dev = DEVICE_DT_GET(DT_ALIAS(spi_flash0));

	if (!device_is_ready(flash_dev))
	{
		printk("%s: device not ready.\n", flash_dev->name);
		return 0;
	}

	rc = flash_erase(flash_dev, 0,
					 4096);
	if (rc != 0)
	{
		printk("main: Flash erase failed! %d\n", rc);
	}
	else
	{
		printk("main: Flash erase succeeded!\n");
	}

	rc = fs_skip_to_end(flash_dev);
	if (rc != 0)
	{
		printk("main: skip failed\n");
		return 0;
	}
	k_msleep(1);

	rc = fs_write_packet(flash_dev, buf, 11);
	if (rc != 0)
	{
		printk("main: write 1 failed - %x\n", rc);
	}
	buf[0] = 0xaa;
	buf[1] = 0xcc;
	buf[2] = 0xee;
	buf[3] = 0xbb;
	buf[4] = 0xdd;
	rc = fs_write_packet(flash_dev, buf, 11);
	if (rc != 0)
	{
		printk("write 2 failed\n");
	}

	flash_read_t res = fs_read(flash_dev, buf, 1);
	if (res.res != FS_SUCCESS)
	{
		printk("main: read 1 failed : %u\n", res.res);
	}
	printk("main: %x %x %x %x %x\n", buf[0], buf[1], buf[2], buf[3], buf[4]);
	res = fs_read(flash_dev, buf, 2);
	if (res.res != FS_SUCCESS)
	{
		printk("main: read 2 failed : %u\n", res.res);
	}
	printk("main: %x %x %x %x %x\n", buf[0], buf[1], buf[2], buf[3], buf[4]);

	// printf("\nTest 2: Flash write\n");

	// printf("Attempting to write %zu bytes\n", len);
	// rc = flash_write(flash_dev, 0, expected, len);
	// if (rc != 0)
	// {
	// 	printf("Flash write failed! %d\n", rc);
	// 	return;
	// }

	// memset(buf, 0, len);
	// rc = flash_read(flash_dev, 0, buf, 32);
	// if (rc != 0)
	// {
	// 	printf("Flash read failed! %d\n", rc);
	// 	return;
	// }

	// if (memcmp(expected, buf, len) == 0)
	// {
	// 	printf("Data read matches data written. Good!!\n");
	// }
	// else
	// {
	// 	const uint8_t *wp = expected;
	// 	const uint8_t *rp = buf;
	// 	const uint8_t *rpe = rp + len;

	// 	printf("Data read does not match data written!!\n");
	// 	while (rp < rpe)
	// 	{
	// 		printf("%08x wrote %02x read %02x %s\n",
	// 			   (uint32_t)(0 + (rp - buf)),
	// 			   *wp, *rp, (*rp == *wp) ? "match" : "MISMATCH");
	// 		++rp;
	// 		++wp;
	// 	}
	// }

	// for (uint8_t i = 0; i < 32; i++)
	// {
	// 	printk("%x ", buf[i]);
	// }

	printk("main: Init done\n");
	return 0;
}

K_THREAD_DEFINE(status_led_thread_id, STATUS_THREAD_STACKSIZE, status_led_thread, NULL, NULL, NULL,
				STATUS_THREAD_PRIORITY, 0, 0);