#ifndef SERVICE_H_
#define SERVICE_H_

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

typedef enum
{
    SET_TX_MODE = 0x00,
    SET_TX_POWER = 0x01,
    SET_TX_CHANNEL = 0x02,

    START_TX = 0x10,
    START_RX = 0x11,
} command_t;

int host_service_init(void);
void host_service_send(struct bt_conn *conn, const uint8_t *data, uint16_t len);

#endif