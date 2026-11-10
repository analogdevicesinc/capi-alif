#!/usr/bin/env python3
#
# Copyright (c) 2026 Analog Devices, Inc.
#
# SPDX-License-Identifier: BSD-3-Clause
"""Flash one or more RTSS cores of a DK-E7 in a single Application TOC.

Alif's flow packages every image that the device should boot into one ATOC
and writes it in one go, so a multicore application is flashed as a unit
rather than core-by-core. This wraps that flow:

    1. generate an app-cfg JSON naming each core's .bin and MRAM address,
    2. run app-gen-toc to build the ATOC,
    3. put the target into maintenance mode (see below),
    4. run app-write-mram.

Two things about this board make step 3 more involved than it looks, both
consequences of the frozen SETOOLS Linux executables rather than of the
hardware:

  * The SEUART runs at 55000 baud, which the bundled pyserial cannot set on
    a CDC-ACM port -- so everything goes through alif_seuart_bridge.py.
    That bridge holds one fixed baud, so app-write-mram's default "bump to
    921600 mid-transfer" has to be disabled with -s.

  * app-write-mram normally resets the target itself to enter maintenance
    mode, over serial control lines the bridge cannot pass through. Once an
    application is in MRAM it boots fast enough to close the ISP window, so
    the write has to be preceded by the documented "hard maintenance mode"
    handshake: the maintenance tool waits, the operator presses RESET, and
    the SE stops the cores instead of booting them.

That last step needs a human at the board, so this script asks for the
press rather than pretending it can do it. With a J-Link, or with the
SETOOLS *source* release (which can drive the real port at 55000 and reset
over control lines), none of this ceremony is needed -- see README.md.
"""
import argparse
import json
import os
import pty
import select
import shutil
import subprocess
import sys
import time

# MRAM load addresses per core, from Alif's own DevKit-e7 example configs
# (Boards/DevKit-e7/Examples/*/.alif/*.json) -- these must match the
# APP_MRAM_*_BASE values the linker scripts use, or the image is written
# somewhere the core will not fetch it from.
CORE_MRAM_ADDR = {
    "rtss_hp": "0x80200000",
    "rtss_he": "0x80000000",
}
CORE_CPU_ID = {
    "rtss_hp": "M55_HP",
    "rtss_he": "M55_HE",
}


def staged_name(core, path):
    """Name this core's image uniquely inside SETOOLS' build/images/.

    Both cores of a multicore app are normally built from one source tree
    into per-core build directories, so their binaries share a basename
    ("blinky.bin" and "blinky.bin"). Staging them under that basename makes
    the second copy silently overwrite the first, and app-gen-toc then
    points BOTH core entries at the surviving file -- observed on real
    hardware as a clean write in which RTSS-HP was handed RTSS-HE's image
    and neither core ran. Prefixing with the core id keeps them distinct.
    """
    return f"{core}_{os.path.basename(path)}"


def build_toc_config(images, setools_dir):
    """Compose the app-gen-toc input.

    The A32 stub is always included: the boot chain expects an entry for
    every application core, and omitting it leaves the RTSS cores unstarted
    (observed as a successful write followed by nothing running at all).
    A core we are not flashing gets the vendor's stub so that it is still
    accounted for.
    """
    cfg = {
        "DEVICE": {
            "disabled": False,
            "binary": "app-device-config.json",
            "version": "0.5.00",
            "signed": True,
        },
        "A32_STUB": {
            "disabled": False,
            "binary": "a32_stub_0.bin",
            "version": "1.0.0",
            "signed": True,
            "loadAddress": "0x02000000",
            "cpu_id": "A32_0",
            "flags": ["load", "boot"],
        },
    }

    for core, _ in images.items():
        cfg[core.upper() + "_APP"] = {
            "disabled": False,
            "binary": staged_name(core, images[core]),
            "version": "1.0.0",
            "signed": False,
            "mramAddress": CORE_MRAM_ADDR[core],
            "cpu_id": CORE_CPU_ID[core],
            "flags": ["boot"],
        }

    # Any RTSS core without a real image still gets the vendor stub.
    stub_for = {"rtss_hp": "m55_stub_hp.bin", "rtss_he": "m55_stub_he.bin"}
    stub_addr = {"rtss_hp": "0x50000000", "rtss_he": "0x58000000"}
    for core in ("rtss_hp", "rtss_he"):
        if core in images:
            continue
        stub_path = os.path.join(setools_dir, "build", "images", stub_for[core])
        if not os.path.isfile(stub_path):
            continue
        cfg[core.upper() + "_STUB"] = {
            "disabled": False,
            "binary": stub_for[core],
            "version": "1.0.0",
            "signed": True,
            "loadAddress": stub_addr[core],
            "cpu_id": CORE_CPU_ID[core],
            "flags": ["load", "boot"],
        }

    return cfg


