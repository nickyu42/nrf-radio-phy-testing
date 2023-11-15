#include <mpsl/mpsl_lib.h>

#include "service.h"
#include "radio.h"
#include "bluetooth.h"
#include "flash.h"

#define RADIO_SERVICE 0x1E, 0x6B, 0x0B, 0xEA, 0x7A, 0x4F, 0x48, 0x2B, \
                      0x86, 0x9C, 0x76, 0x80, 0x15, 0xA5, 0x1A, 0xA5

#define RADIO_COMMAND_CHARACTERISTIC 0xFA, 0x26, 0xA2, 0x40, 0x3B, 0x8A, 0x44, 0xD7, \
                                     0xBD, 0x9B, 0xE3, 0x7D, 0xBC, 0xDC, 0xE9, 0x58

#define RADIO_READ_LOG_CHARACTERISTIC 0xF7, 0x42, 0x47, 0x95, 0x77, 0x8E, 0x4E, 0x04, \
                                      0x94, 0x3E, 0x62, 0x17, 0xDC, 0x7D, 0x3A, 0x5D

#define RADIO_RX_STATS_CHARACTERISTIC 0xC4, 0x17, 0xB1, 0x87, 0x59, 0x6C, 0x48, 0x60, \
                                      0xAC, 0xD3, 0x17, 0xCD, 0xF8, 0xF8, 0x71, 0x73

#define RADIO_TX_STATS_CHARACTERISTIC 0xDF, 0x14, 0xEA, 0xAC, 0xB1, 0x07, 0x42, 0xEC, \
                                      0xB9, 0x93, 0x73, 0x22, 0x46, 0x10, 0x02, 0x0A

#define RADIO_SERVICE_UUID BT_UUID_DECLARE_128(RADIO_SERVICE)
#define RADIO_COMMAND_CHARACTERISTIC_UUID BT_UUID_DECLARE_128(RADIO_COMMAND_CHARACTERISTIC)
#define RADIO_RX_STATS_CHARACTERISTIC_UUID BT_UUID_DECLARE_128(RADIO_RX_STATS_CHARACTERISTIC)
#define RADIO_TX_STATS_CHARACTERISTIC_UUID BT_UUID_DECLARE_128(RADIO_TX_STATS_CHARACTERISTIC)
#define RADIO_READ_LOG_CHARACTERISTIC_UUID BT_UUID_DECLARE_128(RADIO_READ_LOG_CHARACTERISTIC)

#define MAX_TRANSMIT_SIZE 240
uint8_t data_rx[MAX_TRANSMIT_SIZE];
uint8_t data_tx[MAX_TRANSMIT_SIZE];

uint8_t stats_read_buffer[256];

static nrf_radio_mode_t mode;
static uint8_t tx_power;
static uint8_t channel;

bool indicate_active = false;

static void send_tx_packets(void);
static K_WORK_DEFINE(send_tx_packets_worker, send_tx_packets);

static void receive_rx_packets(void);
static K_WORK_DEFINE(receive_rx_packets_worker, receive_rx_packets);

int send_all_logs(void);
// static K_WORK_DEFINE(send_all_logs_worker, send_all_logs);

int host_service_init(void)
{
    memset(&data_rx, 0, MAX_TRANSMIT_SIZE);
    memset(&data_tx, 0, MAX_TRANSMIT_SIZE);

    mode = NRF_RADIO_MODE_BLE_LR125KBIT;
    tx_power = RADIO_TXPOWER_TXPOWER_Pos8dBm;
    channel = 0;

    return 0;
}

// Disables BT temporarily and sends a set amount of packets before reenabling BT
static void send_tx_packets(void)
{
    // Wait until water
    k_msleep(5000);

    struct radio_test_config test_config;
    memset(&test_config, 0, sizeof(test_config));
    test_config.type = MODULATED_TX;
    test_config.mode = mode;
    test_config.params.modulated_tx.txpower = tx_power;
    test_config.params.modulated_tx.channel = channel;
    test_config.params.modulated_tx.pattern = TRANSMIT_PATTERN_11110000;
    test_config.params.modulated_tx.packets_num = 5000;

    // Reset radio TX statistics
    radio_packets_sent = 0;

    bluetooth_disable();
    printk("Disabling MPSL\n");
    mpsl_lib_uninit();

    printk("Starting TX test\n");
    radio_test_init();
    radio_test_start(&test_config);

    k_msleep(30000);

    printk("Cancelling test\n");
    radio_test_cancel();

    printk("Restarting MPSL and BT\n");
    mpsl_lib_init();
    bluetooth_enable();
}

