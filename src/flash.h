#ifndef _FLASH_H_
#define _FLASH_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

#define FLASH_SIZE 16777216
#define FLASH_SECTOR_SIZE 4069

typedef enum
{
    FS_SUCCESS = 0xFF,

    FS_ERROR = 0x00,
    FS_EOF = 0x01,
} flash_read_result_t;

typedef struct
{
    uint8_t bytes_read;
    flash_read_result_t res;
} flash_read_t;

flash_read_t fs_read(struct device *d,
                     uint8_t *buf,
                     uint8_t part);

int fs_skip_to_end(struct device *d);

int fs_write_packet(struct device *d, uint8_t *buf, uint16_t len);

#endif