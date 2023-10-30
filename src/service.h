#ifndef SERVICE_H_
#define SERVICE_H_

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

int my_service_init(void);

void my_service_send(struct bt_conn *conn, const uint8_t *data, uint16_t len);

#endif