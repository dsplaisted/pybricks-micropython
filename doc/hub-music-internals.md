# How music on the hub works

Technical notes behind `hub-music.md`.

## Sharing the flash with the firmware of LEGO

The hub has a 32 MB external SPI flash chip. Pybricks uses 256 KB of it, and
the firmware of LEGO keeps a littlefs file system in everything above 1 MB.

| Region | Used by |
| --- | --- |
| 0 – 4 KB | Skipped by the firmware updater of LEGO |
| 4 KB – 1020 KB | Staging area for firmware updates |
| 512 KB – 768 KB | Pybricks program storage, inside that staging area |
| 1020 KB – 1 MB | Skipped by the firmware updater of LEGO |
| 1 MB – 32 MB | File system of the firmware of LEGO |

Songs are ordinary files in that same file system rather than a region of
their own. Taking a region would mean overwriting the file system, which the
firmware of LEGO would then most likely reformat, losing whatever the user had
stored. Sharing it keeps both firmwares working and destroys nothing.

littlefs comes from the MicroPython submodule, so the same file system can be
mounted. What this gives up: the firmware of LEGO may delete or overwrite the
songs, and it allocates from the same free space. Neither matters for a hub
used with Pybricks.

Measured on a hub used normally: the file system reports 31 MB with a fraction
of a megabyte in use. Sampling the raw chip finds data across two thirds of
it, but that is rubbish left behind — littlefs does not erase blocks when it
stops using them.

Reflashing is never at risk. The bootloader and firmware live in internal
flash, and the first 32 KB of it cannot be erased and is entered with a
button, so DFU is always available whatever the external flash holds.

## Reading a song while it plays

The chip is on SPI2 at 24 MHz, so about 3 MB/s, where a 96 kbit/s song needs
12 KB/s. Throughput is not the problem; the interface is. littlefs reads
synchronously while the flash driver is asynchronous, so a read has to be
waited out, which blocks everything else on the hub.

Reads are therefore kept to about 1 KB, roughly 0.35 ms, and taken in a step
of their own so that waiting is never added to the time spent decoding. The
decoder works from a window buffer that is refilled ahead of it.

## Decoding a granule at a time

A frame at 44.1 kHz took nearly five milliseconds to decode, and the control
loop runs every five, so decoding one could take a whole turn away from it.

A frame is made of granules — two at 44.1 kHz, one at 22.05 kHz, which is why
the higher rate costs twice as much. Preparing a frame and decoding each of
its granules are separate steps, so no step is longer than the single granule
that the lower rate already took. Everything a granule needs between steps
already lives in memory that lasts, and minimp3 is compiled into
`lib/pbio/src/sound_mp3.c`, so its own pieces are used without keeping a
changed copy of it.

Measured on a Prime Hub, same song, while Python counted loop turns:

| | 22.05 kHz, 24k | 22.05 kHz, 48k | 44.1 kHz, 96k |
| --- | --- | --- | --- |
| Processor used | 8.4% | 8.8% | 17.5% |
| Longest single decode | 2.20 ms | 2.36 ms | 2.32 ms |
| Longest background turn | 2.22 ms | 2.36 ms | 2.44 ms |
| Python slowed by | 10.4% | 11.0% | 20.8% |
| A loop pacing at 10 ms | 3.0% slower | — | 13.5% slower |
| Underruns | 0 | 0 | 0 |

Before the work above, 44.1 kHz showed 4.95 ms for its longest turn. The total
cost is unchanged by splitting; only the longest turn is.

## Starting part way through a song

Frames of a song encoded at a constant rate are all the same size, so the
position of any moment follows from the rate: parse one frame header, and the
offset is the time multiplied by it. minimp3 fills in the frame details when
given no output buffer, so this costs a header parse rather than a decode.
Landing mid-frame is fine, as the decoder scans forward to the next header.

Two traps. Encoders put a frame at the start of a file describing how the file
was made, and it is **not** always at the rate of the music — ffmpeg writes it
at 56 kbit/s whatever the song is, which sent a 24 kbit/s song more than twice
as far into the file as asked. The rate is therefore taken from the first frame
that carries music, and the offset is measured from where that frame begins
rather than from the start of the file. A song encoded at a varying rate would
only be found approximately this way.

## Talking to the computer

Two commands and one event, on top of the Pybricks protocol:

- `WRITE_FLASH_FILE` (8): open, append, close, remove. Refused while a song is
  playing or paused, since the file is read as it plays.
- `READ_FLASH_FILE` (9): list a directory, or read part of a file.
- `FLASH_FILE_DATA` (4): the answer, as a stream of pieces, because a command
  can only answer with an error code.

An answer goes to every connection at once, so it has to fit the smallest of
them: a USB packet holds 64 bytes at full speed against 244 for a Bluetooth
message. The size follows whichever connections are listening, so Bluetooth
keeps its bigger answers when nothing is on USB.

Printing and reading files share one notification buffer, since neither needs
to be quick while the other is working, and that pays for raising the limit to
what a single radio packet holds. Status reports keep buffers of their own so
they never queue behind bulk data.

## USB

USB is off unless a build asks for it with `PB_USB=1`. Upstream turned it off
for release builds, saying it was unfinished and could cause lockups in `lsusb`
or when writing stdout, and finished it afterwards in work that does not sit on
this release. Printing 126 KB over USB did not lock anything up here, and file
operations still worked afterwards, but that is one machine on one day against
a fault its authors called unexpected.

With USB off, the code behind the flag compiles to nothing: the firmware is
the same size, to the byte.

| | Bluetooth | USB |
| --- | --- | --- |
| Upload | 8.4 KB/s | 57.5 KB/s |
| Download | 6.5 KB/s | 21.7 KB/s |
| Printing 6 KB | 360 ms | 26 ms |

Downloads gain less than uploads because an answer has to fit one USB packet,
while an upload fills a whole message.

## Showing the music

A granule is held as 32 bands of frequency just before it becomes sound again,
so how loud each part of the range is costs a sum over numbers that are
already there. It is measured before the granule is turned into samples, which
reuses that memory.

Nothing is measured above band 24, near 16 kHz, because that is where encoders
stop keeping anything: including the empty bands above it only watered down
what the top part reported.

The numbers are the size of the sound in each part, scaled up by 65536 so they
survive being made whole — they are small fractions, around a ten thousandth.
Whoever shows them decides what counts as full. Music holds about sixty times
less at the top of the range than at the bottom, so the player weighs each
column against what an average song holds in that part before comparing them.

## Program storage

Ten slots. The size of one program is a fixed fifth of program storage
whatever the slot count, set by `PBSYS_CONFIG_STORAGE_PROGRAM_SIZE_DIVISOR`,
so fewer slots lowers the combined size rather than raising the limit for one.

Storage resets itself whenever the firmware hash changes, which is what makes
it safe to change the number of slots: the old layout can never be misread.