def arm_maintenance_and_wait_for_reset(setools_dir, pts_path, timeout_s=120):
    """Enter SETOOLS' "hard maintenance mode" and wait for a physical reset.

    Must NOT let `maintenance` exit through its own menu (e.g. by feeding it
    pre-buffered "return to top menu, then Enter to exit" keystrokes): doing
    that is what this function replaced, because it made every write that
    followed fail with "Target did not respond" -- confirmed twice on real
    hardware, immediately after the reset was correctly caught (the tool's
    own "Select an option (Enter to return)" prompt reappeared). The
    working theory is that a menu-driven exit sends the SE a real command
    that resumes/boots the target, closing the ISP window before
    app-write-mram gets to it -- whereas killing the process with SIGTERM
    right after the catch, with no further protocol exchange, is exactly
    what worked by hand earlier in this platform's own bring-up.

    So: start `maintenance`, select Device Control -> Hard maintenance mode
    (the two keystrokes that begin the wait), poll its output for the
    "Select an option (Enter to return)" prompt reappearing (the signal
    that it caught the reset), and the instant that shows up, terminate the
    process outright -- never send it another keystroke.

    `maintenance` is run against a PTY, not a plain pipe: this frozen
    PyInstaller binary's C library block-buffers stdout instead of line-
    buffering it once stdout is not a TTY, so its own prompts (including the
    "Waiting for Target"/catch text this function polls for) can sit in its
    internal buffer for many seconds -- observed directly on real hardware
    as a run where the reset was pressed and caught by the SE, but never
    surfaced to this loop until long after the ISP window had closed. A PTY
    makes `isatty()` true for the child, restoring its normal line-buffered
    behavior.
    """
    master_fd, slave_fd = pty.openpty()
    proc = subprocess.Popen(
        ["./maintenance", "-c", pts_path, "-b", "115200"],
        cwd=setools_dir, stdin=slave_fd, stdout=slave_fd, stderr=slave_fd,
        close_fds=True,
    )
    os.close(slave_fd)
    try:
        os.write(master_fd, b"1\n1\n")

        deadline = time.time() + timeout_s
        buf = ""
        caught = False
        while time.time() < deadline:
            r, _, _ = select.select([master_fd], [], [], 1.0)
            if not r:
                continue
            try:
                chunk = os.read(master_fd, 256).decode(errors="replace")
            except OSError:
                break
            if not chunk:
                break
            buf += chunk

            # The exact same "Select an option (Enter to return)" prompt is
            # printed BEFORE arming (the Hard/Soft/Device-reset submenu we
            # just navigated into) and AGAIN after a reset is caught -- and
            # output arrives in bursts, so a single read can contain both
            # occurrences already concatenated. A plain "in buf" check
            # cannot tell them apart and matched the pre-arm one here on
            # the first attempt at this fix, exiting instantly with no
            # reset ever pressed. The fix is to require the catch-prompt to
            # appear STRICTLY AFTER the first "Waiting for Target" text --
            # so as soon as that spinner is seen at all, drop everything
            # up to and including it, and only look at what comes next.
            wait_idx = buf.find("Waiting for Target")
            if wait_idx != -1:
                buf = buf[wait_idx:]
                if "Select an option (Enter to return)" in buf:
                    print("[flash] reset caught -- stopping maintenance mode", flush=True)
                    caught = True
                    break
        if not caught:
            print("[flash] warning: timed out waiting for a reset", file=sys.stderr, flush=True)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        os.close(master_fd)

    # Give the SE a brief moment after the port closes before the next tool
    # opens it -- observed to matter empirically, same as the working
    # by-hand sequence's own small pause here.
    time.sleep(1.5)


