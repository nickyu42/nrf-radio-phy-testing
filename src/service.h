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
    SET_PACKET_SIZE = 0x03,

    START_TX = 0x10,
    START_RX = 0x11,
} command_t;

extern bool indicate_active;
int send_all_logs(void);

int host_service_init(void);

#endif