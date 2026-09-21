"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3=1
and the same song uploaded at several qualities.

Description: Compares what each quality costs to play.

Plays the same song at each quality while running Python code, and reports
how much of the processor the decoder took, how long its longest turn was,
and how much it slowed the Python code down. Run it to decide which quality
is worth using, once the difference in sound has been judged by ear.
"""

from pybricks.hubs import PrimeHub
from pybricks.tools import StopWatch, wait

# The song to compare, and the qualities it was uploaded at. The names are
# what the files are called, without the quality or the extension.
SONG = "03 - Dueling Banjos"
TIERS = ("24k", "48k", "96k")

# Volume to play at. The hub default is 100, which is loud for desk testing.
VOLUME = 15

# How long to measure each quality for.
PERIOD = 8000

hub = PrimeHub()
hub.speaker.volume(VOLUME)
hub.speaker.mp3_repeat(True)
hub.speaker.mp3_persist(False)

watch = StopWatch()


def count_work(ms):
    # Counts how many turns of a loop run in a fixed time, which says how
    # much of the processor is left for a program.
    watch.reset()
    count = 0
    x = 1
    while watch.time() < ms:
        x = (x * 31 + 7) % 1000003
        count += 1
    return count


# A single measurement of the quiet hub drifts, enough to make a quality
# look free or even helpful, so each one is compared with a quiet stretch
# measured right before it.
print("Measuring each quality against a quiet run just before it.")
print()

for tier in TIERS:
    song = "{}-{}.mp3".format(SONG, tier)

    try:
        hub.speaker.select_mp3(song)
    except Exception:
        print("{}: not on the hub, skipped".format(tier))
        continue

    hub.speaker.stop_mp3()
    wait(200)
    alone = count_work(PERIOD)

    hub.speaker.play_mp3()

    # Let it settle, then measure from a clean slate so that starting up is
    # not counted against it.
    wait(500)
    hub.speaker.mp3_stats(reset=True)

    turns = count_work(PERIOD)
    s = hub.speaker.mp3_stats()

    print("{}: {} Hz, {} kbit/s".format(tier, s["sample_rate"], s["bitrate_kbps"]))
    # The average is for a whole frame, while the longest is for one piece
    # of decoding, so the average is the larger of the two once a frame is
    # decoded in pieces.
    print("  decode per frame: {:.2f} ms".format(s["decode_us_avg"] / 1000))
    print("  longest single decode: {:.2f} ms".format(s["decode_us_max"] / 1000))
    print("  processor used by decoding: {:.1f}%".format(s["cpu_pct"]))
    print(
        "  longest background turn: {:.2f} ms, refill allowed {:.0f} ms".format(
            s["step_us_max"] / 1000, s["half_period_us"] / 1000
        )
    )
    print(
        "  slowest refill: {:.1f} ms, underruns: {}".format(
            s["fill_latency_us_max"] / 1000, s["underruns"]
        )
    )
    print(
        "  Python slowed by {:.1f}% ({} turns against {})".format(
            100 * (alone - turns) / alone, turns, alone
        )
    )
    print()

hub.speaker.stop_mp3()
hub.speaker.mp3_repeat(False)
print("Done.")
