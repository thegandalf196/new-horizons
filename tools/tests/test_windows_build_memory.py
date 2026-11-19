"""Deterministic host-memory sampling tests without a native API or threads."""
# SPDX-License-Identifier: GPL-2.0-or-later

import ctypes
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from tools.tests.test_windows_build_diagnostics import diagnostics
import windows_build_memory as memory


def sample(used, committed):
    return {'totalPhysicalBytes': 100, 'availablePhysicalBytes': 100 - used,
            'inUsePhysicalBytes': used, 'committedBytes': committed, 'commitLimitBytes': 200}


class WindowsBuildMemoryTest(unittest.TestCase):
    def test_native_page_counters_convert_to_bytes_without_since_boot_peak(self):
        def read(pointer, size):
            info = pointer._obj
            self.assertEqual(size, ctypes.sizeof(info))
            info.PageSize = 4096
            info.PhysicalTotal, info.PhysicalAvailable = 100, 25
            info.CommitTotal, info.CommitLimit, info.CommitPeak = 80, 150, 140
            return 1
        api = Mock(side_effect=read)
        with patch.object(ctypes, 'WinDLL', create=True, return_value=SimpleNamespace(GetPerformanceInfo=api)):
            result = memory.read_windows_host_memory()
        self.assertEqual(result, {'totalPhysicalBytes': 409600, 'availablePhysicalBytes': 102400,
                                  'inUsePhysicalBytes': 307200, 'committedBytes': 327680,
                                  'commitLimitBytes': 614400})
        self.assertNotIn('commitPeak', result)

    def test_samples_track_failures_intervals_and_independent_host_peaks(self):
        read = Mock(side_effect=[sample(20, 90), OSError('sample unavailable'), sample(60, 80)])
        sampler = memory.WindowsHostMemorySampler(read=read, clock=Mock(side_effect=[0, 0.5, 2]))
        for _ in range(3):
            sampler.sample()
        report = sampler.report
        self.assertEqual(report['sampleCount'], 2)
        self.assertEqual(report['failedSampleCount'], 1)
        self.assertEqual(report['missedIntervals'], 2)
        self.assertEqual(report['maximumObservedIntervalSeconds'], 1.5)
        self.assertEqual(report['peakInUsePhysicalSample']['inUsePhysicalBytes'], 60)
        self.assertEqual(report['peakCommittedSample']['committedBytes'], 90)
        self.assertIn('lower bounds', report['note'])

    def test_sampler_reuses_one_thread_and_stops_before_final_sample(self):
        thread = Mock()
        thread.is_alive.return_value = False
        sampler = memory.WindowsHostMemorySampler(read=Mock(return_value=sample(20, 80)))
        with patch.object(memory.threading, 'Thread', return_value=thread) as factory:
            sampler.start()
            sampler.start()
            report = sampler.finish()
        factory.assert_called_once()
        thread.start.assert_called_once()
        thread.join.assert_called_once_with(timeout=2)
        self.assertTrue(sampler.stop_event.is_set())
        self.assertTrue(report['samplerStopped'])
        self.assertEqual(report['sampleCount'], 2)

    def test_non_windows_hosts_do_not_touch_native_api_or_start_thread(self):
        with patch.object(memory.sys, 'platform', 'linux'), patch.object(memory, 'WindowsHostMemorySampler') as constructor:
            self.assertIsNone(memory.create_windows_memory_sampler())
        constructor.assert_not_called()

    def test_thread_resource_failure_is_diagnostic_only_and_cannot_change_build(self):
        thread = Mock()
        thread.start.side_effect = RuntimeError('thread unavailable')
        sampler = memory.WindowsHostMemorySampler(read=Mock(return_value=sample(20, 80)))
        with patch.object(memory.threading, 'Thread', return_value=thread):
            sampler.start()
            sampler.start()
            report = sampler.finish()
        self.assertTrue(report['samplerStartFailed'])
        self.assertTrue(report['samplerStopped'])
        thread.join.assert_not_called()

    def test_build_diagnostics_stops_sampler_and_retains_host_report_on_failure(self):
        import json
        from pathlib import Path
        import tempfile
        from tools.tests.test_windows_build_diagnostics import build
        sampler = Mock()
        sampler.finish.return_value = {'available': True, 'sampleCount': 2, 'scope': 'host'}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result = SimpleNamespace(returncode=19)
            with patch.object(diagnostics, 'create_windows_memory_sampler', return_value=sampler), \
                    patch.object(diagnostics.BuildDiagnostics, 'cache_snapshot', return_value={}), \
                    patch.object(build.subprocess, 'run', return_value=result):
                status = build.main(['build', '--build-root', str(root), '--diagnostics-dir', str(root / 'out'), '--jobs', '4'])
            self.assertEqual(status, 19)
            report = json.loads((root / 'out/build-diagnostics.json').read_text())
            self.assertEqual(report['jobs'], 4)
            self.assertEqual(report['hostMemory'], sampler.finish.return_value)
        sampler.start.assert_called_once()
        sampler.finish.assert_called_once()


if __name__ == '__main__':
    unittest.main()
