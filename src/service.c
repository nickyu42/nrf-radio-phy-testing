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

static nrf_radio_mode_t mode;
static uint8_t tx_power;
static uint8_t channel;

static struct radio_test_config test_config;

static int start_tx(void);
static K_WORK_DEFINE(start_tx_worker, start_tx);

int host_service_init(void)
{
    memset(&data_rx, 0, MAX_TRANSMIT_SIZE);
    memset(&data_tx, 0, MAX_TRANSMIT_SIZE);
    mode = NRF_RADIO_MODE_BLE_LR125KBIT;
    tx_power = RADIO_TXPOWER_TXPOWER_Pos8dBm;
    channel = 0;
}

typedef enum
{
    SET_TX_MODE = 0x00,
    SET_TX_POWER = 0x01,
    START_TX = 0x10,
    START_RX = 0x11,
} command_t;

static int start_tx(void)
{
    memset(&test_config, 0, sizeof(test_config));
    test_config.type = MODULATED_TX;
    test_config.mode = mode;
    test_config.params.modulated_tx.txpower = tx_power;
    test_config.params.modulated_tx.channel = channel;
    test_config.params.modulated_tx.pattern = TRANSMIT_PATTERN_11110000;
    test_config.params.modulated_tx.packets_num = 5000;

    bluetooth_disable();

    printk("STARTING TEST\n");
    radio_test_start(&test_config);
    k_msleep(1000);
    printk("STOPPING TEST\n");
    radio_test_cancel();

    bluetooth_enable();
}

static ssize_t
on_receive(struct bt_conn *conn,
           const struct bt_gatt_attr *attr,
           const void *buf,
           uint16_t len,
           uint16_t offset,
           uint8_t flags)
{
    const uint8_t *buffer = buf;

    printk("Received data, handle %d, conn %p, data byte : %u", attr->handle, conn, len);
    for (uint8_t i = 0; i < len; i++)
    {
        printk("%02X", buffer[i]);
    }
    printk("\n");

    switch (buffer[0])
    {
    case SET_TX_MODE:
        printk("SET_TX_MODE\n");
        break;

    case SET_TX_POWER:
        printk("SET_TX_POWER\n");
        break;

    case START_TX:
        printk("START_TX\n");
        k_work_submit(&start_tx_worker);
        break;

    case START_RX:
        printk("SET_RX\n");
        break;

    default:
        break;
    }

    return len;
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
                                              BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              NULL, NULL, NULL),
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