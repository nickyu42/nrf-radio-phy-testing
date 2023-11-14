import asyncio
import csv
import json
import pprint
from bleak import BleakScanner, BleakClient

PLATYNODE_1 = "DD2AD668-201C-FAAB-B036-C245019CB582"
PLATYNODE_2 = "12939F17-4861-09C9-1D2D-06714A323FAA"

SEND_COMMAND_CHAR = "58e9dcbc-7de3-9bbd-d744-8a3b40a226fa"
READ_RX_STATS_CHAR = "7371f8f8-cd17-d3ac-6048-6c5987b117c4"
READ_TX_STATS_CHAR = "0a021046-2273-93b9-ec42-07b1acea14df"

prescaler = 1
oscillator_frequency = 16_000_000 / (2**prescaler)


async def scan():
    devices = await BleakScanner.discover(return_adv=True)

    for d, a in devices.values():
        print()
        print(d)
        print("-" * len(str(d)))
        print(a)


async def run_test(
    device1, device2, tx_mode, tx_power, tx_channel, packet_size, filename="results.csv"
):
    print(
        f"---------- STARTING TEST {tx_mode=} {tx_power=} {tx_channel=} -------------"
    )

    print("Connecting")
    async with BleakClient(device1) as tx_client, BleakClient(device2) as rx_client:
        print("Connected")

        # for s in tx_client.services:
        #     for c in s.characteristics:
        #         print(c, c.properties, c.description, c.uuid, c.descriptors)

        print("Setting parameters")
        await tx_client.write_gatt_char(
            SEND_COMMAND_CHAR, bytearray([0x00, tx_mode]), response=False
        )
        await tx_client.write_gatt_char(
            SEND_COMMAND_CHAR, bytearray([0x01, tx_power]), response=False
        )
        await tx_client.write_gatt_char(
            SEND_COMMAND_CHAR, bytearray([0x02, tx_channel]), response=False
        )
        await tx_client.write_gatt_char(
            SEND_COMMAND_CHAR, bytearray([0x03, packet_size]), response=False
        )

        await rx_client.write_gatt_char(
            SEND_COMMAND_CHAR, bytearray([0x00, tx_mode]), response=False
        )
        await rx_client.write_gatt_char(
            SEND_COMMAND_CHAR, bytearray([0x01, tx_power]), response=False
        )
        await rx_client.write_gatt_char(
            SEND_COMMAND_CHAR, bytearray([0x02, tx_channel]), response=False
        )
        await rx_client.write_gatt_char(
            SEND_COMMAND_CHAR, bytearray([0x03, packet_size]), response=False
        )

        await asyncio.sleep(0.1)

        # Start RX on client 2
        print("Starting RX")
        await rx_client.write_gatt_char(SEND_COMMAND_CHAR, b"\x11", response=False)
        await asyncio.sleep(0.1)

        # Start TX on client 1
        print("Starting TX")
        await tx_client.write_gatt_char(SEND_COMMAND_CHAR, b"\x10", response=False)
        await asyncio.sleep(7)

        await tx_client.disconnect()
        await rx_client.disconnect()

    # Reconnect
    print("Reconnecting")
    async with BleakClient(device1) as tx_client, BleakClient(device2) as rx_client:
        # Read RX stats from client 2
        print("Reading stats")
        rx_stats = await rx_client.read_gatt_char(READ_RX_STATS_CHAR)
        tx_stats = await tx_client.read_gatt_char(READ_TX_STATS_CHAR)

        await asyncio.sleep(0.1)
        await rx_client.disconnect()
        await tx_client.disconnect()

        print("------")

        rssi = rx_stats[0] | rx_stats[1] << 8 | rx_stats[2] << 16 | rx_stats[3] << 24
        packets = rx_stats[4] | rx_stats[5] << 8 | rx_stats[6] << 16 | rx_stats[7] << 24
        crc = rx_stats[8] | rx_stats[9] << 8 | rx_stats[10] << 16 | rx_stats[11] << 24
        ticks = (
            rx_stats[12] | rx_stats[13] << 8 | rx_stats[14] << 16 | rx_stats[15] << 24
        )
        sent = tx_stats[0] | tx_stats[1] << 8 | tx_stats[2] << 16 | tx_stats[3] << 24

        print(
            f"{sent=} | {packets=} {crc=} {rssi=} {ticks=} time_taken={ticks/oscillator_frequency}s",
            end="",
        )

        if packets > 0:
            print(f" average_rssi={rssi/packets}", end="")

        with open(filename, "a+") as f:
            writer = csv.writer(f)
            writer.writerow(
                [
                    tx_mode,
                    tx_channel,
                    tx_power,
                    packet_size,
                    sent,
                    packets,
                    crc,
                    rssi,
                    ticks,
                    ticks / oscillator_frequency,
                ]
            )

        print()


