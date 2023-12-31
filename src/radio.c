/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "radio.h"

#include <hal/nrf_power.h>

#include <nrfx_timer.h>
#include <zephyr/kernel.h>

#include <helpers/nrfx_gppi.h>

#include <zephyr/drivers/gpio.h>

#include <string.h>

#include "flash.h"

uint32_t radio_is_active_counter = 0;

/* Length on air of the LENGTH field. */
#define RADIO_LENGTH_LENGTH_FIELD (8UL)

#define RADIO_TEST_TIMER_INSTANCE 0

#define RADIO_TEST_EGU NRF_EGU0
#define RADIO_TEST_EGU_EVENT NRF_EGU_EVENT_TRIGGERED0
#define RADIO_TEST_EGU_TASK NRF_EGU_TASK_TRIGGER0

/* Frequency calculation for a given channel in the IEEE 802.15.4 radio
 * mode.
 */
#define IEEE_FREQ_CALC(_channel) (IEEE_DEFAULT_FREQ +  \
								  (IEEE_DEFAULT_FREQ * \
								   ((_channel)-IEEE_MIN_CHANNEL)))
/* Frequency calculation for a given channel. */
#define CHAN_TO_FREQ(_channel) (2400 + _channel)

/* Buffer for the radio TX packet */
static uint8_t tx_packet[RADIO_MAX_PAYLOAD_LEN];
/* Buffer for the radio RX packet. */
static uint8_t rx_packet[RADIO_MAX_PAYLOAD_LEN];
/* Number of transmitted packets. */
static uint32_t tx_packet_cnt;
/* Number of received packets with valid CRC. */
static uint32_t rx_packet_cnt;
/* Most recent measured RSSI value */
static uint8_t rssi;

/* Radio current channel (frequency). */
static uint8_t current_channel;

/* Timer used for channel sweeps and tx with duty cycle. */
static const nrfx_timer_t timer = NRFX_TIMER_INSTANCE(RADIO_TEST_TIMER_INSTANCE);

/* Total payload size */
static uint16_t total_payload_size;

/* PPI channel for starting radio */
static uint8_t ppi_radio_start;

/* Packet size to use */
uint8_t packet_size = RADIO_MAX_PAYLOAD_LEN - 1;

/* RX packets statistics */
uint32_t radio_total_rssi;
uint32_t radio_packets_received;
uint32_t radio_total_crcok;
bool radio_has_received;
// For logging
static uint8_t rx_log_buf[RADIO_MAX_PAYLOAD_LEN + 4 * 4 + 2];
bool radio_logging_active = false;

/* TX packets statistics */
uint32_t radio_packets_sent;

static void radio_power_set(nrf_radio_mode_t mode, uint8_t channel, int8_t power)
{
	int8_t radio_power = power;

	ARG_UNUSED(mode);
	ARG_UNUSED(channel);

	nrf_radio_txpower_set(NRF_RADIO, (nrf_radio_txpower_t)radio_power);
}

static void radio_channel_set(nrf_radio_mode_t mode, uint8_t channel)
{
	uint16_t frequency;

	frequency = CHAN_TO_FREQ(channel);
	nrf_radio_frequency_set(NRF_RADIO, frequency);
}

static void radio_config(nrf_radio_mode_t mode, enum transmit_pattern pattern)
{
	nrf_radio_packet_conf_t packet_conf;

	/* Set fast ramp-up time. */
	nrf_radio_modecnf0_set(NRF_RADIO, true, RADIO_MODECNF0_DTX_Center);
	/* Disable CRC. */
	nrf_radio_crc_configure(NRF_RADIO, RADIO_CRCCNF_LEN_Disabled,
							NRF_RADIO_CRC_ADDR_INCLUDE, 0);

	/* Set the device address 0 to use when transmitting. */
	nrf_radio_txaddress_set(NRF_RADIO, 0);
	/* Enable the device address 0 to use to select which addresses to
	 * receive
	 */
	nrf_radio_rxaddresses_set(NRF_RADIO, 1);
	nrf_radio_prefix0_set(NRF_RADIO, 0x6A);
	nrf_radio_base0_set(NRF_RADIO, 0x58FE811B);

	/* Packet configuration:
	 * payload length size = 8 bits,
	 * 0-byte static length, max 255-byte payload,
	 * 4-byte base address length (5-byte full address length),
	 * Bit 24: 1 Big endian,
	 * Bit 25: 1 Whitening enabled.
	 */
	memset(&packet_conf, 0, sizeof(packet_conf));
	packet_conf.lflen = RADIO_LENGTH_LENGTH_FIELD;
	packet_conf.maxlen = packet_size;
	// packet_conf.maxlen = (sizeof(tx_packet) - 1);
	packet_conf.statlen = 0;
	packet_conf.balen = 4;
	packet_conf.big_endian = true;
	packet_conf.whiteen = true;

	/* Packet configuration:
	 * S1 size = 0 bits,
	 * S0 size = 0 bytes,
	 * 10 bytes preamble.
	 */
	packet_conf.plen = NRF_RADIO_PREAMBLE_LENGTH_LONG_RANGE;
	packet_conf.cilen = 2;
	packet_conf.termlen = 3;
	packet_conf.big_endian = false;
	packet_conf.balen = 3;

	/* Set CRC length; CRC calculation does not include the address
	 * field.
	 */
	nrf_radio_crc_configure(NRF_RADIO, RADIO_CRCCNF_LEN_Three,
							NRF_RADIO_CRC_ADDR_SKIP, 0);

	/* preamble, address (BALEN + PREFIX), lflen, code indicator, TERM, payload, CRC */
	total_payload_size = 10 + (packet_conf.balen + 1) + 1 + packet_conf.cilen +
						 packet_conf.termlen + packet_conf.maxlen + RADIO_CRCCNF_LEN_Three;

	nrf_radio_packet_configure(NRF_RADIO, &packet_conf);
}

