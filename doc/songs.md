# Preparing songs for the hub

Songs live in `/pybricks/songs` on the external flash. A name on its own is
taken to be a song in that folder, so `speaker.select_mp3("01 - Title.mp3")`
is enough. Paths are limited to 64 characters including the folder, which
leaves 47 for the name.

Send them with `pybricksdev files put`. Nothing has to be built into the
firmware.

## What to encode

MP3, layer 3. Stereo is mixed to mono as it plays, so encoding in mono spends
every bit on what is heard rather than on a channel that is thrown away.

The sample rate decides what playing a song costs, not the bitrate: a frame at
44.1 kHz holds twice the samples of one at 22.05 kHz. Measured on a Prime Hub
playing the same song, while Python counted loop turns:

| | 22.05 kHz, 24k | 22.05 kHz, 48k | 44.1 kHz, 96k |
| --- | --- | --- | --- |
| Processor used | 8.4% | 8.8% | 17.5% |
| Longest background turn | 2.22 ms | 2.36 ms | 2.44 ms |
| Python slowed by | 10.4% | 11.0% | 20.8% |
| A loop pacing at 10 ms | 3.0% slower | — | 13.5% slower |
| Underruns | 0 | 0 | 0 |

So 48k costs almost nothing over 24k, and 96k costs twice as much for the
higher sample rate that comes with it. Motor control is not affected either
way: it runs in the firmware, and no single piece of decoding takes longer
than half of the five milliseconds between its turns.

At 96 kbit/s mono a song takes 12 KB a second, so 37 minutes of music fills
about 25.5 MB. The file system holds about 31 MB, of which the firmware of
LEGO uses a fraction of a megabyte on some hubs and nearer three on others.

## Leave room below the top

The hub turns each sample into a whole number and cuts off anything past the
top, so a song that peaks above full scale is flattened there rather than
simply played loudly.

Encoding pushes the peaks higher than they went in: measured over these
songs, by 2.6 to 3.2 decibels. The usual practice of leaving one decibel of
room is therefore not enough. Holding the peaks four decibels below the top
before encoding lands the result just under it.

    ffmpeg -i in.m4a -map_metadata -1 -vn \
        -af "volume=6.4dB,alimiter=limit=0.63:level=false" \
        -ac 1 -ar 44100 -b:a 96k "01 - Title.mp3"

`level=false` matters: the limiter levels its output back up to the ceiling by
default, which puts back exactly the room being made.

Evening out the loudness of a set of songs, so that one is not much louder
than the next, is ordinary audio work: measure each with `loudnorm`, apply one
gain for the whole song, then limit as above. Worth doing if songs are chosen
during a session rather than set once.
