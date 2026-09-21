"""
Hardware Module: Prime Hub or Inventor Hub, with firmware built with PB_MP3=1.

Description: Reads the external flash to see what other firmwares store
there, before deciding where songs could go. Read only: this program does
not write anything.

Prints what kind of file system is at the 1 MB mark, and samples the whole
chip to see which parts are in use.
"""

from pybricks.hubs import PrimeHub

KB = 1024
MB = 1024 * KB

# Where the file system of LEGO's firmware starts.
FS_START = 1 * MB
CHIP_SIZE = 32 * MB

# One sample per this much, to see which parts hold data.
SAMPLE_STEP = 64 * KB
SAMPLE_SIZE = 32

hub = PrimeHub()


def hex_dump(data, address):
    for i in range(0, len(data), 16):
        row = data[i : i + 16]
        hexed = " ".join("{:02x}".format(b) for b in row)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
        print("  {:08x}  {:<47s}  {}".format(address + i, hexed, text))


def identify(block, address):
    # littlefs v2 keeps "littlefs" at offset 8, v1 at offset 40.
    if block[8:16] == b"littlefs":
        return "littlefs v2"
    if block[40:48] == b"littlefs":
        return "littlefs v1"
    # FAT has a signature at the end of the boot sector.
    if block[510:512] == b"\x55\xaa":
        return "FAT"
    if all(b == 0xFF for b in block):
        return "erased"
    return "unknown"


print("Reading the start of the file system area...")
for block in range(2):
    address = FS_START + block * 4 * KB
    data = hub.system.flash_read(address, 512)
    print("At {} KB: {}".format(address // KB, identify(data, address)))
    hex_dump(data[:64], address)

print()
print("Sampling the whole chip, one read every {} KB...".format(SAMPLE_STEP // KB))

used = 0
total = 0
highest = None
first_used_above_fs = None

# One character per sample: # holds data, . is erased. Each line covers 4 MB.
PER_LINE = 64
line = ""

for address in range(0, CHIP_SIZE, SAMPLE_STEP):
    data = hub.system.flash_read(address, SAMPLE_SIZE)
    total += 1
    if all(b == 0xFF for b in data):
        line += "."
    else:
        line += "#"
        used += 1
        highest = address
        if first_used_above_fs is None and address >= FS_START:
            first_used_above_fs = address

    if len(line) == PER_LINE:
        start = address + SAMPLE_STEP - PER_LINE * SAMPLE_STEP
        print("{:5.1f} MB  {}".format(start / MB, line))
        line = ""

if line:
    print("        {}".format(line))

print()
print("{} of {} samples hold data.".format(used, total))
if highest is not None:
    print("Highest sample with data: {} KB ({:.1f} MB).".format(highest // KB, highest / MB))
if first_used_above_fs is not None:
    print("First sample with data in the file system area: {} KB.".format(first_used_above_fs // KB))
print()
print("Note that a sample only looks at {} bytes, so this shows roughly".format(SAMPLE_SIZE))
print("which parts are in use, not exactly how much is stored.")