def main():
    # This script's own stdout is fully block-buffered whenever it is not a
    # TTY (piped through ninja/cmake into a log, for instance), which would
    # delay the "press RESET" banner below by an unpredictable amount.
    # Force line buffering so status always appears as it happens.
    sys.stdout.reconfigure(line_buffering=True)
    sys.stderr.reconfigure(line_buffering=True)

    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--setools-dir", required=True)
    p.add_argument("--bridge-script", required=True)
    p.add_argument("--device", default="/dev/ttyACM0")
    p.add_argument("--baud", default="55000")
    p.add_argument("--rtss-hp", help="path to the RTSS-HP .bin")
    p.add_argument("--rtss-he", help="path to the RTSS-HE .bin")
    p.add_argument("--peer-rtss-hp", help="RTSS-HP .bin to include if it has been built")
    p.add_argument("--peer-rtss-he", help="RTSS-HE .bin to include if it has been built")
    p.add_argument("--assume-maintenance", action="store_true",
                   help="skip the reset handshake (target is already in "
                        "maintenance mode, e.g. straight after a previous write)")
    args = p.parse_args()

    images = {}
    if args.rtss_hp:
        images["rtss_hp"] = args.rtss_hp
    if args.rtss_he:
        images["rtss_he"] = args.rtss_he
    if not images:
        print("error: give at least one of --rtss-hp / --rtss-he", file=sys.stderr)
        return 2

    # Every write replaces the whole ATOC, so a core left out of this run is
    # stubbed and stops running -- flashing the two cores of one application
    # one after the other therefore never leaves both alive. The build system
    # passes the *other* core's expected image here so that a core already
    # built alongside this one is carried along instead of being stubbed.
    for core, peer in (("rtss_hp", args.peer_rtss_hp), ("rtss_he", args.peer_rtss_he)):
        if core in images or not peer:
            continue
        if os.path.isfile(peer):
            images[core] = peer
            print(f"[flash] including already-built {core}: {peer}")
        else:
            print(f"[flash] note: {core} not built -- it will be stubbed and will not run")

    setools = os.path.abspath(args.setools_dir)
    img_dir = os.path.join(setools, "build", "images")
    os.makedirs(img_dir, exist_ok=True)

    # app-gen-toc resolves `binary` names relative to build/images/, so the
    # images have to be copied in rather than referenced where they were built.
    for core, path in images.items():
        if not os.path.isfile(path):
            print(f"error: {core} image not found: {path}", file=sys.stderr)
            return 1
        shutil.copy(path, os.path.join(img_dir, staged_name(core, path)))

    cfg = build_toc_config(images, setools)
    cfg_path = os.path.join(setools, "build", "config", "capi-alif-app-cfg.json")
    os.makedirs(os.path.dirname(cfg_path), exist_ok=True)
    with open(cfg_path, "w") as f:
        json.dump(cfg, f, indent=4)
    print(f"[flash] cores: {', '.join(sorted(images))}")

    r = subprocess.run(["./app-gen-toc", "-f", cfg_path, "-o", "build/AppTocPackage.bin"],
                       cwd=setools)
    if r.returncode:
        print("error: app-gen-toc failed", file=sys.stderr)
        return r.returncode

    # The macOS SETOOLS opens the SEUART itself and resets into maintenance mode; the bridge is Linux-only.
    if sys.platform == "darwin":
        r = subprocess.run(["./app-write-mram", "-c", args.device, "-b", args.baud], cwd=setools)
        if r.returncode:
            print("error: app-write-mram failed", file=sys.stderr)
        return r.returncode

    bridge = subprocess.Popen([sys.executable, args.bridge_script, args.device, args.baud],
                              stdout=subprocess.PIPE, stderr=sys.stderr, text=True)
    try:
        pts = bridge.stdout.readline().strip()
        if not pts:
            print("error: bridge did not report a PTY", file=sys.stderr)
            return 1
        print(f"[flash] SEUART {args.device}@{args.baud} bridged to {pts}")
        time.sleep(0.3)

        if not args.assume_maintenance:
            print()
            print("=" * 68)
            print("  Arming maintenance mode -- PRESS THE RESET BUTTON ON THE BOARD.")
            print("  (the cores must be stopped before MRAM can be rewritten)")
            print("=" * 68)
            print()
            arm_maintenance_and_wait_for_reset(setools, pts)

        r = subprocess.run(["./app-write-mram", "-c", pts, "-b", "115200", "-s", "-nr"],
                           cwd=setools)
        if r.returncode:
            print("error: app-write-mram failed -- if it reported 'Target did not "
                  "respond', the ISP window closed; re-run and press RESET when asked",
                  file=sys.stderr)
            return r.returncode
    finally:
        bridge.terminate()
        try:
            bridge.wait(timeout=2)
        except subprocess.TimeoutExpired:
            bridge.kill()

    print()
    print("[flash] done -- press RESET to boot the newly written image(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