static void receive_rx_packets(void)
{
    // Wait until water
    k_msleep(5000);

    struct radio_test_config test_config;
    memset(&test_config, 0, sizeof(test_config));
    test_config.type = RX;
    test_config.mode = mode;
    test_config.params.rx.channel = channel;
    test_config.params.rx.pattern = TRANSMIT_PATTERN_11110000;

    // Clear flash for logging
    if (fs_erase(fs_flash_device, 20) != 0)
    {
        printk("receive_rx_packets: error! could not erase flash\n");
        return;
    }

    // Reset radio RX statistics
    radio_total_rssi = 0;
    radio_packets_received = 0;
    radio_total_crcok = 0;
    radio_has_received = false;

    NRF_TIMER2->PRESCALER = 1;
    NRF_TIMER2->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    NRF_TIMER2->TASKS_CLEAR = TIMER_TASKS_CLEAR_TASKS_CLEAR_Trigger;
    NRF_TIMER2->MODE = TIMER_MODE_MODE_Timer;

    NRF_TIMER2->TASKS_START = TIMER_TASKS_START_TASKS_START_Trigger;

    bluetooth_disable();
    printk("receive_rx_packets: Disabling MPSL\n");
    mpsl_lib_uninit();

    printk("receive_rx_packets: Starting RX test\n");
    radio_test_init();
    radio_test_start(&test_config);
    k_msleep(10);
    radio_logging_active = true;

    k_msleep(31000);

    printk("receive_rx_packets: Cancelling test\n");
    radio_logging_active = false;
    radio_test_cancel();
    NRF_TIMER2->TASKS_STOP = TIMER_TASKS_STOP_TASKS_STOP_Trigger;

    printk("receive_rx_packets: Restarting MPSL and BT\n");
    mpsl_lib_init();
    bluetooth_enable();

    NRF_TIMER2->TASKS_CAPTURE[2] = TIMER_TASKS_CAPTURE_TASKS_CAPTURE_Trigger;

    uint32_t ticks_taken = NRF_TIMER2->CC[1] - NRF_TIMER2->CC[0];
    printk("receive_rx_packets: Done with RX stats: total %u, crc %u, rssi %u, ticks %u, time_taken %u\n",
           radio_packets_received, radio_total_crcok, radio_total_rssi, ticks_taken, NRF_TIMER2->CC[2]);
}

static ssize_t handle_host_command(
    struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    const void *buf,
    uint16_t len,
    uint16_t offset,
    uint8_t flags)
{
    const uint8_t *buffer = buf;

    // Print received payload
    printk("Received data, handle %d, conn %p, data byte : %u\n", attr->handle, conn, len);
    for (uint8_t i = 0; i < len; i++)
    {
        printk("%02X", buffer[i]);
    }
    printk("\n");

    switch (buffer[0])
    {
    case SET_TX_MODE:
        printk("SET_TX_MODE\n");

        switch (buffer[1])
        {
        case 0:
            mode = NRF_RADIO_MODE_BLE_LR125KBIT;
            break;

        case 1:
            mode = NRF_RADIO_MODE_BLE_LR500KBIT;
            break;

        case 2:
            mode = NRF_RADIO_MODE_BLE_1MBIT;
            break;

        case 3:
            mode = NRF_RADIO_MODE_BLE_2MBIT;
            break;

        case 4:
            mode = NRF_RADIO_MODE_NRF_1MBIT;
            break;

        case 5:
            mode = NRF_RADIO_MODE_NRF_2MBIT;
            break;

        case 6:
            mode = NRF_RADIO_MODE_IEEE802154_250KBIT;
            break;

        default:
            break;
        }

        break;

    case SET_TX_POWER:
        printk("SET_TX_POWER\n");
        uint8_t new_tx_power = buffer[1];
        if (new_tx_power > 8)
        {
            printk("Invalid TX power argument %d\n", new_tx_power);
            break;
        }

        tx_power = new_tx_power;
        break;

    case SET_TX_CHANNEL:
        printk("SET_TX_CHANNEL\n");
        uint8_t new_channel = buffer[1];
        if (new_channel > 80)
        {
            printk("Invalid TX channel argument %d\n", new_channel);
            break;
        }

        channel = new_channel;
        break;

    case SET_PACKET_SIZE:
        printk("SET_PACKET_SIZE %u\n", buffer[1]);
        packet_size = buffer[1];
        break;

    case START_TX:
        printk("START_TX\n");
        k_work_submit(&send_tx_packets_worker);
        break;

    case START_RX:
        printk("SET_RX\n");
        k_work_submit(&receive_rx_packets_worker);
        break;

    default:
        break;
    }

    return len;
}

static ssize_t read_tx_stats_handler(
    struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    void *buf,
    uint16_t len,
    uint16_t offset)
{
    stats_read_buffer[0] = radio_packets_sent & 0xFF;
    stats_read_buffer[1] = (radio_packets_sent >> 8) & 0xFF;
    stats_read_buffer[2] = (radio_packets_sent >> 16) & 0xFF;
    stats_read_buffer[3] = (radio_packets_sent >> 24) & 0xFF;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, stats_read_buffer, 4);
}

