"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3.

Description: Play/pause button for the embedded MP3, with volume control.

The center button plays, pauses and continues the song like a play/pause
button. Left and right change the volume, repeating when held. The volume
and the song both keep going after this program ends.
"""

from pybricks.hubs import PrimeHub
from pybricks.parameters import Button
from pybricks.tools import wait, StopWatch

FIRST_REPEAT = 250  # hold this long before the volume starts repeating
REPEAT = 25         # time between repeats after that
POLL = 5            # how often to check the buttons

hub = PrimeHub()

# The song, and how it plays, are chosen once. They stay chosen after this
# program ends, which is how the song keeps playing into the next one.
hub.speaker.select_mp3(0)
hub.speaker.mp3_repeat(True)
hub.speaker.mp3_persist(True)
watch = StopWatch()

held = 0
due = 0
shown = None

while True:
    pressed = hub.buttons.pressed()

    if Button.CENTER in pressed:
        playing = hub.speaker.play_pause_mp3()
        print("playing" if playing else "paused")
        # Wait for the release, so one press is one action.
        while Button.CENTER in hub.buttons.pressed():
            wait(POLL)
        continue

    if Button.LEFT in pressed:
        step = -1
    elif Button.RIGHT in pressed:
        step = 1
    else:
        step = 0

    if step == 0:
        held = 0
    elif step != held:
        held = step
        hub.speaker.volume(min(100, max(0, hub.speaker.volume() + step)))
        due = watch.time() + FIRST_REPEAT
    elif watch.time() >= due:
        hub.speaker.volume(min(100, max(0, hub.speaker.volume() + step)))
        due = watch.time() + REPEAT

    volume = hub.speaker.volume()
    if volume != shown:
        hub.display.number(volume)
        shown = volume

    wait(POLL)
