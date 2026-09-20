"""
Hardware Module: Prime Hub or Inventor Hub.

Description: Plays a series of tones to hear which frequencies the hub's
speaker actually reproduces.

Listen for which ones sound loud, quiet or inaudible. That tells you which
part of the audio matters, and therefore whether a higher quality recording
is worth its cost.
"""

from pybricks.hubs import PrimeHub
from pybricks.tools import wait

VOLUME = 60
DURATION = 700

FREQUENCIES = (
    100,
    150,
    200,
    300,
    400,
    600,
    800,
    1200,
    1600,
    2400,
    3200,
    4500,
    6000,
    8000,
    10000,
)

hub = PrimeHub()
hub.speaker.volume(VOLUME)

print("Each tone plays for {} ms at volume {}.".format(DURATION, VOLUME))
print("Note which ones you can hear, and how loud they seem.")

for frequency in FREQUENCIES:
    print("{} Hz".format(frequency))
    hub.speaker.beep(frequency, DURATION)
    wait(300)

print("Done.")