static ssize_t read_rx_stats_handler(
    struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    void *buf,
    uint16_t len,
    uint16_t offset)
{
    stats_read_buffer[0] = radio_total_rssi & 0xFF;
    stats_read_buffer[1] = (radio_total_rssi >> 8) & 0xFF;
    stats_read_buffer[2] = (radio_total_rssi >> 16) & 0xFF;
    stats_read_buffer[3] = (radio_total_rssi >> 24) & 0xFF;

    stats_read_buffer[4] = radio_packets_received & 0xFF;
    stats_read_buffer[5] = (radio_packets_received >> 8) & 0xFF;
    stats_read_buffer[6] = (radio_packets_received >> 16) & 0xFF;
    stats_read_buffer[7] = (radio_packets_received >> 24) & 0xFF;

    stats_read_buffer[8] = radio_total_crcok & 0xFF;
    stats_read_buffer[9] = (radio_total_crcok >> 8) & 0xFF;
    stats_read_buffer[10] = (radio_total_crcok >> 16) & 0xFF;
    stats_read_buffer[11] = (radio_total_crcok >> 24) & 0xFF;

    uint32_t ticks_taken = NRF_TIMER2->CC[1] - NRF_TIMER2->CC[0];

    stats_read_buffer[12] = ticks_taken & 0xFF;
    stats_read_buffer[13] = (ticks_taken >> 8) & 0xFF;
    stats_read_buffer[14] = (ticks_taken >> 16) & 0xFF;
    stats_read_buffer[15] = (ticks_taken >> 24) & 0xFF;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, stats_read_buffer, 16);
}

static void on_sent(struct bt_conn *conn, void *user_data)
{
    ARG_UNUSED(user_data);

    const bt_addr_le_t *addr = bt_conn_get_dst(conn);

    printk("Data sent to Address 0x %02X %02X %02X %02X %02X %02X \n", addr->a.val[0], addr->a.val[1], addr->a.val[2], addr->a.val[3], addr->a.val[4], addr->a.val[5]);
}

void on_cccd_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    switch (value)
    {
    case BT_GATT_CCC_NOTIFY:
        // Start sending stuff!
        printk("on_cccd_changed: notify\n");
        indicate_active = true;
        break;

    case BT_GATT_CCC_INDICATE:
        // Start sending stuff via indications
        printk("on_cccd_changed: indicate\n");
        indicate_active = true;

        // For some reason, having this line here causes the board to be stuck
        // when RTT is trying to connect?
        // It seems like if any ble handler references `send_all_logs`, which
        // references `host_service`, some vague error occurs that makes it
        // so that RTT does not work??
        // k_work_submit(&send_all_logs_worker);

        break;

    case 0:
        // Stop sending stuff
        printk("on_cccd_changed: stop\n");
        indicate_active = false;
        break;

    default:
        printk("Error, CCCD has been set to an invalid value");
    }
}

BT_GATT_SERVICE_DEFINE(host_service,
                       BT_GATT_PRIMARY_SERVICE(RADIO_SERVICE_UUID),
                       BT_GATT_CHARACTERISTIC(RADIO_COMMAND_CHARACTERISTIC_UUID,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                              NULL, handle_host_command, NULL),
                       BT_GATT_CHARACTERISTIC(RADIO_RX_STATS_CHARACTERISTIC_UUID,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              read_rx_stats_handler, NULL, NULL),
                       BT_GATT_CCC(on_cccd_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CHARACTERISTIC(RADIO_TX_STATS_CHARACTERISTIC_UUID,
                                              BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ,
                                              read_tx_stats_handler, NULL, NULL), );

int send_all_logs(void)
{
    printk("send_all_logs: starting...\n");

    const struct bt_gatt_attr *attr = &host_service.attrs[3];

    uint8_t part = 1;

    flash_read_t result;

    while (true)
    {
        result = fs_read(fs_flash_device, stats_read_buffer, part);

        if (result.res != FS_SUCCESS)
        {
            printk("send_all_logs: read result %d\n", result.res);
            break;
        }

        if (!indicate_active)
        {
            break;
        }

        // Buffer tends to be big, send as max mtu
        // uint16_t mtu = bt_gatt_get_mtu(bt_current_conn);
        uint16_t mtu = 20;
        for (size_t byte_index = 0; byte_index < result.bytes_read; byte_index += mtu)
        {
            size_t remaining_bytes = mtu;
            if (byte_index + mtu > result.bytes_read)
            {
                remaining_bytes = result.bytes_read - byte_index;
            }

            int err = bt_gatt_notify(NULL, attr, stats_read_buffer + byte_index, remaining_bytes);
            if (err != 0)
            {
                printk("send_all_logs: bt_gatt_indicate err %d\n", err);
            }
        }

        part++;
    }

    printk("send_all_logs: end\n");
    return 0;
}
