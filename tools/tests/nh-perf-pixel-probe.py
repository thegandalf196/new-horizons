#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded XTest -> changed Xvfb-root ROI probe; NOT physical scanout/frame timing.

Requires freshly recorded owned :191 display guard and client identity JSON
(pid, start_ticks, window). No display discovery/fallback or focus changes.
The caller must inspect the affected region before/after and attribute the change;
unrelated animation makes a sample invalid. A stable pre-input ROI is required.
"""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--guard', required=True, type=Path)
p.add_argument('--client', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--roi', required=True, nargs=4, type=int, metavar=('X', 'Y', 'W', 'H'))
p.add_argument('--click', required=True, nargs=2, type=int)
p.add_argument('--timeout', type=float, default=5)
p.add_argument('--interval-ms', type=float, default=10)
a = p.parse_args()
if not 0 < a.timeout <= 30 or not 5 <= a.interval_ms <= 100:
    p.error('Timeout must be <=30s, polling interval 5..100ms')
if min(a.roi) < 0 or not 0 < a.roi[2] * a.roi[3] <= 16384:
    p.error('ROI must be nonnegative, nonempty, <=16384 pixels')
if a.output.exists():
    p.error('Output already exists; refusing input or overwrite')
if a.roi[0] + a.roi[2] > 1280 or a.roi[1] + a.roi[3] > 800 or not (0 <= a.click[0] < 1280 and 0 <= a.click[1] < 800):
    p.error('Coordinates exceed the declared private 1280x800 display')
g = json.loads(a.guard.read_text())
game = json.loads(a.client.read_text())
window = int(str(game['window']), 0)

def identity(pid, ticks):
    root = Path('/proc') / str(pid)
    fields = (root / 'stat').read_text().rsplit(')', 1)[1].split()
    if root.stat().st_uid != os.getuid() or fields[0] == 'Z' or fields[19] != str(ticks):
        raise RuntimeError('Owned process identity changed')
    return root, fields

def check():
    root, _ = identity(g['pid'], g['start_ticks'])
    cmd = (root / 'cmdline').read_bytes().split(b'\0')
    if (Path(os.fsdecode(cmd[0])).name != 'Xvfb' or cmd[1] != b':191'
            or b'-nolisten' not in cmd or cmd[cmd.index(b'-nolisten') + 1] != b'tcp'
            or Path('/tmp/.X11-unix/X191').stat().st_ino != g['socket_inode']
            or Path('/proc/sys/kernel/random/boot_id').read_text().strip() != g['boot_id']):
        raise RuntimeError('Private display guard mismatch')
    return identity(game['pid'], game['start_ticks'])

class Image(c.Structure):
    _fields_ = [('width', c.c_int), ('height', c.c_int), ('xoffset', c.c_int),
                ('format', c.c_int), ('data', c.c_void_p), ('byte_order', c.c_int),
                ('bitmap_unit', c.c_int), ('bitmap_bit_order', c.c_int),
                ('bitmap_pad', c.c_int), ('depth', c.c_int),
                ('bytes_per_line', c.c_int), ('bits_per_pixel', c.c_int)]

check()
x = c.CDLL('libX11.so.6')
t = c.CDLL('libXtst.so.6')
for name, args, result in [
    ('XOpenDisplay', [c.c_char_p], c.c_void_p),
    ('XDefaultRootWindow', [c.c_void_p], c.c_ulong),
    ('XGetInputFocus', [c.c_void_p, c.POINTER(c.c_ulong), c.POINTER(c.c_int)], c.c_int),
    ('XGetImage', [c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint, c.c_ulong, c.c_int], c.POINTER(Image)),
    ('XDestroyImage', [c.POINTER(Image)], c.c_int),
    ('XFlush', [c.c_void_p], c.c_int), ('XSync', [c.c_void_p, c.c_int], c.c_int),
    ('XCloseDisplay', [c.c_void_p], c.c_int),
]:
    fn = getattr(x, name); fn.argtypes = args; fn.restype = result
for name, args in [
    ('XTestFakeMotionEvent', [c.c_void_p, c.c_int, c.c_int, c.c_int, c.c_ulong]),
    ('XTestFakeButtonEvent', [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]),
]:
    fn = getattr(t, name); fn.argtypes = args; fn.restype = c.c_int

d = x.XOpenDisplay(b':191')
if not d:
    raise SystemExit('Private display unavailable')
record = {'clock': 'Python monotonic_ns/CLOCK_MONOTONIC; wall mapping bracket below',
          'roi': a.roi, 'click': a.click, 'interval_ms': a.interval_ms,
          'timeout_s': a.timeout, 'samples': [], 'status': 'not_injected',
          'limits': 'XGetImage root observation, not physical display or every frame; changed ROI needs independent attribution'}
record['wall_mapping'] = [time.monotonic_ns(), time.time_ns(), time.monotonic_ns()]

def pixels():
    start = time.monotonic_ns()
    img = x.XGetImage(d, root_window, *a.roi, c.c_ulong(-1).value, 2)
    if not img:
        raise RuntimeError('XGetImage failed')
    try:
        value = hashlib.sha256(c.string_at(img.contents.data, img.contents.bytes_per_line * img.contents.height)).hexdigest()
    finally:
        x.XDestroyImage(img)
    return start, time.monotonic_ns(), value

try:
    focus, revert = c.c_ulong(), c.c_int()
    x.XGetInputFocus(d, c.byref(focus), c.byref(revert))
    if focus.value != window:
        raise RuntimeError('Expected owned client window is not focused; no input sent')
    root_window = x.XDefaultRootWindow(d)
    # Move pointer before establishing stability; cursor itself is not captured.
    t.XTestFakeMotionEvent(d, 0, *a.click, 0); x.XSync(d, 0)
    warm = []
    for _ in range(25):
        check(); warm.append(pixels()); time.sleep(a.interval_ms / 1000)
    record['warmup'] = warm
    if len({sample[2] for sample in warm}) != 1:
        raise RuntimeError('ROI changed before input; cannot attribute subsequent change')
    _, stat = check()
    record['proc_before'] = {'utime': stat[11], 'stime': stat[12], 'rss_pages': stat[21],
                             'ticks_per_second': os.sysconf('SC_CLK_TCK')}
    before = warm[-1][2]
    record['injection_begin_ns'] = time.monotonic_ns()
    t.XTestFakeButtonEvent(d, 1, 1, 0); t.XTestFakeButtonEvent(d, 1, 0, 0); x.XFlush(d)
    record['injection_flush_return_ns'] = time.monotonic_ns()
    record['status'] = 'timeout'
    deadline = time.monotonic() + a.timeout
    while time.monotonic() < deadline:
        check()
        sample = pixels(); record['samples'].append(sample)
        if sample[2] != before:
            record['status'] = 'changed_needs_attribution'
            # This is an observation bracket, not a sub-frame presentation timestamp.
            prior = record['samples'][-2][0] if len(record['samples']) > 1 else record['injection_begin_ns']
            record['change_bracket_ns'] = [prior, sample[1]]
            break
        time.sleep(a.interval_ms / 1000)
    _, stat = check()
    record['proc_after'] = {'utime': stat[11], 'stime': stat[12], 'rss_pages': stat[21]}
except Exception as exc:
    record['error'] = str(exc)
    record['status'] = 'invalid'
finally:
    record['finish_ns'] = time.monotonic_ns()
    x.XCloseDisplay(d)
    with a.output.open('x') as output:
        json.dump(record, output, indent=2); output.write('\n')
print(record['status'])
raise SystemExit(0 if record['status'] == 'changed_needs_attribution' else 1)
