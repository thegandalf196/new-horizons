#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Normal XTest input on an identity-guarded private :191 Xvfb; no fallback.

Usage: nh-private-input.py --guard LOCAL_JSON click X Y key Escape wait 1
The tester records pid/start_ticks/boot_id/socket_inode when starting owned Xvfb.
Never create a guard by discovering an arbitrary existing display.
"""
import ctypes as c
import json
import os
from pathlib import Path
import sys
import time

args = sys.argv[1:]
if len(args) < 2 or args.pop(0) != '--guard':
    raise SystemExit('Explicit owned-Xvfb --guard JSON required')
guard = json.loads(Path(args.pop(0)).read_text())


def check_owner():
    proc = Path('/proc') / str(int(guard['pid']))
    stat = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
    command = (proc / 'cmdline').read_bytes().split(b'\0')
    owned = (
        proc.stat().st_uid == os.getuid()
        and stat[0] != 'Z'
        and stat[19] == str(guard['start_ticks'])
        and Path('/proc/sys/kernel/random/boot_id').read_text().strip() == guard['boot_id']
        and Path(os.fsdecode(command[0])).name == 'Xvfb'
        and command[1] == b':191'
        and b'-nolisten' in command
        and command[command.index(b'-nolisten') + 1] == b'tcp'
        and Path('/tmp/.X11-unix/X191').stat().st_ino == guard['socket_inode']
    )
    if not owned:
        raise SystemExit('Owned private Xvfb identity changed; refusing input')


check_owner()
x = c.CDLL('libX11.so.6')
t = c.CDLL('libXtst.so.6')
x.XOpenDisplay.argtypes = [c.c_char_p]
x.XOpenDisplay.restype = c.c_void_p
x.XStringToKeysym.argtypes = [c.c_char_p]
x.XStringToKeysym.restype = c.c_ulong
x.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
x.XKeysymToKeycode.restype = c.c_uint
x.XFlush.argtypes = [c.c_void_p]
x.XCloseDisplay.argtypes = [c.c_void_p]
t.XTestFakeMotionEvent.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_int, c.c_ulong]
t.XTestFakeButtonEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
t.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
d = x.XOpenDisplay(b':191')
if not d:
    raise SystemExit('Private :191 unavailable; refusing fallback')
try:
    check_owner()
    while args:
        check_owner()
        action = args.pop(0)
        if action == 'click':
            px, py = int(args.pop(0)), int(args.pop(0))
            t.XTestFakeMotionEvent(d, 0, px, py, 0)
            t.XTestFakeButtonEvent(d, 1, 1, 0)
            t.XTestFakeButtonEvent(d, 1, 0, 0)
        elif action == 'rightdown':
            px, py = int(args.pop(0)), int(args.pop(0))
            t.XTestFakeMotionEvent(d, 0, px, py, 0)
            t.XTestFakeButtonEvent(d, 3, 1, 0)
        elif action == 'rightup':
            t.XTestFakeButtonEvent(d, 3, 0, 0)
        elif action == 'key':
            key = x.XKeysymToKeycode(d, x.XStringToKeysym(args.pop(0).encode()))
            if not key:
                raise ValueError('Unknown keysym')
            t.XTestFakeKeyEvent(d, key, 1, 0)
            t.XTestFakeKeyEvent(d, key, 0, 0)
        elif action == 'wait':
            time.sleep(float(args.pop(0)))
        else:
            raise ValueError(action)
        x.XFlush(d)
        time.sleep(0.15)
finally:
    x.XCloseDisplay(d)
