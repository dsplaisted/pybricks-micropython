# Talking to the hub over USB

USB is much faster than Bluetooth for sending files, but it was not finished
when v4.0.1 was made, so it is off unless a build asks for it:

    make PB_MP3=song.mp3 PB_USB=1

Run `make clean` first. Changing the flag does not make anything rebuild by
itself, so a build that skips it silently keeps the setting it had.

## What it is worth

Measured on a Prime Hub with the same 477 KB song, over one connection each:

| | Bluetooth | USB |
| --- | --- | --- |
| Upload | 8.4 KB/s | 57.5 KB/s |
| Download | 6.5 KB/s | 21.7 KB/s |
| Printing 6 KB | 360 ms | 26 ms |

Both transports work at once. Uploading 8.3 MB of songs took 149 seconds over
USB, where Bluetooth would have taken about 17 minutes.

Downloads gain less than uploads because an answer has to fit one USB packet,
which holds 64 bytes at full speed, against 244 for a Bluetooth message.

## Why it is off by default

Upstream turned USB off for release builds in a5c6648ed, saying it was
unfinished and could cause "unexpected lockups such as in lsusb or when
writing stdout". Around 25 commits touched USB afterwards, several of which
read as real fixes rather than tidying, such as `host: Fix send busy states`
and `host: Fix completion state`. Those sit on a later restructuring of the
driver, so they do not lift onto this release on their own.

Printing 126 KB over USB in one go did not lock anything up, and file
operations still worked afterwards, but that is one machine on one day
against a fault the authors described as unexpected. Hence the switch.

## Using it

The hub needs the WinUSB driver on Windows. Then any of these take `usb`
where they would take `ble`:

    pybricksdev files list usb /pybricks/songs
    pybricksdev files put usb song.mp3 /pybricks/songs/
    pybricksdev run usb program.py
