"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3=1.

Description: A music player for the hub.

Put some songs on the hub first:

    pybricksdev files put ble -n "Hub C" song.mp3 /pybricks/songs/

Controls:

    center          play or pause
    left, right     tap to choose the song before or after this one
    left, right     hold to turn the volume down or up
    Bluetooth       stop this program

Songs named like "01 - Title-24k.mp3" and "01 - Title-96k.mp3" are the same
song at different qualities. Moving between those carries on from the same
moment instead of starting over, so the two can be compared by ear.

While a song plays the display shows how loud each part of the frequency
range is, low notes on the left and high on the right. It shows the number
of the song instead when the song changes or nothing is playing, and the
volume while that is being changed. A volume of 100 shows as > because the
display only holds two digits. The name of each song is printed as it is
chosen, so it can be read in Pybricks Code; without that, go by the number.

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
NUMBER_SHOWN = 1200  # how long the song number stays after it changes
DRAW_EVERY = 40  # how often to redraw the bars

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
number_until = 0
next_draw = 0

# The loudest each part of the range has been lately, so that the bars fill
# the display whatever the song and whatever it was encoded from.
loudest = [1, 1, 1, 1, 1]


def track_of(song):
    # Everything before the last dash is the song itself, so that the same
    # song at another quality can be recognised.
    name = name_of(song)
    cut = -1
    for i in range(len(name)):
        if name[i] == "-":
            cut = i
    return name[:cut] if cut > 0 else name


def name_of(song):
    # Only the last part of the path is worth reading out.
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


def show_levels():
    # A column for each part of the frequency range, filled from the bottom.
    global loudest, shown

    levels = hub.speaker.mp3_levels()

    # Music holds far less at the top of the range than at the bottom, so a
    # single scale would leave the right hand columns dark. Each column is
    # measured against its own loudest instead, and the quietest columns are
    # held back so that near silence does not fill them.
    for i in range(5):
        if levels[i] > loudest[i]:
            loudest[i] = levels[i]
        else:
            # Come down slowly, so a quiet passage fills the display only
            # after it has been quiet for a while rather than at once.
            loudest[i] = loudest[i] * 0.95 + levels[i] * 0.05

    floor = max(loudest) / 10
    rows = []
    for row in range(5):
        # The top row is lit only by the tallest bars.
        height = 5 - row
        lit = []
        for i in range(5):
            scale = loudest[i] if loudest[i] > floor else floor
            lit.append(100 if scale > 0 and levels[i] * 5 / scale >= height else 0)
        rows.append(lit)

    hub.display.icon(rows)
    shown = None


def select(at, play):
    global index

    was = songs[index]
    # Where the song has got to, before choosing another one loses it.
    at_ms = hub.speaker.mp3_position()
    was_paused = hub.speaker.mp3_paused()

    index = at % len(songs)
    song = songs[index]

    hub.speaker.select_mp3(song)
    print("{}. {}".format(index + 1, name_of(song)))
    show_number()

    if play:
        # The same song at another quality carries on from the same moment,
        # which is the only way to hear the difference between them.
        same = track_of(song) == track_of(was)
        hub.speaker.play_mp3(at_ms if same else 0)
    elif was_paused:
        # Choosing a song does not disturb one that is paused, so carrying on
        # would bring back the old song rather than this one.
        hub.speaker.stop_mp3()


def show_number():
    # The number is worth seeing for a moment when the song changes, then the
    # music itself is more interesting.
    global number_until

    show(index + 1)
    number_until = watch.time() + NUMBER_SHOWN


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

    # The hub knows whether a song is going, and it keeps going across
    # programs, so asking beats remembering: a song left playing by an
    # earlier run would otherwise not be recognised, and choosing another
    # song would rename what is heard without changing it.
    if hub.speaker.mp3_playing():
        playing = True

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

    # The volume, then the song number, then the music itself.
    if volume_until and watch.time() > volume_until:
        volume_until = 0
        show_number()

    if not volume_until and watch.time() > number_until and watch.time() >= next_draw:
        next_draw = watch.time() + DRAW_EVERY
        if hub.speaker.mp3_playing():
            show_levels()
        else:
            show(index + 1)

    # Nothing says when a song ends, so the player watches for it and goes on
    # to the next one.
    if playing and hub.speaker.mp3_stopped():
        select(index + 1, True)

    wait(POLL)
