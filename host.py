import asyncio
from bleak import BleakScanner, BleakClient

PLATYNODE_1 = 'DD2AD668-201C-FAAB-B036-C245019CB582'
PLATYNODE_2 = '12939F17-4861-09C9-1D2D-06714A323FAA'

SEND_COMMAND_CHAR = '7371f8f8-cd17-d3ac-6048-6c5987b117c4'
READ_RX_STATS_CHAR = '58e9dcbc-7de3-9bbd-d744-8a3b40a226fa'

async def main():
    # devices = await BleakScanner.discover(
    #     return_adv=True
    # )

    # for d, a in devices.values():
    #     print()
    #     print(d)
    #     print("-" * len(str(d)))
    #     print(a)

    tx_mode = 1
    tx_power = 0
    tx_channel = 0

    device1 = await BleakScanner.find_device_by_address(PLATYNODE_1)
    device2 = await BleakScanner.find_device_by_address(PLATYNODE_2)
    print(device1, device2)

    async with BleakClient(device1) as tx_client, BleakClient(device2) as rx_client:
        await tx_client.write_gatt_char(SEND_COMMAND_CHAR, bytearray([0x00, tx_mode]), response=False)
        await tx_client.write_gatt_char(SEND_COMMAND_CHAR, bytearray([0x01, tx_power]), response=False)
        await tx_client.write_gatt_char(SEND_COMMAND_CHAR, bytearray([0x02, tx_channel]), response=False)

        await rx_client.write_gatt_char(SEND_COMMAND_CHAR, bytearray([0x00, tx_mode]), response=False)
        await rx_client.write_gatt_char(SEND_COMMAND_CHAR, bytearray([0x01, tx_power]), response=False)
        await rx_client.write_gatt_char(SEND_COMMAND_CHAR, bytearray([0x02, tx_channel]), response=False)

        await asyncio.sleep(0.1)

        # Start TX on client 1
        await tx_client.write_gatt_char(SEND_COMMAND_CHAR, b'\x10', response=False)
        await asyncio.sleep(0.5)

        # Start RX on client 2
        await rx_client.write_gatt_char(SEND_COMMAND_CHAR, b'\x11', response=False)
        await asyncio.sleep(7)

        await tx_client.disconnect()
        await rx_client.disconnect()

    # Reconnect client 2
    async with BleakClient(device2) as rx_client:
        # Read RX stats from client 2
        rx_stats = await rx_client.read_gatt_char(READ_RX_STATS_CHAR)

        rssi = rx_stats[0] | rx_stats[1] << 8 | rx_stats[2] << 16 | rx_stats[3] << 24
        packets = rx_stats[4] | rx_stats[5] << 8 | rx_stats[6] << 16 | rx_stats[7] << 24
        crc = rx_stats[8] | rx_stats[9] << 8 | rx_stats[10] << 16 | rx_stats[11] << 24
        ticks = rx_stats[12] | rx_stats[13] << 8 | rx_stats[14] << 16 | rx_stats[15] << 24

        print(f'{rssi=} {packets=} {crc=} {ticks=} average_rssi={rssi/packets}')

        await asyncio.sleep(0.1)
        await rx_client.disconnect()

asyncio.run(main())