static void radio_disable(void)
{
	nrf_radio_shorts_set(NRF_RADIO, 0);
	nrf_radio_int_disable(NRF_RADIO, ~0);
	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);

	nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_DISABLE);
	while (!nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_DISABLED))
	{
		/* Do nothing */
	}
	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);
}

static void radio_mode_set(NRF_RADIO_Type *reg, nrf_radio_mode_t mode)
{

	nrf_radio_mode_set(reg, mode);
}

static void radio_modulated_tx_carrier(uint8_t mode, int8_t txpower, uint8_t channel,
									   enum transmit_pattern pattern)
{
	radio_disable();
	radio_config(mode, pattern);
	// tx_packet[0] = sizeof(tx_packet) - 1;
	tx_packet[0] = packet_size;
	memset(tx_packet + 1, 0xF0, sizeof(tx_packet) - 1);
	nrf_radio_packetptr_set(NRF_RADIO, tx_packet);

	if (mode == RADIO_MODE_MODE_Ble_LR125Kbit || mode == RADIO_MODE_MODE_Ble_LR500Kbit)
	{
		nrf_radio_shorts_enable(NRF_RADIO,
								NRF_RADIO_SHORT_READY_START_MASK |
									NRF_RADIO_SHORT_PHYEND_START_MASK);
	}
	else
	{
		nrf_radio_shorts_enable(NRF_RADIO,
								NRF_RADIO_SHORT_READY_START_MASK |
									NRF_RADIO_SHORT_END_START_MASK);
	}

	radio_mode_set(NRF_RADIO, mode);
	radio_power_set(mode, channel, txpower);

	radio_channel_set(mode, channel);

	tx_packet_cnt = 0;

	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_END);
	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_PHYEND);

	if (mode == RADIO_MODE_MODE_Ble_LR125Kbit || mode == RADIO_MODE_MODE_Ble_LR500Kbit)
	{
		nrf_radio_int_enable(NRF_RADIO, NRF_RADIO_INT_PHYEND_MASK);
	}
	else
	{
		nrf_radio_int_enable(NRF_RADIO, NRF_RADIO_INT_END_MASK);
	}

	nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_TXEN);
}

static void radio_rx(uint8_t mode, uint8_t channel, enum transmit_pattern pattern)
{
	radio_disable();

	radio_mode_set(NRF_RADIO, mode);
	nrf_radio_shorts_enable(NRF_RADIO,
							NRF_RADIO_SHORT_READY_START_MASK |
								NRF_RADIO_SHORT_END_START_MASK |
								NRF_RADIO_SHORT_ADDRESS_RSSISTART_MASK |
								NRF_RADIO_SHORT_DISABLED_RSSISTOP_MASK);
	nrf_radio_packetptr_set(NRF_RADIO, rx_packet);

	radio_config(mode, pattern);
	radio_channel_set(mode, channel);

	rx_packet_cnt = 0;

	nrf_radio_int_enable(NRF_RADIO,
						 NRF_RADIO_INT_CRCOK_MASK |
							 NRF_RADIO_INT_RSSIEND_MASK |
							 NRF_RADIO_INT_ADDRESS_MASK);

	nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_RXEN);
}

void radio_test_start(const struct radio_test_config *config)
{
	switch (config->type)
	{
	case MODULATED_TX:
		radio_modulated_tx_carrier(config->mode,
								   config->params.modulated_tx.txpower,
								   config->params.modulated_tx.channel,
								   config->params.modulated_tx.pattern);
		break;
	case RX:
		radio_rx(config->mode,
				 config->params.rx.channel,
				 config->params.rx.pattern);
		break;
	}
}

