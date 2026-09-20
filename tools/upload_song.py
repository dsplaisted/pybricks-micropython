#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The Pybricks Authors

"""
Uploads a file to the external flash of a hub running firmware built with
PB_MP3, without rebuilding the firmware.

It runs a small program on the hub that receives the file, then sends the
file over Bluetooth. The hub acknowledges each chunk, so that it is never
sent data faster than it can write to the flash.

Usage:

    python tools/upload_song.py song.mp3 /song.mp3
"""

import argparse
import asyncio
import tempfile
import time
from pathlib import Path

from pybricksdev.ble import find_device
from pybricksdev.connections.pybricks import PybricksHubBLE

# Runs on the hub. The path and size are filled in before it is compiled.
HUB_PROGRAM = """
from pybricks.hubs import PrimeHub

hub = PrimeHub()
received = hub.system.flash_receive_file(path="{path}", size={size}, chunk={chunk})
print("DONE", received)
"""


async def wait_for_line(hub, timeout=30):
    # With print_output off, the hub puts each line it prints in this queue.
    return (await asyncio.wait_for(hub._stdout_line_queue.get(), timeout)).strip()


async def upload(file: Path, path: str, chunk: int) -> None:

    data = file.read_bytes()
    print(f"Uploading {file.name} ({len(data) / 1024:.1f} KB) to {path} on the hub.")

    print("Looking for a hub...")
    device = await find_device()
    hub = PybricksHubBLE(device)
    await hub.connect()

    try:
        with tempfile.TemporaryDirectory() as directory:
            program = Path(directory) / "receive.py"
            program.write_text(
                HUB_PROGRAM.format(path=path, size=len(data), chunk=chunk)
            )
            await hub.run(str(program), wait=False, print_output=False)

            while (line := await wait_for_line(hub)) != "READY":
                print(f"hub: {line}")

            # One BLE packet at a time, up to what the hub accepts.
            packet = hub._max_write_size - 1
            start = time.monotonic()
            sent = 0

            while sent < len(data):
                piece = data[sent : sent + chunk]

                for index in range(0, len(piece), packet):
                    await hub.write(piece[index : index + packet])

                # Wait for the hub to store it before sending more.
                line = await wait_for_line(hub)
                if line != "K":
                    raise RuntimeError(f"unexpected reply from hub: {line}")

                sent += len(piece)
                elapsed = time.monotonic() - start
                print(
                    f"\r  {sent / 1024:7.1f} KB of {len(data) / 1024:.1f} KB"
                    f"   {sent / 1024 / elapsed:5.1f} KB/s",
                    end="",
                )

            print()
            print(f"hub: {await wait_for_line(hub)}")
            print(f"Done in {time.monotonic() - start:.1f} s.")
    finally:
        await hub.disconnect()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path, help="file to upload")
    parser.add_argument("path", nargs="?", help="path on the hub, such as /song.mp3")
    parser.add_argument("--chunk", type=int, default=1024, help="bytes per chunk")
    args = parser.parse_args()

    asyncio.run(upload(args.file, args.path or "/" + args.file.name, args.chunk))


if __name__ == "__main__":
    main()
