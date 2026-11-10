#!/usr/bin/env python3
#
# Copyright (c) 2026 Analog Devices, Inc.
#
# SPDX-License-Identifier: BSD-3-Clause
"""Bridge a real serial device to a local PTY.

Alif's SETOOLS (app-write-mram, maintenance, updateSystemPackage, ...) are
distributed as frozen PyInstaller binaries whose bundled pyserial crashes
with termios.error: (22, 'Invalid argument') when opening a port at the
SEUART's non-standard baud rates (55000/100000, set via the Linux
TCSETS2/BOTHER custom-baud mechanism). The system pyserial handles the same
rates, and the crash reproduces on a bare software PTY, so it is the bundled
copy rather than the hardware or the driver.

Standard baud rates (e.g. 115200) work fine in the same binary, and a PTY
accepts any termios settings without a crash (no real UART clock to
validate against). So: this script opens the real device and sets the true
custom baud itself (the working code path), then exposes a PTY slave that
SETOOLS opens at an arbitrary standard baud (its own baud setting on the
PTY side is a no-op) while this process relays raw bytes between the two.

Usage: alif_seuart_bridge.py <real-device> <real-baud>
  Prints the PTY slave path on stdout, then blocks relaying bytes.
  Point SETOOLS at the printed path with any standard -b/--baudrate value,
  e.g.: maintenance -c <printed-pts-path> -b 115200 -opt getbanner

KNOWN LIMITATION -- read before using this for app-write-mram. This bridge
pins the real device at one fixed baud, but app-write-mram by default
"bumps" the link to 921600 mid-session for the data transfer (see the
SETOOLS user guide, "Dynamic Baud Rate Change"), which the real device
then follows and this bridge does not -- the link dies mid-write. Pass
app-write-mram's '-s' flag to disable that bump when going through here.
Reading/observing an application's own console output does NOT involve
this bridge at all: that is a separate UART (see capi-alif's README).

THE PROPER FIX is to not need this script: SETOOLS also ships as a source
release (user guide section 2, "Source code release" -- needs `pip install
pyqt5 cryptography pyserial pylink-square`), and system pyserial handles
55000 baud correctly, which is exactly what the frozen build fails at.
Prefer the source release where it is available; this bridge exists only
to make the frozen Linux executables usable on a CDC-ACM port.
"""
import os
import pty
import select
import sys
import termios
import fcntl
import struct
import array

REAL_DEV = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 55000

# --- Linux-specific: set custom baud on the real device via TCSETS2/BOTHER ---
TCGETS2 = 0x802C542A
TCSETS2 = 0x402C542B
BOTHER = 0o010000
CBAUD = 0o010017

# struct termios2 layout (x86_64 linux):
# c_iflag, c_oflag, c_cflag, c_lflag: unsigned int (4 each)
# c_line: unsigned char (1)
# c_cc[19]: unsigned char
# padding to align c_ispeed
# c_ispeed, c_ospeed: unsigned int
TERMIOS2_FMT = "IIIIB19sII"

fd = os.open(REAL_DEV, os.O_RDWR | os.O_NOCTTY)

buf = fcntl.ioctl(fd, TCGETS2, b"\x00" * struct.calcsize(TERMIOS2_FMT))
iflag, oflag, cflag, lflag, line, cc, ispeed, ospeed = struct.unpack(TERMIOS2_FMT, buf)

cflag &= ~CBAUD
cflag |= BOTHER
cflag |= termios.CS8 | termios.CREAD | termios.CLOCAL
iflag = 0
oflag = 0
lflag = 0
ispeed = BAUD
ospeed = BAUD

new_buf = struct.pack(TERMIOS2_FMT, iflag, oflag, cflag, lflag, line, cc, ispeed, ospeed)
fcntl.ioctl(fd, TCSETS2, new_buf)

print(f"[bridge] {REAL_DEV} configured at {BAUD} baud (BOTHER)", file=sys.stderr)

# --- Create the PTY pair ---
master_fd, slave_fd = pty.openpty()
slave_name = os.ttyname(slave_fd)
print(f"[bridge] PTY slave ready: {slave_name}", file=sys.stderr)
print(slave_name)
sys.stdout.flush()

# Put the pty master side in raw mode too (doesn't matter much, but keep clean)
try:
    attrs = termios.tcgetattr(master_fd)
    attrs[3] &= ~(termios.ECHO | termios.ICANON)
    termios.tcsetattr(master_fd, termios.TCSANOW, attrs)
except Exception as e:
    print(f"[bridge] warn: could not set master pty raw mode: {e}", file=sys.stderr)

print("[bridge] relaying... (Ctrl-C to stop)", file=sys.stderr)

try:
    while True:
        r, _, _ = select.select([fd, master_fd], [], [], 1.0)
        if fd in r:
            try:
                data = os.read(fd, 4096)
            except OSError:
                break
            if data:
                os.write(master_fd, data)
        if master_fd in r:
            try:
                data = os.read(master_fd, 4096)
            except OSError:
                break
            if data:
                os.write(fd, data)
except KeyboardInterrupt:
    pass
finally:
    os.close(fd)
    os.close(master_fd)
    os.close(slave_fd)
