#include <mpsl/mpsl_lib.h>

#include "service.h"
#include "radio.h"
#include "bluetooth.h"

#define MY_SERVICE_UUID 0x1E, 0x6B, 0x0B, 0xEA, 0x7A, 0x4F, 0x48, 0x2B, \
                        0x86, 0x9C, 0x76, 0x80, 0x15, 0xA5, 0x1A, 0xA5

#define RX_CHARACTERISTIC_UUID 0xC4, 0x17, 0xB1, 0x87, 0x59, 0x6C, 0x48, 0x60, \
                               0xAC, 0xD3, 0x17, 0xCD, 0xF8, 0xF8, 0x71, 0x73

#define TX_CHARACTERISTIC_UUID 0xFA, 0x26, 0xA2, 0x40, 0x3B, 0x8A, 0x44, 0xD7, \
                               0xBD, 0x9B, 0xE3, 0x7D, 0xBC, 0xDC, 0xE9, 0x58

#define BT_UUID_MY_SERVICE BT_UUID_DECLARE_128(MY_SERVICE_UUID)
#define BT_UUID_MY_SERVICE_RX BT_UUID_DECLARE_128(RX_CHARACTERISTIC_UUID)
#define BT_UUID_MY_SERVICE_TX BT_UUID_DECLARE_128(TX_CHARACTERISTIC_UUID)

#define MAX_TRANSMIT_SIZE 240
uint8_t data_rx[MAX_TRANSMIT_SIZE];
uint8_t data_tx[MAX_TRANSMIT_SIZE];

uint8_t rx_stats_read_buffer[16];

static nrf_radio_mode_t mode;
static uint8_t tx_power;
static uint8_t channel;

static int send_tx_packets(void);
static K_WORK_DEFINE(send_tx_packets_worker, send_tx_packets);

static int receive_rx_packets(void);
static K_WORK_DEFINE(receive_rx_packets_worker, receive_rx_packets);

int host_service_init(void)
{
    memset(&data_rx, 0, MAX_TRANSMIT_SIZE);
    memset(&data_tx, 0, MAX_TRANSMIT_SIZE);

    mode = NRF_RADIO_MODE_BLE_LR125KBIT;
    tx_power = RADIO_TXPOWER_TXPOWER_Pos8dBm;
    channel = 0;
}

// Disables BT temporarily and sends a set amount of packets before reenabling BT
static int send_tx_packets(void)
{
    struct radio_test_config test_config;
    memset(&test_config, 0, sizeof(test_config));
    test_config.type = MODULATED_TX;
    test_config.mode = mode;
    test_config.params.modulated_tx.txpower = tx_power;
    test_config.params.modulated_tx.channel = channel;
    test_config.params.modulated_tx.pattern = TRANSMIT_PATTERN_11110000;
    test_config.params.modulated_tx.packets_num = 5000;

    bluetooth_disable();
    printk("Disabling MPSL\n");
    mpsl_lib_uninit();

    printk("Starting TX test\n");
    radio_test_init();
    radio_test_start(&test_config);

    k_msleep(5000);

    printk("Cancelling test\n");
    radio_test_cancel();

    printk("Restarting MPSL and BT\n");
    mpsl_lib_init();
    bluetooth_enable();
}

static int receive_rx_packets(void)
{
    struct radio_test_config test_config;
    memset(&test_config, 0, sizeof(test_config));
    test_config.type = RX;
    test_config.mode = mode;
    test_config.params.rx.channel = channel;
    test_config.params.rx.pattern = TRANSMIT_PATTERN_11110000;

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
    printk("Disabling MPSL\n");
    mpsl_lib_uninit();

    printk("Starting RX test\n");
    radio_test_init();
    radio_test_start(&test_config);

    k_msleep(6000);

    printk("Cancelling test\n");
    radio_test_cancel();
    NRF_TIMER2->TASKS_STOP = TIMER_TASKS_STOP_TASKS_STOP_Trigger;
    printk("Restarting MPSL and BT\n");
    mpsl_lib_init();
    bluetooth_enable();

    NRF_TIMER2->TASKS_CAPTURE[2] = TIMER_TASKS_CAPTURE_TASKS_CAPTURE_Trigger;

    uint32_t ticks_taken = NRF_TIMER2->CC[1] - NRF_TIMER2->CC[0];
    printk("Done with RX stats: total %u, crc %u, rssi %u, ticks %u, time_taken %u\n",
           radio_packets_received, radio_total_crcok, radio_total_rssi, ticks_taken, NRF_TIMER2->CC[2]);
}

