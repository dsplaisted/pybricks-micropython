"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3.

Description: Plays a song from a file on the external flash, rather than
from the firmware.

Upload a song to the hub first, then run this. It reports the same timings
as speaker_mp3.py, so the cost of reading from the flash can be compared
with playing from memory.
"""

from pybricks.hubs import PrimeHub
from pybricks.tools import StopWatch, wait

VOLUME = 25
# A name on its own means a song in the hub's songs folder.
SONG_NAME = "pybricks_song.mp3"

hub = PrimeHub()
watch = StopWatch()


def report(phase):
    s = hub.speaker.mp3_stats(reset=True)
    print(phase)
    print(
        "  decode per frame: avg {:.2f} ms, max {:.2f} ms".format(
            s["decode_us_avg"] / 1000, s["decode_us_max"] / 1000
        )
    )
    print("  CPU used by decoding: {:.1f}%".format(s["cpu_pct"]))
    print(
        "  slowest refill: {:.1f} ms of {:.1f} ms allowed".format(
            s["fill_latency_us_max"] / 1000, s["half_period_us"] / 1000
        )
    )
    print("  longest background step: {:.2f} ms".format(s["step_us_max"] / 1000))
    print("  underruns:", s["underruns"])


# The song has to be on the hub already. Put it there with:
#
#     pybricksdev files put ble song.mp3 /pybricks/songs/pybricks_song.mp3
#
songs = hub.speaker.mp3_songs()
if not [song for song in songs if str(song).endswith(SONG_NAME)]:
    print("{} is not on the hub. Songs found: {}".format(SONG_NAME, songs))
    raise SystemExit

hub.speaker.volume(VOLUME)

print("Playing {} from the flash...".format(SONG_NAME))
hub.speaker.select_mp3(SONG_NAME)
hub.speaker.mp3_repeat(True)
hub.speaker.play_mp3()

s = hub.speaker.mp3_stats(reset=True)
print(
    "{} Hz, {} kbit/s. Decoder memory: {} bytes.".format(
        s["sample_rate"], s["bitrate_kbps"], s["memory_bytes"]
    )
)

wait(10000)
report("Waiting for 10 s:")

watch.reset()
x = 1
while watch.time() < 10000:
    x = (x * 31 + 7) % 1000003
report("Busy Python loop for 10 s:")

# Reading the file happens in steps of its own, so the longest step should
# be about the same as when playing from memory.
print()
print("Playing for another 30 s to cover several reads...")
wait(30000)
report("After 30 s more:")

hub.speaker.stop_mp3()
print("Stopped.")
