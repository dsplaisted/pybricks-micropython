"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3=1.

Description: Lists the files that the firmware of LEGO keeps on the
external flash, and how full the file system is.

Read only: this program does not write or change anything.
"""

from pybricks.hubs import PrimeHub

KB = 1024
MB = 1024 * KB

MAX_DEPTH = 3

hub = PrimeHub()

used, total = hub.system.flash_usage()
print(
    "File system: {:.2f} MB used of {:.2f} MB ({:.0f}%), {:.2f} MB free.".format(
        used / MB, total / MB, 100 * used / total, (total - used) / MB
    )
)
print()


def walk(path, depth):
    entries = hub.system.flash_files(path)
    entries.sort()

    for name, size, is_dir in entries:
        indent = "  " * depth
        if is_dir:
            print("{}{}/".format(indent, name))
            if depth + 1 < MAX_DEPTH:
                walk((path + "/" + name).replace("//", "/"), depth + 1)
        else:
            if size >= KB:
                print("{}{:<24s} {:>8.1f} KB".format(indent, name, size / KB))
            else:
                print("{}{:<24s} {:>8d} B".format(indent, name, size))


walk("/", 0)
