#include <zephyr/sys/printk.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/hrs.h>

#include "service.h"

#define DEVICE_NAME "platynode"
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

// static void start_advertising_coded(struct k_work *work);
// static K_WORK_DEFINE(start_advertising_coded_worker, start_advertising_coded);

static void start_advertising(struct k_work *work);
static K_WORK_DEFINE(start_advertising_worker, start_advertising);

static struct bt_le_ext_adv *adv;

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
                  BT_UUID_16_ENCODE(BT_UUID_DIS_VAL)),
    // BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN)
};

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
    int err;
    struct bt_conn_info info;
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (conn_err)
    {
        printk("Connection failed (err %d)\n", conn_err);
        return;
    }

    err = bt_conn_get_info(conn, &info);
    if (err)
    {
        printk("Failed to get connection info (err %d)\n", err);
    }
    else
    {
        printk("Connected: %s\n", addr);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected (reason 0x%02x)\n", reason);

    k_work_submit(&start_advertising_worker);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

// static int create_advertising_coded(void)
// {
//     int err;
//     struct bt_le_adv_param param =
//         BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONNECTABLE |
//                                  BT_LE_ADV_OPT_EXT_ADV |
//                                  BT_LE_ADV_OPT_CODED,
//                              BT_GAP_ADV_FAST_INT_MIN_2,
//                              BT_GAP_ADV_FAST_INT_MAX_2,
//                              NULL);

//     err = bt_le_ext_adv_create(&param, NULL, &adv);
//     if (err)
//     {
//         printk("Failed to create advertiser set (err %d)\n", err);
//         return err;
//     }

//     printk("Created adv: %p\n", adv);

//     err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
//     if (err)
//     {
//         printk("Failed to set advertising data (err %d)\n", err);
//         return err;
//     }

//     return 0;
// }

// static void start_advertising_coded(struct k_work *work)
// {
//     int err;

//     err = bt_le_ext_adv_start(adv, NULL);
//     if (err)
//     {
//         printk("Failed to start advertising set (err %d)\n", err);
//         return;
//     }

//     printk("Advertiser %p set started\n", adv);
// }

static void start_advertising(struct k_work *work)
{
    int err;

    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        printk("Advertising failed to start (err %d)\n", err);
        return;
    }

    printk("Advertising successfully started\n");
}

int bluetooth_init(void)
{
    int err;

    printk("Enabling bluetooth");
    err = bt_enable(NULL);
    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }
    printk("Bluetooth initialized\n");

    host_service_init();

    // err = create_advertising_coded();
    // if (err)
    // {
    //     printk("Advertising failed to create (err %d)\n", err);
    //     return 0;
    // }

    k_work_submit(&start_advertising_worker);

    return 0;
}

int bluetooth_disable(void)
{
    int err;

    printk("Disabling bluetooth\n");

    err = bt_le_adv_stop();
    // err = bt_le_ext_adv_stop(adv);
    if (err)
    {
        printk("Could not stop advertising(err %d)\n", err);
        return 0;
    }

    printk("Advertising stopped\n");

    err = bt_disable();
    if (err)
    {
        printk("Could not stop bt (err %d)\n", err);
        return 0;
    }

    printk("BT stopped\n");

    return 0;
}

static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    printk("Pairing cancelled: %s\n", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .cancel = auth_cancel,
};

int bluetooth_enable(void)
{
    int err;

    printk("Enabling bluetooth\n");
    err = bt_enable(NULL);
    if (err)
    {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    // TODO: not sure if it is necessary to reinitialize this when enabling bluetooth
    // err = create_advertising_coded();
    // if (err)
    // {
    //     printk("Advertising failed to create (err %d)\n", err);
    //     return 0;
    // }

    bt_conn_auth_cb_register(&auth_cb_display);

    k_work_submit(&start_advertising_worker);
    return 0;
}