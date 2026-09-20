# Storing songs in the external flash

Notes for the experimental MP3 player. The hub has a 32 MB external SPI
flash chip, of which Pybricks uses 256 KB. This is about whether the rest
can hold songs, and what it would take.

## What is in the external flash today

| Region | Used by |
|---|---|
| 0 – 4 KB | Skipped by LEGO's firmware updater (first sector) |
| 4 KB – 1020 KB | "Raw filesystem": staging area for firmware updates |
| 512 KB – 768 KB | **Pybricks program storage** (inside the staging area) |
| 1020 KB – 1 MB | Skipped by LEGO's firmware updater (last sector) |
| 1 MB – 32 MB | File system of LEGO's firmware: programs, sounds, images |

Sources, all in the MicroPython port that LEGO's own firmware is based on,
at `micropython/ports/stm32/boards/LEGO_HUB_NO6`:

- `mpconfigboard.h`: `MICROPY_HW_SPIFLASH_OFFSET_BYTES` is 1 MB, and the
  file system spans from there to the end of the 32 MB chip.
- `appupdate.py`: "The raw filesystem is in the first 1MiB of external SPI
  flash, but skip the first and last flash sectors." A firmware update
  writes the new image there, starting at a random block and wrapping
  around, so any part of that megabyte can be overwritten.

This also explains the choice Pybricks made: its 256 KB sits inside the
update staging area, which no file system claims, at the cost of being
erased whenever a LEGO firmware update runs.

## Would songs interfere with LEGO's firmware?

**Yes, if songs go above 1 MB, which they must.** That region is LEGO's
file system. Writing songs there corrupts it, and LEGO's firmware would
most likely reformat it on the next boot, which loses whatever the user had
stored: downloaded programs, and any sounds and images the app put there.
The SPIKE app can restore its own content, but hub-stored user programs
would be gone.

The reverse is just as true. Whenever LEGO's firmware runs, it may write to
its file system, and the songs are lost.

Below 1 MB is not an option either: only about 250 KB is free there, and a
LEGO firmware update can overwrite all of it.

### Can it stop LEGO's firmware from working?

No, as long as the file system is left in a state that LEGO's firmware
recognises as empty.

- **Reflashing always works.** The bootloader and the firmware live in the
  internal flash of the chip, not in the external flash. The first 32 KB of
  internal flash cannot be erased and is entered with a button, so DFU is
  always available no matter what the external flash contains.
- **An empty file system is recreated automatically.** In
  `micropython/ports/stm32/main.c`, `init_flash_fs()` creates a fresh file
  system when mounting fails because there is none. LEGO's firmware is
  built on this port, so a hub whose file system was overwritten should
  come up with an empty one.
- **The case to avoid** is in the same function: a partly valid littlefs
  superblock makes it fall back to a default block device and leave
  recovery to the user, instead of formatting.

So when the song region is first claimed, the firmware should also erase
the first two blocks at the 1 MB offset, where both littlefs and FAT keep
the data that identifies the file system. That turns "damaged file system"
into "no file system", which is the case that recovers by itself. Failing
that, the app's factory reset recreates it.

### Measured on a hub

Read with `hub.system.flash_read()` on a hub that had been used normally:

- At 1 MB there is a **littlefs v2** file system, with its usual pair of
  superblocks in the first two blocks. Its header reports a block size of
  4 KB and 7936 blocks, which is 31 MB: the whole chip above 1 MB.
- Sampling 32 bytes every 64 KB across the chip, **331 of 512 samples hold
  data, right up to 31.9 MB**.

Mounting that file system and asking it what it holds gives a very
different picture: **0.05 MB in use of 31 MB**, with a handful of small
files. The 65 % of samples holding data is stale: littlefs does not erase
blocks when it stops using them, so most of what the sampling found is
rubbish left behind by earlier use.

So the chip is nearly empty, and the file system may still allocate
anywhere in it, including any region we might pick for ourselves.

### Better: share the file system

Taking a region and overwriting the file system, as described above, is not
necessary. Songs can be ordinary files in the file system that is already
there, which keeps both firmwares working and destroys nothing.

littlefs is in the MicroPython submodule at `micropython/lib/littlefs`, so
the firmware can mount the same file system. Compiled for reading only, it
costs about 4 KB of firmware and 288 bytes of buffers, because only the
read paths are linked in.

What this gives up: LEGO's firmware may delete or overwrite the songs, for
instance when the app resets the hub, and it allocates from the same free
space. Neither matters for a hub used with Pybricks.

The one real cost is that littlefs has a synchronous interface while the
flash driver is asynchronous, so its reads have to be waited out. Keeping
each read to about 1 KB puts that at roughly 0.35 ms, which is less than
one decode step, as long as reading is done in its own step.

## Other considerations

### Reading while playing

The chip is on SPI2 at 24 MHz (`BaudRatePrescaler` of 2 on a 48 MHz bus),
so about 3 MB/s. A 24 kbit/s song needs 3 KB/s, about a thousandth of that,
so reading during playback is not a throughput problem.

It is a structural change though: the decoder currently reads the song from
a pointer into firmware flash, which is always available. Reading from the
external flash is asynchronous, so the background task has to wait for a
read to finish. The simplest approach is a window buffer of one or two
4 KB sectors that is refilled ahead of the decoder.

### Writing songs to the hub

There is no path today for getting a song onto the hub. Options, cheapest
first:

- **Over the USB or Bluetooth connection into RAM, then to flash.** A new
  Pybricks protocol command, or a Python API fed by `stdin`. USB is the
  practical choice: a 478 KB song takes seconds over USB, but minutes over
  Bluetooth.
- **As part of the firmware**, as now, with the firmware build embedding
  several songs. No new upload path, but a rebuild and reflash per song,
  and the firmware is limited to about 600 KB of audio.

Erasing must be taken into account: 4 KB per erase operation, tens of
milliseconds each, so about 15 to 20 seconds per 8 MB. Playback has to stop
while erasing and writing, since the chip cannot read then.

### Keeping track of songs

A file system is not needed. A single header sector listing name, offset
and length for each song is enough, and it is much less code. Songs are
written whole and never modified in place.

### Amount of audio

At 24 kbit/s, 8 MB holds about 45 minutes; the full 31 MB holds about three
hours. Both are far beyond what a competition needs, so the region can be
kept small deliberately, which also reduces the chance of colliding with
LEGO's file system.

## Suggested next step

Prototype with a fixed region at a chosen offset, a header sector, and
playback reading through a window buffer, while keeping the
embedded-in-firmware path working. Decide between option 1 and option 2
above before writing anything to the chip on a hub that matters.
