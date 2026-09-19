"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3.

Description: Starts the embedded MP3 so that it keeps playing after this
program ends, then exits immediately.

Run this program, then run other programs from the hub buttons or from
Pybricks Code. The music should keep playing through all of them. Set STOP
to True and run it again to stop the music.
"""

from pybricks.hubs import PrimeHub

VOLUME = 25
STOP = False

hub = PrimeHub()

if STOP:
    hub.speaker.stop_mp3()
    print("Stopped.")
else:
    hub.speaker.volume(VOLUME)
    hub.speaker.play_mp3(loop=True, persist=True)
    print("Playing. This program ends now, but the music continues.")