static ssize_t on_receive(
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

static ssize_t on_read(
    struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    void *buf,
    uint16_t len,
    uint16_t offset)
{
    rx_stats_read_buffer[0] = radio_total_rssi & 0xFF;
    rx_stats_read_buffer[1] = (radio_total_rssi >> 8) & 0xFF;
    rx_stats_read_buffer[2] = (radio_total_rssi >> 16) & 0xFF;
    rx_stats_read_buffer[3] = (radio_total_rssi >> 24) & 0xFF;

    rx_stats_read_buffer[4] = radio_packets_received & 0xFF;
    rx_stats_read_buffer[5] = (radio_packets_received >> 8) & 0xFF;
    rx_stats_read_buffer[6] = (radio_packets_received >> 16) & 0xFF;
    rx_stats_read_buffer[7] = (radio_packets_received >> 24) & 0xFF;

    rx_stats_read_buffer[8] = radio_total_crcok & 0xFF;
    rx_stats_read_buffer[9] = (radio_total_crcok >> 8) & 0xFF;
    rx_stats_read_buffer[10] = (radio_total_crcok >> 16) & 0xFF;
    rx_stats_read_buffer[11] = (radio_total_crcok >> 24) & 0xFF;

    uint32_t ticks_taken = NRF_TIMER2->CC[1] - NRF_TIMER2->CC[0];

    rx_stats_read_buffer[12] = ticks_taken & 0xFF;
    rx_stats_read_buffer[13] = (ticks_taken >> 8) & 0xFF;
    rx_stats_read_buffer[14] = (ticks_taken >> 16) & 0xFF;
    rx_stats_read_buffer[15] = (ticks_taken >> 24) & 0xFF;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, rx_stats_read_buffer, 16);
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
        break;

    case BT_GATT_CCC_INDICATE:
        // Start sending stuff via indications
        break;

    case 0:
        // Stop sending stuff
        break;

    default:
        printk("Error, CCCD has been set to an invalid value");
    }
}

BT_GATT_SERVICE_DEFINE(host_service,
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_MY_SERVICE),
                       BT_GATT_CHARACTERISTIC(BT_UUID_MY_SERVICE_RX,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                              NULL, on_receive, NULL),
                       BT_GATT_CHARACTERISTIC(BT_UUID_MY_SERVICE_TX,
                                              BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ,
                                              on_read, NULL, NULL),
                       BT_GATT_CCC(on_cccd_changed,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

void host_service_send(struct bt_conn *conn, const uint8_t *data, uint16_t len)
{
    /*
    The attribute for the TX characteristic is used with bt_gatt_is_subscribed
    to check whether notification has been enabled by the peer or not.
    Attribute table: 0 = Service, 1 = Primary service, 2 = RX, 3 = TX, 4 = CCC.
    */
    const struct bt_gatt_attr *attr = &host_service.attrs[3];

    struct bt_gatt_notify_params params =
        {
            .uuid = BT_UUID_MY_SERVICE_TX,
            .attr = attr,
            .data = data,
            .len = len,
            .func = on_sent};

    // Check whether notifications are enabled or not
    if (bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY))
    {
        // Send the notification
        if (bt_gatt_notify_cb(conn, &params))
        {
            printk("Error, unable to send notification\n");
        }
    }
    else
    {
        printk("Warning, notification not enabled on the selected attribute\n");
    }
}