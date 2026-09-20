"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3.

Description: A music player for the hub.

Put some songs on the hub first:

    pybricksdev files put ble -n "Hub C" song.mp3 /pybricks/songs/

Controls:

    center          play or pause
    left, right     tap to choose the song before or after this one
    left, right     hold to turn the volume down or up
    Bluetooth       stop this program

The display shows the number of the song, and the volume while it is being
changed. A volume of 100 shows as > because the display only holds two
digits. The name of each song is printed as it is chosen, so it can be read
in Pybricks Code; without that, go by the number.

The music keeps playing after this program ends, and the song stays chosen,
so starting the player again carries on from where it was.
"""

from pybricks.hubs import PrimeHub
from pybricks.parameters import Button
from pybricks.tools import StopWatch, wait

FIRST_REPEAT = 250  # hold this long before the volume starts changing
REPEAT = 25  # time between steps after that
POLL = 5  # how often to check the buttons
VOLUME_SHOWN = 1000  # how long the volume stays on the display

hub = PrimeHub()

# The center button stops the program by default, which would end the player
# the first time it was used to pause. The Bluetooth button stops it instead.
hub.system.set_stop_button(Button.BLUETOOTH)

songs = hub.speaker.mp3_songs()

if not songs:
    print("No songs on the hub. Upload one to /pybricks/songs/ first.")
    raise SystemExit

# Songs end instead of repeating, so that the player can move on to the next
# one, and the music outlives this program.
hub.speaker.mp3_repeat(False)
hub.speaker.mp3_persist(True)

watch = StopWatch()

index = 0
shown = None
volume_until = 0


def name_of(song):
    # A song on the flash is a path, and a song built into the firmware is a
    # number. Only the last part of the path is worth reading out.
    if not isinstance(song, str):
        return "firmware song {}".format(song)

    name = song.split("/")[-1]
    if name.endswith(".mp3"):
        name = name[:-4]
    return name


def show(value):
    # Drawing the same thing again would restart the animation of a number
    # too wide for the display.
    global shown

    if value != shown:
        hub.display.number(value)
        shown = value


def select(at, play):
    global index

    index = at % len(songs)
    song = songs[index]

    hub.speaker.select_mp3(song)
    print("{}. {}".format(index + 1, name_of(song)))
    show(index + 1)

    if play:
        hub.speaker.play_mp3()


def set_volume(value):
    global volume_until

    hub.speaker.volume(min(100, max(0, value)))
    show(hub.speaker.volume())
    volume_until = watch.time() + VOLUME_SHOWN


print("{} songs. Center plays, left and right choose, hold for volume.".format(len(songs)))

# Carry on with the song that is already chosen, since the choice outlives
# the program that made it. Choosing it again does not disturb it if it is
# playing.
selected = hub.speaker.mp3_selected()
start = 0
for at, song in enumerate(songs):
    if song == selected:
        start = at

select(start, False)

# Music that is already playing keeps playing, so the player has to know
# about it to move on when the song ends.
playing = hub.speaker.mp3_playing()
held = 0
due = 0
repeated = False

while True:
    pressed = hub.buttons.pressed()

    if Button.CENTER in pressed:
        playing = hub.speaker.play_pause_mp3()
        print("playing" if playing else "paused")

        # Wait for the release, so that holding it does not toggle over and
        # over.
        while Button.CENTER in hub.buttons.pressed():
            wait(POLL)

    if Button.LEFT in pressed:
        step = -1
    elif Button.RIGHT in pressed:
        step = 1
    else:
        step = 0

    if step == 0:
        # A press that ends before the volume starts changing is a tap, which
        # means the song rather than the volume.
        if held and not repeated:
            select(index + held, playing)
        held = 0
    elif step != held:
        held = step
        repeated = False
        due = watch.time() + FIRST_REPEAT
    elif watch.time() >= due:
        repeated = True
        set_volume(hub.speaker.volume() + step)
        due = watch.time() + REPEAT

    # The song number comes back once the volume has been read.
    if volume_until and watch.time() > volume_until:
        volume_until = 0
        show(index + 1)

    # Nothing says when a song ends, so the player watches for it and goes on
    # to the next one.
    if playing and hub.speaker.mp3_stopped():
        select(index + 1, True)

    wait(POLL)
