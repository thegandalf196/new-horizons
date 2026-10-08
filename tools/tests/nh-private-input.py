#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Normal XTest input on an identity-guarded private :191 Xvfb; no fallback.

Usage: nh-private-input.py --guard LOCAL_JSON move X Y click X Y key Escape wait 1
       nh-private-input.py --guard LOCAL_JSON text 'vcmiskill new-horizons:learning 1'
The tester records pid/start_ticks/boot_id/socket_inode when starting owned Xvfb.
Never create a guard by discovering an arbitrary existing display.
"""
import ctypes as c
import json
import os
from pathlib import Path
import sys
import time

def check_owner(guard):
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


_PUNCTUATION = {
    ' ': ('space', False),
    '-': ('minus', False), '_': ('minus', True),
    '=': ('equal', False), '+': ('equal', True),
    '[': ('bracketleft', False), '{': ('bracketleft', True),
    ']': ('bracketright', False), '}': ('bracketright', True),
    ';': ('semicolon', False), ':': ('semicolon', True),
    "'": ('apostrophe', False), '"': ('apostrophe', True),
    ',': ('comma', False), '<': ('comma', True),
    '.': ('period', False), '>': ('period', True),
    '/': ('slash', False), '?': ('slash', True),
    '\\': ('backslash', False), '|': ('backslash', True),
    '`': ('grave', False), '~': ('grave', True),
}


def ascii_text_plan(value):
    """Explicit US-ASCII key/Shift plan; controls and non-ASCII are refused."""
    if len(value) > 4096:
        raise ValueError('Text exceeds bounded 4096-character input limit')
    result = []
    shifted_digits = ')!@#$%^&*('
    for char in value:
        if 'a' <= char <= 'z' or '0' <= char <= '9':
            result.append((char, False))
        elif 'A' <= char <= 'Z':
            result.append((char.lower(), True))
        elif char in shifted_digits:
            result.append((str(shifted_digits.index(char)), True))
        elif char in _PUNCTUATION:
            result.append(_PUNCTUATION[char])
        else:
            raise ValueError('Text accepts only printable ASCII characters')
    return result


def send_text(value, x, t, display, guard_check):
    """Preflight the complete text before events; release each key/Shift finally."""
    plan = ascii_text_plan(value)
    keys = []
    for name, shifted in plan:
        key = x.XKeysymToKeycode(display, x.XStringToKeysym(name.encode('ascii')))
        if not key:
            raise ValueError('ASCII text key unavailable on private display')
        keys.append((key, shifted))
    shift = 0
    if any(shifted for _, shifted in keys):
        shift = x.XKeysymToKeycode(display, x.XStringToKeysym(b'Shift_L'))
        if not shift:
            raise ValueError('Shift unavailable on private display')
    for key, shifted in keys:
        # This connection was opened only on verified :191. Recheck identity
        # before each character's event batch; cleanup releases only that batch.
        guard_check()
        try:
            if shifted and not t.XTestFakeKeyEvent(display, shift, 1, 0):
                raise RuntimeError('Private Shift press failed')
            if not t.XTestFakeKeyEvent(display, key, 1, 0):
                raise RuntimeError('Private text key press failed')
        finally:
            try:
                t.XTestFakeKeyEvent(display, key, 0, 0)
            finally:
                try:
                    if shifted:
                        t.XTestFakeKeyEvent(display, shift, 0, 0)
                finally:
                    x.XFlush(display)


def parse_actions(args):
    """Validate all text before any earlier action can send input."""
    widths = {'move': 2, 'click': 2, 'rightdown': 2, 'rightup': 0,
              'key': 1, 'text': 1, 'wait': 1}
    result = []
    index = 0
    while index < len(args):
        action = args[index]
        if action not in widths:
            raise ValueError(action)
        end = index + widths[action] + 1
        if end > len(args):
            raise ValueError('Missing private input action argument')
        values = args[index + 1:end]
        if action == 'text':
            ascii_text_plan(values[0])
        result.append((action, values))
        index = end
    return result


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)
    if len(args) < 2 or args.pop(0) != '--guard':
        raise SystemExit('Explicit owned-Xvfb --guard JSON required')
    guard = json.loads(Path(args.pop(0)).read_text())
    actions = parse_actions(args)
    check_owner(guard)
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
        check_owner(guard)
        for action, values in actions:
            check_owner(guard)
            apply_action(action, values, x, t, d, lambda: check_owner(guard))
            x.XFlush(d)
            time.sleep(0.15)
    finally:
        x.XCloseDisplay(d)
    return 0


def apply_action(action, values, x, t, d, guard_check):
    if action == 'move':
        px, py = map(int, values)
        t.XTestFakeMotionEvent(d, 0, px, py, 0)
    elif action == 'click':
        px, py = map(int, values)
        t.XTestFakeMotionEvent(d, 0, px, py, 0)
        t.XTestFakeButtonEvent(d, 1, 1, 0)
        t.XTestFakeButtonEvent(d, 1, 0, 0)
    elif action == 'rightdown':
        px, py = map(int, values)
        t.XTestFakeMotionEvent(d, 0, px, py, 0)
        t.XTestFakeButtonEvent(d, 3, 1, 0)
    elif action == 'rightup':
        t.XTestFakeButtonEvent(d, 3, 0, 0)
    elif action == 'key':
        key = x.XKeysymToKeycode(d, x.XStringToKeysym(values[0].encode()))
        if not key:
            raise ValueError('Unknown keysym')
        t.XTestFakeKeyEvent(d, key, 1, 0)
        t.XTestFakeKeyEvent(d, key, 0, 0)
    elif action == 'text':
        send_text(values[0], x, t, d, guard_check)
    elif action == 'wait':
        time.sleep(float(values[0]))
    else:
        raise ValueError(action)


if __name__ == '__main__':
    raise SystemExit(main())
