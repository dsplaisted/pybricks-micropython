"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3=1.

Description: Writes to the file system on the external flash, which is
shared with the firmware of LEGO.

First writes a small file, reads it back, checks it and deletes it again.
Set BIG_WRITE to also write a few hundred kilobytes, the size of a song,
and report how fast that goes.

Writing is limited to the file system area, so the programs and settings
stored by Pybricks itself cannot be touched by this.
"""

from pybricks.hubs import PrimeHub
from pybricks.tools import StopWatch

KB = 1024
MB = 1024 * KB

TEST_PATH = "/pybricks_test.bin"
BIG_PATH = "/pybricks/songs/pybricks_test_big.bin"

# Set to True to also write a big file, about the size of a song.
BIG_WRITE = False
BIG_SIZE = 128 * KB

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

if BIG_WRITE:
    print()
    print("Writing {:.0f} KB to {}...".format(BIG_SIZE / KB, BIG_PATH))

    # The folder is made as needed, so this also checks that a file can be
    # written somewhere that does not exist yet.
    big = data * (BIG_SIZE // len(data))
    watch.reset()
    hub.system.flash_write_file(BIG_PATH, big)
    elapsed = watch.time()

    print(
        "Wrote {:.1f} KB in {} ms ({:.0f} KB/s).".format(
            len(big) / KB, elapsed, len(big) / KB / (elapsed / 1000)
        )
    )

    hub.system.flash_remove(BIG_PATH)
    show_usage("After the big write")

print()
print("Done. Run flash_files.py to check that the files of LEGO are intact.")
