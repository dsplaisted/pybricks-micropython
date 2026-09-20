"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3.

Description: Writes to the file system on the external flash, which is
shared with the firmware of LEGO.

First writes a small file, reads it back, checks it and deletes it again.
Set STORE_SONG to also copy a song embedded in the firmware to a file,
which writes a few hundred kilobytes and reports how long that takes.

Writing is limited to the file system area, so the programs and settings
stored by Pybricks itself cannot be touched by this.
"""

from pybricks.hubs import PrimeHub
from pybricks.tools import StopWatch

KB = 1024
MB = 1024 * KB

TEST_PATH = "/pybricks_test.bin"
SONG_PATH = "/pybricks_song.mp3"

# Set to True to also store a song. This writes a few hundred kilobytes.
STORE_SONG = False

hub = PrimeHub()
watch = StopWatch()


def show_usage(label):
    used, total = hub.system.flash_usage()
    print("{}: {:.2f} MB used of {:.2f} MB.".format(label, used / MB, total / MB))


show_usage("Before")

# A pattern that is easy to check, and long enough to cover several pages.
data = bytes(range(256)) * 4

print("Writing {} bytes to {}...".format(len(data), TEST_PATH))
watch.reset()
hub.system.flash_write_file(TEST_PATH, data)
print("Wrote in {} ms.".format(watch.time()))

read_back = hub.system.flash_read_file(TEST_PATH, 0, len(data))
print("Read back {} bytes. Matches: {}".format(len(read_back), read_back == data))

part = hub.system.flash_read_file(TEST_PATH, 300, 4)
print("Bytes 300 to 304: {}, expected {}".format(part, data[300:304]))

names = [name for name, size, is_dir in hub.system.flash_files("/")]
print("In the root: {}".format(names))

hub.system.flash_remove(TEST_PATH)
names = [name for name, size, is_dir in hub.system.flash_files("/")]
print("After deleting: {}".format(names))

show_usage("After")

if STORE_SONG:
    size = None
    print()
    print("Storing song 0 as {}...".format(SONG_PATH))
    watch.reset()
    hub.speaker.store_mp3(0, SONG_PATH)
    elapsed = watch.time()

    for name, file_size, is_dir in hub.system.flash_files("/"):
        if "/" + name == SONG_PATH:
            size = file_size

    if size is None:
        print("The song is not there. Something went wrong.")
    else:
        print(
            "Stored {:.1f} KB in {} ms ({:.0f} KB/s).".format(
                size / KB, elapsed, size / KB / (elapsed / 1000)
            )
        )
    show_usage("After storing")

print()
print("Done. Run flash_files.py to check that the files of LEGO are intact.")
