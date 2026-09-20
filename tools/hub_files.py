#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The Pybricks Authors

"""
Lists, reads and writes the files on the external flash of a hub running
firmware built with PB_MP3.

The hub does this itself, so no program has to be running on it. It
connects over USB if a hub is plugged in, and otherwise over Bluetooth.

Usage:

    python tools/hub_files.py list
    python tools/hub_files.py put song.mp3 /song.mp3
    python tools/hub_files.py get /program/00/program.mpy program.mpy
"""

import argparse
import asyncio
import struct
import time
from pathlib import Path

from pybricksdev.ble.pybricks import PYBRICKS_COMMAND_EVENT_UUID

COMMAND_WRITE_FLASH_FILE = 8
COMMAND_READ_FLASH_FILE = 9

# Operations of the write command.
OPEN, APPEND, CLOSE = 0, 1, 2

# Operations of the read command.
LIST, READ = 0, 1

# Event that answers the read command, and the kinds of answer.
EVENT_FLASH_FILE_DATA = 4
REPLY_ENTRY, REPLY_DATA, REPLY_END, REPLY_ERROR = 0, 1, 2, 3


class Hub:
    """Talks to a hub over whichever connection is available."""

    def __init__(self, hub):
        self.hub = hub
        self.replies = asyncio.Queue()

    async def connect(self):
        # Answers to the read command arrive as events. The handler is
        # registered while connecting, so replace it before that.
        original = self.hub._pybricks_service_handler

        def handler(_, data: bytes) -> None:
            if data and data[0] == EVENT_FLASH_FILE_DATA:
                self.replies.put_nowait(bytes(data[1:]))
            else:
                original(_, data)

        self.hub._pybricks_service_handler = handler
        await self.hub.connect()

    async def disconnect(self):
        await self.hub.disconnect()

    async def command(self, *parts: bytes) -> None:
        await self.hub.write_gatt_char(PYBRICKS_COMMAND_EVENT_UUID, b"".join(parts), True)

    async def reply(self, timeout=30) -> bytes:
        return await asyncio.wait_for(self.replies.get(), timeout)

    @property
    def chunk_size(self) -> int:
        # Two bytes go to the command and the operation.
        return self.hub._max_write_size - 2


async def open_hub(use_ble: bool) -> Hub:
    if not use_ble:
        try:
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
                    in (
                        SPIKE_PRIME_USB_PID,
                        SPIKE_ESSENTIAL_USB_PID,
                        MINDSTORMS_INVENTOR_USB_PID,
                    )
                    and device.product.endswith("Pybricks")
                )

            device = find_usb(custom_match=is_pybricks_usb)
            if device is not None:
                print("Found a hub on USB.")
                return Hub(PybricksHubUSB(device))
        except Exception:
            # No USB backend, such as a missing driver.
            pass

    from pybricksdev.ble import find_device
    from pybricksdev.connections.pybricks import PybricksHubBLE

    print("Looking for a hub over Bluetooth...")
    return Hub(PybricksHubBLE(await find_device()))


async def list_files(hub: Hub, path: str) -> None:
    await hub.command(bytes([COMMAND_READ_FLASH_FILE, LIST]), path.encode())

    while True:
        reply = await hub.reply()
        kind = reply[0]

        if kind == REPLY_ENTRY:
            is_dir = reply[1]
            size = struct.unpack("<I", reply[2:6])[0]
            name = reply[6:].decode()
            print(f"  {name + '/' if is_dir else name:<28s} {'' if is_dir else size}")
        elif kind == REPLY_END:
            return
        elif kind == REPLY_ERROR:
            raise RuntimeError(f"the hub could not list {path}")


async def get_file(hub: Hub, path: str, destination: Path) -> None:
    data = bytearray()
    chunk = 100

    while True:
        await hub.command(
            bytes([COMMAND_READ_FLASH_FILE, READ]),
            struct.pack("<IH", len(data), chunk),
            path.encode(),
        )

        piece = b""
        while True:
            reply = await hub.reply()
            if reply[0] == REPLY_DATA:
                piece = reply[2:]
            elif reply[0] == REPLY_END:
                break
            elif reply[0] == REPLY_ERROR:
                raise RuntimeError(f"the hub could not read {path}")

        data.extend(piece)
        print(f"\r  {len(data)} bytes", end="")

        # A short piece means the end of the file.
        if len(piece) < chunk:
            break

    print()
    destination.write_bytes(data)
    print(f"Wrote {len(data)} bytes to {destination}.")


async def put_file(hub: Hub, file: Path, path: str) -> None:
    data = file.read_bytes()
    print(f"Uploading {file.name} ({len(data) / 1024:.1f} KB) to {path}.")

    await hub.command(bytes([COMMAND_WRITE_FLASH_FILE, OPEN]), path.encode())

    start = time.monotonic()
    sent = 0

    while sent < len(data):
        piece = data[sent : sent + hub.chunk_size]
        await hub.command(bytes([COMMAND_WRITE_FLASH_FILE, APPEND]), piece)
        sent += len(piece)

        elapsed = time.monotonic() - start
        print(
            f"\r  {sent / 1024:7.1f} KB of {len(data) / 1024:.1f} KB"
            f"   {sent / 1024 / elapsed:5.1f} KB/s",
            end="",
        )

    await hub.command(bytes([COMMAND_WRITE_FLASH_FILE, CLOSE]))
    print()
    print(f"Done in {time.monotonic() - start:.1f} s.")


async def run(args) -> None:
    hub = await open_hub(args.ble)
    await hub.connect()
    try:
        if args.action == "list":
            await list_files(hub, args.path or "/")
        elif args.action == "put":
            await put_file(hub, Path(args.file), args.path or "/" + Path(args.file).name)
        else:
            await get_file(hub, args.file, Path(args.path or Path(args.file).name))
    finally:
        await hub.disconnect()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["list", "put", "get"])
    parser.add_argument("file", nargs="?", help="file to send, or path on the hub to read")
    parser.add_argument("path", nargs="?", help="where to put it")
    parser.add_argument("--ble", action="store_true", help="use Bluetooth even if USB is available")
    args = parser.parse_args()

    asyncio.run(run(args))


if __name__ == "__main__":
    main()
