"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3.

Description: Measures the timing of experimental MP3 playback.

Plays the MP3 embedded in the firmware while this program waits, while it
runs Python code, and while it makes long calls that don't let background
tasks run. Then compares how fast Python code runs with and without audio.
"""

from pybricks.hubs import PrimeHub
from pybricks.tools import StopWatch, wait

# Volume to play at. The hub default is 100, which is loud for desk testing.
VOLUME = 25

# Whether to let the whole song finish, which takes about 2.5 minutes, to
# check that playback stops by itself at the end. The measurements above it
# take about 30 seconds either way.
WAIT_FOR_SONG_END = False

hub = PrimeHub()
watch = StopWatch()


def start_phase():
    # Starts a new measurement, so that the time spent printing the previous
    # results is not counted in the next phase.
    hub.speaker.mp3_stats(reset=True)


def report(phase):
    s = hub.speaker.mp3_stats()
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


def busy_python(ms):
    watch.reset()
    x = 1
    while watch.time() < ms:
        x = (x * 31 + 7) % 1000003


def blocking_call(n):
    # max() over a range runs entirely in C, without giving background tasks
    # a turn, so it shows what happens when the decoder is starved. It does
    # not add the values up, which would overflow.
    watch.reset()
    max(range(n))
    return watch.time()


def benchmark(ms):
    # Counts how many loop iterations run in a fixed time.
    watch.reset()
    count = 0
    while watch.time() < ms:
        count += 1
    return count


print("Benchmark without audio...")
base = benchmark(5000)

hub.speaker.volume(VOLUME)
hub.speaker.play_mp3()
s = hub.speaker.mp3_stats()
print(
    "Playing {} Hz, {} kbit/s, {} channel(s) at volume {}. Decoder memory: {} bytes.".format(
        s["sample_rate"], s["bitrate_kbps"], s["channels"], VOLUME, s["memory_bytes"]
    )
)

start_phase()
wait(10000)
report("Waiting for 10 s:")

start_phase()
busy_python(10000)
report("Busy Python loop for 10 s:")

# These should straddle the time that one buffer half lasts, so the last
# ones are expected to cause underruns.
for n in (30000, 60000, 120000, 300000):
    start_phase()
    ms = blocking_call(n)
    # Nothing runs in the background during the call above, so the decoder
    # only notices and reports missed deadlines once it runs again. Wait for
    # it to catch up before reading the results.
    wait(300)
    report("Blocking call of {} ms, plus 300 ms to recover:".format(ms))

start_phase()
playing = benchmark(5000)
report("Benchmark with audio:")

# Volume changes apply to sound that is already playing.
print("Changing volume while playing...")
for volume in (10, 60, VOLUME):
    hub.speaker.volume(volume)
    wait(1500)
print(
    "Benchmark: {} without audio, {} with audio ({:.1f}% slower)".format(
        base, playing, 100 * (base - playing) / base
    )
)

if WAIT_FOR_SONG_END:
    # Check that playback stops by itself at the end of the song.
    print("Waiting for the song to end...")
    while hub.speaker.mp3_state() == "playing":
        wait(1000)
    print("Done. Played {} half buffers.".format(hub.speaker.mp3_stats()["halves_played"]))
else:
    hub.speaker.stop_mp3()
    print("Stopped.")
