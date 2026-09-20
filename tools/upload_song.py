#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The Pybricks Authors

"""
Uploads a file to the external flash of a hub running firmware built with
PB_MP3, without rebuilding the firmware.

The hub stores the file itself, so no program has to be running. Each
command is answered by the hub before the next one is sent, which keeps it
from being sent data faster than it can write.

Usage:

    python tools/upload_song.py song.mp3 /song.mp3
"""

import argparse
import asyncio
import time
from pathlib import Path

from pybricksdev.ble import find_device
from pybricksdev.ble.pybricks import PYBRICKS_COMMAND_EVENT_UUID
from pybricksdev.connections.pybricks import PybricksHubBLE

# Writes a file on the external flash.
COMMAND_WRITE_FLASH_FILE = 8

OPEN = 0
APPEND = 1
CLOSE = 2


async def send(hub, operation: int, payload: bytes = b"") -> None:
    message = bytes([COMMAND_WRITE_FLASH_FILE, operation]) + payload
    await hub.write_gatt_char(PYBRICKS_COMMAND_EVENT_UUID, message, True)


async def upload(file: Path, path: str) -> None:

    data = file.read_bytes()
    print(f"Uploading {file.name} ({len(data) / 1024:.1f} KB) to {path} on the hub.")

    print("Looking for a hub...")
    device = await find_device()
    hub = PybricksHubBLE(device)
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
    args = parser.parse_args()

    asyncio.run(upload(args.file, args.path or "/" + args.file.name))


if __name__ == "__main__":
    main()
