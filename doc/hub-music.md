# Playing music on the hub

Experimental. A Prime Hub or Inventor Hub can play MP3 files kept on its
external flash, while a program runs. See `hub-music-internals.md` for how it
works and what it costs.

## Building the firmware

Needs the ARM toolchain and a Unix-like environment; building under WSL works.

    cd bricks/primehub
    make PB_MP3=1

Add `PB_USB=1` to talk to the hub over USB as well as Bluetooth, which sends
files about seven times faster. USB was unfinished in this release, so it is
off unless asked for. Run `make clean` when adding or removing either flag:
changing a flag alone does not make anything rebuild, and a build that skips
it keeps the setting it had without saying so.

Install `build/firmware.zip` through Pybricks Code, as with any firmware.
**Flashing clears the programs stored on the hub**, since the firmware resets
its storage whenever it changes.

## Getting the tools

Sending files needs the `files` command, which is on the `flash-files` branch
of pybricksdev rather than in a release:

    pip install -e path/to/pybricksdev

On Windows, USB also needs the hub bound to the WinUSB driver.

## Preparing songs

MP3, layer 3. Stereo is mixed to mono as it plays, so encoding in mono spends
every bit on what is heard. 96 kbit/s at 44.1 kHz sounds best of the settings
measured; 48 kbit/s at 22.05 kHz costs half as much of the processor and is
hard to fault.

**Leave four decibels below the top.** The hub turns each sample into a whole
number and cuts off anything past the top, and encoding pushes the peaks 2.6
to 3.2 decibels higher than they went in, so the usual one decibel of room is
not enough.

    ffmpeg -i in.m4a -map_metadata -1 -vn \
        -af "volume=6.4dB,alimiter=limit=0.63:level=false" \
        -ac 1 -ar 44100 -b:a 96k "01 - Title.mp3"

`level=false` matters: the limiter levels its output back up to the ceiling by
default, which puts back exactly the room being made. Pick the `volume` gain
so the loudest stretch of the song lands near the limiter rather than far
above it.

Evening out the loudness across a set of songs is ordinary audio work:
measure each with `loudnorm`, apply one gain for the whole song, then limit as
above. Worth doing if songs are chosen during a session rather than set once.

## Sending them to the hub

Songs live in `/pybricks/songs`. Names are limited to 47 characters, since a
path may be 64 including the folder. Numbering them keeps them in order, with
two digits so that 10 does not come before 2.

    pybricksdev files put usb "01 - Title.mp3" /pybricks/songs/
    pybricksdev files list usb /pybricks/songs
    pybricksdev files remove usb "/pybricks/songs/01 - Title.mp3"

Use `ble` in place of `usb` to go over Bluetooth, which is slower but needs no
driver. About 37 minutes of music at 96 kbit/s fills 25.5 MB, and the file
system holds about 31 MB, of which the firmware of LEGO uses between a
fraction of a megabyte and three.

**The hub refuses to write files while a song is playing or paused.** A paused
song counts, which is not obvious from the outside: a transfer that stops with
a busy error usually means something is still playing.

## Playing them

    hub.speaker.mp3_songs()          # what can be chosen
    hub.speaker.select_mp3("01 - Title.mp3")
    hub.speaker.play_mp3()           # from the start
    hub.speaker.play_mp3(30000)      # from 30 seconds in
    hub.speaker.play_pause_mp3()     # like a play/pause button
    hub.speaker.pause_mp3()
    hub.speaker.resume_mp3()
    hub.speaker.stop_mp3()

    hub.speaker.mp3_playing()        # and mp3_paused(), mp3_stopped()
    hub.speaker.mp3_selected()       # what is chosen, or None
    hub.speaker.mp3_position()       # how far in, in milliseconds
    hub.speaker.mp3_levels()         # loudness of five parts of the range
    hub.speaker.mp3_repeat(True)     # start again at the end
    hub.speaker.mp3_persist(True)    # keep playing when the program ends
    hub.speaker.mp3_stats()          # timings, for measuring

Which song is chosen, whether it repeats, whether it persists, and the volume
all outlive the program that set them, so a program can start a song and end
while it keeps playing. `mp3_persist` takes effect at the next `play_mp3`,
because it decides where the decoder keeps its memory.

`tests/pup/hub/speaker_mp3_player.py` is a music player built on this: the
centre button plays and pauses, left and right choose the song when tapped and
change the volume when held, and the display shows the music as five columns
of loudness from low notes to high.

## Program slots

Unrelated to music, but on the same branch: the Prime Hub has ten program
slots rather than five. The first five light one pixel on the bottom row of
the display; the next five light every pixel but one.
