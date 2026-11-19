"""Sample Windows host memory during diagnostic builds, including cache children."""
# SPDX-License-Identifier: GPL-2.0-or-later

from datetime import datetime, timezone
import sys
import threading
import time


def read_windows_host_memory():
    # PERFORMANCE_INFORMATION counts pages. CommitPeak is since boot and must
    # not be mistaken for this build's peak, so deliberately do not publish it.
    import ctypes

    class PerformanceInformation(ctypes.Structure):
        _fields_ = [('cb', ctypes.c_uint32)] + [
            (name, ctypes.c_size_t) for name in (
                'CommitTotal', 'CommitLimit', 'CommitPeak', 'PhysicalTotal',
                'PhysicalAvailable', 'SystemCache', 'KernelTotal', 'KernelPaged',
                'KernelNonpaged', 'PageSize')
        ] + [(name, ctypes.c_uint32) for name in ('HandleCount', 'ProcessCount', 'ThreadCount')]

    api = ctypes.WinDLL('psapi', use_last_error=True).GetPerformanceInfo
    api.argtypes = [ctypes.POINTER(PerformanceInformation), ctypes.c_uint32]
    api.restype = ctypes.c_int
    info = PerformanceInformation()
    info.cb = ctypes.sizeof(info)
    if not api(ctypes.byref(info), info.cb):
        raise ctypes.WinError(ctypes.get_last_error())
    page = info.PageSize
    return {'totalPhysicalBytes': info.PhysicalTotal * page,
            'availablePhysicalBytes': info.PhysicalAvailable * page,
            'inUsePhysicalBytes': (info.PhysicalTotal - info.PhysicalAvailable) * page,
            'committedBytes': info.CommitTotal * page,
            'commitLimitBytes': info.CommitLimit * page}


class WindowsHostMemorySampler:
    def __init__(self, read=read_windows_host_memory, interval=0.5, clock=time.perf_counter):
        self.read, self.interval, self.clock = read, interval, clock
        self.stop_event = threading.Event()
        self.thread = None
        self.previous = None
        self.report = {
            'available': False, 'scope': 'Windows host during compiler stages, including sccache and unrelated processes',
            'method': 'GetPerformanceInfo', 'requestedIntervalSeconds': interval,
            'sampleCount': 0, 'failedSampleCount': 0, 'missedIntervals': 0,
            'maximumObservedIntervalSeconds': 0, 'errors': [],
            'note': 'Sampled host peaks are lower bounds, not exact process-tree peaks or memory attributed to this build.',
        }

    def sample(self):
        now = self.clock()
        if self.previous is not None:
            elapsed = now - self.previous
            self.report['maximumObservedIntervalSeconds'] = max(self.report['maximumObservedIntervalSeconds'], elapsed)
            self.report['missedIntervals'] += max(0, int(elapsed / self.interval) - 1)
        self.previous = now
        try:
            sample = self.read()
        except (OSError, ValueError) as error:
            self.report['failedSampleCount'] += 1
            if len(self.report['errors']) < 3:
                self.report['errors'].append(str(error))
            return
        sample = dict(sample, utc=datetime.now(timezone.utc).isoformat())
        self.report['available'] = True
        self.report['sampleCount'] += 1
        self.report.setdefault('firstSample', sample)
        self.report['lastSample'] = sample
        for field, key in (('inUsePhysicalBytes', 'peakInUsePhysicalSample'),
                           ('committedBytes', 'peakCommittedSample')):
            if key not in self.report or sample[field] > self.report[key][field]:
                self.report[key] = sample

    def start(self):
        if self.thread is not None or self.stop_event.is_set():
            return
        self.sample()
        self.thread = threading.Thread(target=self.loop, name='build-memory-sampler', daemon=True)
        try:
            self.thread.start()
        except RuntimeError as error:
            self.thread = None
            self.stop_event.set()
            self.report['samplerStartFailed'] = True
            self.report['errors'].append(str(error))

    def loop(self):
        while not self.stop_event.wait(self.interval):
            self.sample()

    def finish(self):
        self.stop_event.set()
        if self.thread is not None:
            self.thread.join(timeout=2)
            self.report['samplerStopped'] = not self.thread.is_alive()
        else:
            self.report['samplerStopped'] = True
        if self.report['samplerStopped']:
            self.sample()
        return self.report


def create_windows_memory_sampler():
    # No thread or native-library lookup on other hosts, including Wine hosts.
    return WindowsHostMemorySampler() if sys.platform == 'win32' else None
