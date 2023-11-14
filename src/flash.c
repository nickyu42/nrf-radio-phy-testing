#include "flash.h"

#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <string.h>

static off_t fs_offset = 0;
static uint8_t current_part = 0;
static bool reached_end = false;

// XXX: this should ideally be checked for `device_is_ready()` at startup
struct device *fs_flash_device = NULL;

uint16_t round_to_pow2(uint16_t v)
{
    // See https://graphics.stanford.edu/%7Eseander/bithacks.html#RoundUpPowerOf2
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    return v + 1;
}

int fs_erase(struct device *d, uint8_t sectors)
{
    return flash_erase(d, 0, sectors * 4096);
}

void fs_init(void)
{
    printk("fs_init: Getting QSPI flash\n");
    if (fs_flash_device == NULL)
    {
        fs_flash_device = DEVICE_DT_GET(DT_ALIAS(spi_flash0));
        while (!device_is_ready(fs_flash_device))
        {
        }
    }
}

flash_read_t fs_read(struct device *d,
                     uint8_t *buf,
                     uint8_t part)
{
    flash_read_t ret;

    uint16_t len = 0;
    uint8_t header_buf[5];
    off_t offset = 0;

    if (!device_is_ready(d))
    {
        ret.res = FS_ERROR;
        return ret;
    }

    do
    {
        if (flash_read(d, offset, header_buf, 5) != 0)
        {
            ret.res = FS_ERROR;
            return ret;
        }

        // printk("fs_read: offset=%ld, %x %x %x %x %x\n", offset, header_buf[0], header_buf[1], header_buf[2], header_buf[3], header_buf[4]);

        // If no packet here
        if (header_buf[0] != 0xaa || header_buf[1] != 0xaa)
        {
            ret.res = FS_EOF;
            return ret;
        }

        len = (uint16_t)header_buf[3] | ((uint16_t)header_buf[4]) << 8;

        // header + len + first byte of next part
        offset += round_to_pow2(len + 5);

    } while (header_buf[2] < part);

    if (flash_read(d, offset - round_to_pow2(len + 5), buf, len + 5) != 0)
    {
        ret.res = FS_ERROR;
        return ret;
    }

    ret.bytes_read = len + 5;
    ret.res = FS_SUCCESS;
    return ret;
}

int fs_skip_to_end(struct device *d)
{
    printk("fs_skip_to_end: start\n");
    int err = 0;

    if (reached_end)
    {
        printk("fs_skip_to_end: reached end\n");
        return 0;
    }

    if (!device_is_ready(d))
    {
        return -1;
    }

    uint16_t len = 0;
    uint8_t header_buf[5];

    while (fs_offset < FLASH_SIZE - 6)
    {
        printk("fs_skip_to_end: fs_offset=%u\n", fs_offset);
        err = flash_read(d, fs_offset, header_buf, 5);
        if (err != 0)
        {
            return err;
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
    }

    // Should never happen ideally
    return -1;
}

// Note that `len + 5` must be a power of 2
// This is not checked, handle user side
// Edit: Must be a factor of 4, see zephyr/drivers/flash/nrf_qspi_nor.c:qspi_nor_write()
int fs_write_packet(struct device *d, uint8_t *buf, uint16_t len)
{
    int err;
    if (!reached_end)
    {
        err = fs_skip_to_end(d);

        if (err != 0)
        {
            return err;
        }
    }

    uint16_t l = round_to_pow2(len + 5);

    // printk("fs_write_packet: l %u len %u\n", l, len);

    uint8_t write_buf[l];

    write_buf[0] = 0xaa;
    write_buf[1] = 0xaa;
    write_buf[2] = current_part + 1;
    write_buf[3] = (uint8_t)len & 0xff;
    write_buf[4] = (uint8_t)(len >> 8) & 0xff;

    // printk("memcpy\n");

    memcpy(write_buf + 5, buf, len);

    // printk("fs_write_packet: write offset=%u\n", fs_offset);

    err = flash_write(d, fs_offset, write_buf, l);
    if (err != 0)
    {
        return err;
    }

    current_part++;
    fs_offset += l;

    return 0;
}