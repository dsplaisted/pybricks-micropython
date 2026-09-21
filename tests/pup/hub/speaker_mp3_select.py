"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3=1
and at least one MP3 file uploaded to the external flash.

Description: Checks the song selection API.

Lists the songs, plays a little of each, and checks that the settings for
repeating and persisting are remembered and reported.
"""

from pybricks.hubs import PrimeHub
from pybricks.tools import wait

# Volume to play at. The hub default is 100, which is loud for desk testing.
VOLUME = 25

# How long to play each song for.
SAMPLE = 3000

hub = PrimeHub()
hub.speaker.volume(VOLUME)

songs = hub.speaker.mp3_songs()
print("Songs:", songs)

# Nothing should be chosen at the start of the very first run, but a song
# chosen by an earlier program is still chosen, so this only reports it.
print("Selected at start:", hub.speaker.mp3_selected())

# The settings are separate from the song and stay as they are set.
hub.speaker.mp3_repeat(True)
hub.speaker.mp3_persist(False)
print("Repeat:", hub.speaker.mp3_repeat(), "Persist:", hub.speaker.mp3_persist())

if hub.speaker.mp3_repeat() is not True:
    print("FAIL: repeat was not remembered")
if hub.speaker.mp3_persist() is not False:
    print("FAIL: persist was not remembered")

# Choosing a song that is not there should be refused, leaving the previous
# choice alone.
for bad in ("no_such_song.mp3", "/nowhere/no_such_song.mp3"):
    try:
        hub.speaker.select_mp3(bad)
        print("FAIL: selecting", bad, "was allowed")
    except Exception as e:
        print("Refused", bad, "->", type(e).__name__)

for song in songs:
    hub.speaker.select_mp3(song)
    print("Playing", hub.speaker.mp3_selected())

    if hub.speaker.mp3_selected() != song:
        print("FAIL: selection was not reported back")

    hub.speaker.play_mp3()
    wait(SAMPLE)

    if not hub.speaker.mp3_playing():
        print("FAIL: not playing")

    # The play/pause method takes no arguments now, so it needs the song to
    # have been chosen already.
    hub.speaker.play_pause_mp3()
    if not hub.speaker.mp3_paused():
        print("FAIL: not paused")

    hub.speaker.play_pause_mp3()
    if not hub.speaker.mp3_playing():
        print("FAIL: did not continue")

    hub.speaker.stop_mp3()
    if not hub.speaker.mp3_stopped():
        print("FAIL: not stopped")

# Playing again without choosing anything uses whatever was chosen last.
hub.speaker.play_mp3()
wait(SAMPLE)
print("Replayed", hub.speaker.mp3_selected())
hub.speaker.stop_mp3()

hub.speaker.mp3_repeat(False)
print("Done.")
