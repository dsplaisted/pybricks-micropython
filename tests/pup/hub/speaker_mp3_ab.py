"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with two
MP3 files, the same clip at a low and a higher bitrate.

Description: Compares recording quality on the hub's own speaker.

Left button plays the first file, right button plays the second, center
stops. Both are the same 45 second clip, so switching between them plays
the same music from the same point.

Listen from where you would during a match, not with your ear next to the
hub, and try it at the volume you would actually use.
"""

from pybricks.hubs import PrimeHub
from pybricks.parameters import Button
from pybricks.tools import wait

VOLUME = 40

hub = PrimeHub()
hub.speaker.volume(VOLUME)

print("{} songs embedded. Left = first, right = second, center = stop.".format(hub.speaker.mp3_count()))

while True:
    pressed = hub.buttons.pressed()

    if Button.LEFT in pressed:
        hub.speaker.play_mp3(song=0, loop=True)
        print("song 0")
    elif Button.RIGHT in pressed:
        hub.speaker.play_mp3(song=1, loop=True)
        print("song 1")
    elif Button.CENTER in pressed:
        hub.speaker.stop_mp3()
        print("stopped")

    # Wait for the release, so one press is one action.
    while hub.buttons.pressed():
        wait(10)

    wait(10)
