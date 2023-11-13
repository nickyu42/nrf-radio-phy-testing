#include "flash.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>

static off_t fs_offset = 0;
static uint8_t current_part = 0;
static bool reached_end = false;

flash_read_t fs_read(struct device *d,
                     uint8_t *buf,
                     uint8_t part)
{
    int err;
    flash_read_t ret;

    uint16_t len = 0;
    uint8_t header_buf[5];
    off_t offset = 0;

    do
    {
        if (flash_read(d, offset, header_buf, 5) != 0)
        {
            ret.res = FS_ERROR;
            return ret;
        }

        // printk("fs_read: %x %x %x %x %x\n", header_buf[0], header_buf[1], header_buf[2], header_buf[3], header_buf[4]);

        // If no packet here
        if (header_buf[0] != 0xaa || header_buf[1] != 0xaa)
        {
            ret.res = FS_EOF;
            return ret;
        }

        len = (uint16_t)header_buf[3] | ((uint16_t)header_buf[4]) << 8;

        // header + len + first byte of next part
        offset += len + 5;

    } while (header_buf[2] < part);

    if (flash_read(d, offset - len, buf, len) != 0)
    {
        ret.res = FS_ERROR;
        return ret;
    }

    ret.bytes_read = len;
    ret.res = FS_SUCCESS;
    return ret;
}

int fs_skip_to_end(struct device *d)
{
    if (reached_end)
    {
        return 0;
    }

    int err;
    uint16_t len = 0;
    uint8_t header_buf[5];

    // printk("skipping %x %x %x\n", len, fs_offset, current_part);
    while (fs_offset < FLASH_SIZE - 6)
    {
        if (flash_read(d, fs_offset, header_buf, 5) != 0)
        {
            return -1;
        }

        // If no packet here
        if (header_buf[0] != 0xaa || header_buf[1] != 0xaa)
        {
            // printk("reached end\n");
            reached_end = true;
            return 0;
        }

        len = header_buf[3] | header_buf[4] << 8;

        // header + len + first byte of next part
        fs_offset += len + 5 + 1;
        current_part = header_buf[2];

        // printk("skipping %x %x %x\n", len, fs_offset, current_part);
    }

    // Should never happen ideally
    return -1;
}

// Note that `len` must be a multiple of 2
int fs_write_packet(struct device *d, uint8_t *buf, uint16_t len)
{
    int err;
    if (!reached_end)
    {
        err = fs_skip_to_end(d);
        // printk("fs_write_packet: err %d\n", err);
        if (err != 0)
        {
            return err;
        }
    }

    uint8_t write_buf[len + 5];

    write_buf[0] = 0xaa;
    write_buf[1] = 0xaa;
    write_buf[2] = current_part + 1;
    write_buf[3] = (uint8_t)len & 0xff;
    write_buf[4] = (uint8_t)(len >> 8) & 0xff;

    // printk("fs_write_packet: memcpy\n");
    memcpy(write_buf + 5, buf, len);

    // int i = 0;
    // printk("fs_write_packet: %x %x %x %x %x %x %x \n", write_buf[i++], write_buf[i++], write_buf[i++], write_buf[i++], write_buf[i++], write_buf[i++], write_buf[i++]);

    // printk("fs_write_packet: write off=%x len=%x - %x %x\n", fs_offset, len, write_buf[0], write_buf[2]);

    // k_msleep(10);

    err = flash_write(d, fs_offset, write_buf, len + 5);
    if (err != 0)
    {
        return err;
    }

    current_part++;
    fs_offset += len + 5;
}