async def read_logs(device):
    print("reading logs")

    byte_buffer = bytearray()

    async def callback(char, bytes):
        byte_buffer.extend(bytes)

    async with BleakClient(device) as client:
        await client.start_notify(READ_RX_STATS_CHAR, callback)
        await asyncio.sleep(5)
        await client.disconnect()

    return byte_buffer


def decode_buffer(buffer):
    packets = []

    i = 0
    while i < len(buffer):
        header = buffer[i : i + 5]

        total_rssi = (
            buffer[i + 5 + 0]
            | buffer[i + 5 + 1] << 8
            | buffer[i + 5 + 2] << 16
            | buffer[i + 5 + 3] << 24
        )
        packets_count = (
            buffer[i + 5 + 4]
            | buffer[i + 5 + 5] << 8
            | buffer[i + 5 + 6] << 16
            | buffer[i + 5 + 7] << 24
        )
        crc = (
            buffer[i + 5 + 8]
            | buffer[i + 5 + 9] << 8
            | buffer[i + 5 + 10] << 16
            | buffer[i + 5 + 11] << 24
        )
        ticks = (
            buffer[i + 5 + 12]
            | buffer[i + 5 + 13] << 8
            | buffer[i + 5 + 14] << 16
            | buffer[i + 5 + 15] << 24
        )

        rssi = buffer[i + 5 + 16]
        packet_size = buffer[i + 5 + 17]
        packet = buffer[i + 5 + 18 : i + 5 + 18 + packet_size]

        row = {
            "part": buffer[i + 2],
            "total_rssi": total_rssi,
            "packet_count": packets_count,
            "crc": crc,
            "ticks": ticks,
        }

        pprint.pprint(row)

        row["last_packet"] = {
            "rssi": rssi,
            "size": packet_size,
            "data": list(packet),
        }

        packets.append(row)

        i += 5 + 18 + packet_size

    return packets


async def main():
    tx_channel = 100
    tx_power = 8
    tx_mode = 2
    packet_size = 128

    print("Finding device")
    device1 = await BleakScanner.find_device_by_address(PLATYNODE_1)
    device2 = await BleakScanner.find_device_by_address(PLATYNODE_2)

    # print('Starting experiments')
    # for tx_mode in range(5, 6):
    #     for packet_size in (16, 32, 64, 128, 255):
    #         for _ in range(5):
    #             await run_test(device1, device2, tx_mode, tx_power, tx_channel, packet_size, 'results_packetsize_20cm.csv')
    await run_test(
        device2, device1, tx_mode, tx_power, tx_channel, packet_size, "foo.csv"
    )
    buffer = await read_logs(device1)
    packets = decode_buffer(buffer)

    with open("raw_log_buffer.bin", "wb") as raw, open(
        "decoded_log_buffer.json", "w"
    ) as f:
        raw.write(buffer)

        f.writelines([json.dumps(p) + "\n" for p in packets])


asyncio.run(main())