void radio_test_cancel(void)
{
	nrfx_timer_disable(&timer);
	nrfx_timer_clear(&timer);

	// sweep_processing = false;

	nrfx_gppi_channels_disable(BIT(ppi_radio_start));
	// nrfx_gppi_event_endpoint_clear(ppi_radio_start,
	// 							   nrf_egu_event_address_get(RADIO_TEST_EGU, RADIO_TEST_EGU_EVENT));
	nrfx_gppi_task_endpoint_clear(ppi_radio_start,
								  nrf_radio_task_address_get(NRF_RADIO, NRF_RADIO_TASK_TXEN));
	nrfx_gppi_task_endpoint_clear(ppi_radio_start,
								  nrf_radio_task_address_get(NRF_RADIO, NRF_RADIO_TASK_RXEN));
	nrfx_gppi_fork_endpoint_clear(ppi_radio_start,
								  nrf_timer_task_address_get(timer.p_reg, NRF_TIMER_TASK_START));
	nrfx_gppi_event_endpoint_clear(ppi_radio_start,
								   nrf_timer_event_address_get(timer.p_reg, NRF_TIMER_EVENT_COMPARE1));

	radio_disable();
}

void radio_rx_stats_get(struct radio_rx_stats *rx_stats)
{
	size_t size;
	size = sizeof(rx_packet);
	rx_stats->last_packet.buf = rx_packet;
	rx_stats->last_packet.len = size;
	rx_stats->packet_cnt = rx_packet_cnt;
}

static uint16_t write_rx_stats_to_buf()
{
	rx_log_buf[0] = radio_total_rssi & 0xFF;
	rx_log_buf[1] = (radio_total_rssi >> 8) & 0xFF;
	rx_log_buf[2] = (radio_total_rssi >> 16) & 0xFF;
	rx_log_buf[3] = (radio_total_rssi >> 24) & 0xFF;

	rx_log_buf[4] = radio_packets_received & 0xFF;
	rx_log_buf[5] = (radio_packets_received >> 8) & 0xFF;
	rx_log_buf[6] = (radio_packets_received >> 16) & 0xFF;
	rx_log_buf[7] = (radio_packets_received >> 24) & 0xFF;

	rx_log_buf[8] = radio_total_crcok & 0xFF;
	rx_log_buf[9] = (radio_total_crcok >> 8) & 0xFF;
	rx_log_buf[10] = (radio_total_crcok >> 16) & 0xFF;
	rx_log_buf[11] = (radio_total_crcok >> 24) & 0xFF;

	uint32_t ticks = NRF_TIMER2->CC[1];

	rx_log_buf[12] = ticks & 0xFF;
	rx_log_buf[13] = (ticks >> 8) & 0xFF;
	rx_log_buf[14] = (ticks >> 16) & 0xFF;
	rx_log_buf[15] = (ticks >> 24) & 0xFF;

	rx_log_buf[16] = rssi;
	rx_log_buf[17] = packet_size;

	memcpy(rx_log_buf + 18, rx_packet, packet_size);

	return 18 + packet_size;
}

static void write_rx_log_thread(void)
{
	while (true)
	{
		if (radio_logging_active)
		{
			uint16_t rx_stats_bytes = write_rx_stats_to_buf();

			printk("write_rx_log_thread: writing\n");
			int err = fs_write_packet(fs_flash_device, rx_log_buf, rx_stats_bytes);
			if (err != 0)
			{
				printk("write_rx_log_thread: fs_write_packet err=%d\n", err);
			}
			k_msleep(250);
		}
		else
		{
			k_msleep(100);
		}
	}
}

void radio_handler()
{
	if (nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_CRCOK))
	{
		nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_CRCOK);

		radio_is_active_counter = 1000;

		radio_total_crcok++;
	}

	if (nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_RSSIEND))
	{
		nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_RSSIEND);

		rssi = nrf_radio_rssi_sample_get(NRF_RADIO);

		// Stop after 1 sample
		nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_RSSISTOP);

		radio_total_rssi += rssi;
	}

	if (nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_ADDRESS))
	{
		nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_ADDRESS);

		if (!radio_has_received)
		{
			radio_has_received = true;

			// Store when the first packet is received into CC[0]
			NRF_TIMER2->TASKS_CAPTURE[0] = TIMER_TASKS_CAPTURE_TASKS_CAPTURE_Trigger;
		}

		NRF_TIMER2->TASKS_CAPTURE[1] = TIMER_TASKS_CAPTURE_TASKS_CAPTURE_Trigger;

		radio_packets_received++;
	}

	if (nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_END) | nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_PHYEND))
	{
		nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_END);
		nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_PHYEND);

		radio_packets_sent++;
		radio_is_active_counter = 1000;
	}
}

int radio_test_init()
{
	// Radio handler only counts sent/received packets
	// IRQ_CONNECT(RADIO_IRQn, IRQ_PRIO_LOWEST, radio_handler, NULL, 0);
	irq_connect_dynamic(RADIO_IRQn, IRQ_PRIO_LOWEST, radio_handler, NULL, 0);
	irq_enable(RADIO_IRQn);

	return 0;
}

K_THREAD_DEFINE(write_rx_log_thread_id, 1024, write_rx_log_thread, NULL, NULL, NULL,
				6, 0, 0);