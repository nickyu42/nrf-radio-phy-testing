/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdlib.h>

#include <errno.h>
#include <zephyr/init.h>
#include <zephyr/shell/shell.h>
#include <zephyr/types.h>
#include <hal/nrf_power.h>

#include "radio.h"

/* Radio parameter configuration. */
static struct radio_param_config
{
	/** Radio transmission pattern. */
	enum transmit_pattern tx_pattern;

	/** Radio mode. Data rate and modulation. */
	nrf_radio_mode_t mode;

	/** Radio output power. */
	uint8_t txpower;

	/** Radio start channel (frequency). */
	uint8_t channel_start;

	/** Radio end channel (frequency). */
	uint8_t channel_end;

	/** Delay time in milliseconds. */
	uint32_t delay_ms;

	/** Duty cycle. */
	uint32_t duty_cycle;
} config = {
	.tx_pattern = TRANSMIT_PATTERN_11110000,
	.mode = NRF_RADIO_MODE_BLE_1MBIT,
	// .mode = NRF_RADIO_MODE_BLE_LR125KBIT,
	.txpower = RADIO_TXPOWER_TXPOWER_Pos8dBm,
	.channel_start = 0,
	.channel_end = 80,
	.delay_ms = 10,
	.duty_cycle = 50,
};

/* Radio test configuration. */
static struct radio_test_config test_config;

static int cmd_cancel(const struct shell *shell, size_t argc, char **argv)
{
	radio_test_cancel();
	return 0;
}

static int cmd_tx_carrier_start(const struct shell *shell, size_t argc,
								char **argv)
{
	// ieee_channel_check(shell, config.channel_start);
	memset(&test_config, 0, sizeof(test_config));
	test_config.type = UNMODULATED_TX;
	test_config.mode = config.mode;
	test_config.params.unmodulated_tx.txpower = config.txpower;
	test_config.params.unmodulated_tx.channel = config.channel_start;
	radio_test_start(&test_config);

	shell_print(shell, "Start the TX carrier");
	return 0;
}

static int cmd_tx_start(const struct shell *shell,
						size_t argc,
						char **argv)
{

	if (argc > 2)
	{
		shell_error(shell, "%s: bad parameters count.", argv[0]);
		return -EINVAL;
	}

	memset(&test_config, 0, sizeof(test_config));
	test_config.type = MODULATED_TX;
	test_config.mode = config.mode;
	test_config.params.modulated_tx.txpower = config.txpower;
	test_config.params.modulated_tx.channel = config.channel_start;
	test_config.params.modulated_tx.pattern = config.tx_pattern;

	if (argc == 2)
	{
		test_config.params.modulated_tx.packets_num = atoi(argv[1]);
	}

	radio_test_start(&test_config);

	shell_print(shell, "Start the modulated TX carrier");
	return 0;
}

static int cmd_rx_start(const struct shell *shell, size_t argc, char **argv)
{
	config.txpower = RADIO_TXPOWER_TXPOWER_Pos8dBm;
	// ieee_channel_check(shell, config.channel_start);
	memset(&test_config, 0, sizeof(test_config));
	test_config.type = RX;
	test_config.mode = config.mode;
	test_config.params.rx.channel = config.channel_start;
	test_config.params.rx.pattern = config.tx_pattern;
	radio_test_start(&test_config);
	return 0;
}

static int cmd_print_payload(const struct shell *shell, size_t argc,
							 char **argv)
{
	struct radio_rx_stats rx_stats;

	memset(&rx_stats, 0, sizeof(rx_stats));

	radio_rx_stats_get(&rx_stats);

	shell_print(shell, "Received payload:");
	shell_hexdump(shell, rx_stats.last_packet.buf,
				  rx_stats.last_packet.len);
	shell_print(shell, "Number of packets: %d", rx_stats.packet_cnt);

	return 0;
}

SHELL_CMD_REGISTER(cancel, NULL, "Cancel the sweep or the carrier",
				   cmd_cancel);
SHELL_CMD_REGISTER(start_tx, NULL,
				   "Start the modulated TX carrier",
				   cmd_tx_start);
SHELL_CMD_REGISTER(start_rx, NULL, "Start RX", cmd_rx_start);
SHELL_CMD_REGISTER(print_rx, NULL, "Print RX payload", cmd_print_payload);

static int radio_cmd_init(void)
{
	return radio_test_init(&test_config);
}

SYS_INIT(radio_cmd_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
