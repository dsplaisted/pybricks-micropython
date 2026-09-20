#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The Pybricks Authors

"""
Uploads a file to the external flash of a hub running firmware built with
PB_MP3, without rebuilding the firmware.

The hub stores the file itself, so no program has to be running. Each
command is answered by the hub before the next one is sent, which keeps it
from being sent data faster than it can write.

It connects over USB if a hub is plugged in, which is much faster, and
otherwise over Bluetooth.

Usage:

    python tools/upload_song.py song.mp3 /song.mp3
    python tools/upload_song.py song.mp3 /song.mp3 --ble
"""

import argparse
import asyncio
import time
from pathlib import Path

from pybricksdev.ble.pybricks import PYBRICKS_COMMAND_EVENT_UUID

# Writes a file on the external flash.
COMMAND_WRITE_FLASH_FILE = 8

OPEN = 0
APPEND = 1
CLOSE = 2


async def connect_usb():
    """Connects to a hub over USB, or returns None if there is none."""
    from usb.core import find as find_usb

    from pybricksdev.connections.pybricks import PybricksHubUSB
    from pybricksdev.usb import (
        LEGO_USB_VID,
        MINDSTORMS_INVENTOR_USB_PID,
        SPIKE_ESSENTIAL_USB_PID,
        SPIKE_PRIME_USB_PID,
    )

    def is_pybricks_usb(device):
        return (
            device.idVendor == LEGO_USB_VID
            and device.idProduct
            in (SPIKE_PRIME_USB_PID, SPIKE_ESSENTIAL_USB_PID, MINDSTORMS_INVENTOR_USB_PID)
            and device.product.endswith("Pybricks")
        )

    try:
        device = find_usb(custom_match=is_pybricks_usb)
    except Exception:
        # No USB backend, such as a missing driver.
        return None

    return PybricksHubUSB(device) if device else None


async def connect_ble():
    from pybricksdev.ble import find_device
    from pybricksdev.connections.pybricks import PybricksHubBLE

    return PybricksHubBLE(await find_device())


async def send(hub, operation: int, payload: bytes = b"") -> None:
    message = bytes([COMMAND_WRITE_FLASH_FILE, operation]) + payload
    await hub.write_gatt_char(PYBRICKS_COMMAND_EVENT_UUID, message, True)


async def upload(file: Path, path: str, use_ble: bool) -> None:

    data = file.read_bytes()
    print(f"Uploading {file.name} ({len(data) / 1024:.1f} KB) to {path} on the hub.")

    hub = None if use_ble else await connect_usb()
    if hub is None:
        print("Looking for a hub over Bluetooth...")
        hub = await connect_ble()
    else:
        print("Found a hub on USB.")

    await hub.connect()

    try:
        # Two bytes go to the command and the operation.
        chunk = hub._max_write_size - 2

        await send(hub, OPEN, path.encode())

        start = time.monotonic()
        sent = 0

        while sent < len(data):
            piece = data[sent : sent + chunk]
            await send(hub, APPEND, piece)
            sent += len(piece)

            elapsed = time.monotonic() - start
            print(
                f"\r  {sent / 1024:7.1f} KB of {len(data) / 1024:.1f} KB"
                f"   {sent / 1024 / elapsed:5.1f} KB/s",
                end="",
            )

        await send(hub, CLOSE)
        print()
        print(f"Done in {time.monotonic() - start:.1f} s.")
    finally:
        await hub.disconnect()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path, help="file to upload")
    parser.add_argument("path", nargs="?", help="path on the hub, such as /song.mp3")
    parser.add_argument("--ble", action="store_true", help="use Bluetooth even if USB is available")
    args = parser.parse_args()

    asyncio.run(upload(args.file, args.path or "/" + args.file.name, args.ble))


if __name__ == "__main__":
    main()
