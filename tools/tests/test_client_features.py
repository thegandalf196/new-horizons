# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic marker/build-script tests; no linked client is executed."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from client_features import FILENAME, client_feature_files, feature_payload


class ClientFeaturesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.client = self.root / 'new-horizons'
        self.client.write_bytes(b'Synthetic linked image; never executed')
        self.marker = self.root / FILENAME
        self.digest = hashlib.sha256(self.client.read_bytes()).hexdigest()

    def test_markerless_legacy_client_has_no_advertisement(self):
        self.assertEqual(client_feature_files(self.client), {})

    def test_exact_bound_marker_and_mismatch(self):
        self.marker.write_bytes(feature_payload(self.digest))
        self.assertEqual(client_feature_files(self.client), {FILENAME: self.marker})
        self.client.write_bytes(b'Historical rollback fixture')
        with self.assertRaisesRegex(RuntimeError, 'differs from the selected client'):
            client_feature_files(self.client)

    def test_malformed_oversized_and_symlink_markers_refuse(self):
        for payload in (b'', b'unsupported', feature_payload('0'*64), feature_payload(self.digest)+b' '*1024, feature_payload(self.digest)+b'\0'):
            with self.subTest(payload=payload[:20]):
                self.marker.write_bytes(payload)
                with self.assertRaises(RuntimeError):client_feature_files(self.client)
        self.marker.unlink()
        self.marker.symlink_to(self.client)
        with self.assertRaises(RuntimeError):client_feature_files(self.client)

    def test_cmake_producer_emits_bound_marker_without_executing_image(self):
        script = Path(__file__).resolve().parents[2] / 'cmake_modules/WriteNewHorizonsClientFeatures.cmake'
        result = subprocess.run(['/usr/bin/cmake', '-DNH_BUG_REPORTER_TARGET=vcmiclient',
                                 '-DNH_CLIENT='+str(self.client), '-P', str(script)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.marker.read_bytes(), feature_payload(self.digest))
        self.assertEqual(client_feature_files(self.client), {FILENAME:self.marker})
        self.assertFalse((self.root/(FILENAME+'.tmp')).exists())

    def test_cmake_producer_refuses_missing_target_or_nonregular_client(self):
        script = Path(__file__).resolve().parents[2] / 'cmake_modules/WriteNewHorizonsClientFeatures.cmake'
        for target, client in (('other', self.client), ('vcmiclient', self.root),
                               ('vcmiclient', self.root/'missing')):
            with self.subTest(target=target, client=client):
                result = subprocess.run(['/usr/bin/cmake', '-DNH_BUG_REPORTER_TARGET='+target,
                                         '-DNH_CLIENT='+str(client), '-P', str(script)], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.marker.exists())

    def test_cmake_producer_preserves_preexisting_temporary_objects(self):
        script = Path(__file__).resolve().parents[2]/'cmake_modules/WriteNewHorizonsClientFeatures.cmake'
        sentinel = self.root/'preserved sentinel'
        sentinel.write_bytes(b'Do not overwrite')
        temp = self.root/(FILENAME+'.tmp')
        for kind in ('symlink', 'dangling', 'directory', 'regular'):
            with self.subTest(kind=kind):
                if kind in ('symlink','dangling'):
                    temp.symlink_to(sentinel if kind=='symlink' else self.root/'absent target')
                elif kind=='directory':temp.mkdir()
                else:temp.write_bytes(b'Existing temporary file')
                result = subprocess.run(['/usr/bin/cmake','-DNH_BUG_REPORTER_TARGET=vcmiclient',
                                         '-DNH_CLIENT='+str(self.client),'-P',str(script)],capture_output=True)
                self.assertNotEqual(result.returncode,0)
                self.assertEqual(sentinel.read_bytes(),b'Do not overwrite')
                self.assertFalse(self.marker.exists())
                if kind=='directory':temp.rmdir()
                else:temp.unlink()

    def test_install_rebinds_transformed_image_and_respects_prefix_destdir(self):
        source = Path(__file__).resolve().parents[2]
        template = (source/'cmake_modules/InstallNewHorizonsClientFeatures.cmake.in').read_text()
        for index, (bin_dir, destdir) in enumerate((('bin', ''), ('bin', 'dest'), ('/usr/bin', 'dest'))):
            with self.subTest(bin_dir=bin_dir, destdir=destdir):
                work = self.root/str(index)
                work.mkdir()
                prefix = work/'install prefix'
                stage = str(work/destdir) if destdir else ''
                directory = Path(stage+str(Path(bin_dir) if Path(bin_dir).is_absolute() else prefix/bin_dir))
                directory.mkdir(parents=True)
                installed = directory/'new-horizons'
                installed.write_bytes(self.client.read_bytes()+b'changed installed RPATH or strip')
                configured = work/'install.cmake'
                configured.write_text(template.replace('@BIN_DIR@',bin_dir).replace('@CMAKE_SOURCE_DIR@',str(source)))
                import os
                env = dict(os.environ, DESTDIR=stage)
                result = subprocess.run(['/usr/bin/cmake','-DCMAKE_INSTALL_PREFIX='+str(prefix),'-P',str(configured)],env=env,capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual((directory/FILENAME).read_bytes(),feature_payload(hashlib.sha256(installed.read_bytes()).hexdigest()))
                self.assertNotEqual((directory/FILENAME).read_bytes(),feature_payload(self.digest))

    def test_target_advertisement_is_guarded_by_compiled_report_entry_source(self):
        source = (Path(__file__).resolve().parents[2]/'clientapp/CMakeLists.txt').read_text()
        start = source.index('if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND "BugReportBootstrap.cpp" IN_LIST clientapp_SRCS)')
        body = source[start:source.index('endif()', start)]
        self.assertIn('add_custom_command(TARGET vcmiclient POST_BUILD',body)
        self.assertIn('-DNH_BUG_REPORTER_TARGET=vcmiclient',body)
        self.assertIn('WriteNewHorizonsClientFeatures.cmake',body)
        self.assertNotIn('nhCureClientIntegration POST_BUILD',body)
        self.assertLess(source.index('install(TARGETS vcmiclient DESTINATION ${BIN_DIR})'), source.index('install(SCRIPT'))


if __name__ == '__main__':unittest.main()
