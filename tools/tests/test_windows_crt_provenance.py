import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from windows_crt_provenance import bind_runtime, preflight_runtime, sha256
import windows_crt_provenance as provenance


class WindowsCRTProvenanceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.enterContext(patch.dict(os.environ, {'VCToolsRedistDir': '14.51.36231'}))
        self.vs = self.root / 'VS'
        self.selected = self.vs / 'VC/Redist/MSVC/14.29.30133'
        self.source = self.selected / 'x64/Microsoft.VC142.CRT/vcruntime140.dll'
        self.source.parent.mkdir(parents=True)
        self.source.write_bytes(b'actual runtime bytes')
        self.package = self.root / 'package'
        self.notice = self.package / 'licenses/Microsoft/PROVENANCE.json'
        self.notice.parent.mkdir(parents=True)
        terms = self.notice.parent / 'retained.docx'
        terms.write_bytes(b'full retained terms fixture')
        self.notice.write_text(json.dumps({'terms': [{'file': terms.name, 'sha256': sha256(terms)}],
            'notice_environment_redist_directory_version': '14.51.36231'}))
        (self.package / self.source.name).write_bytes(self.source.read_bytes())
        self.cache = self.root / 'CMakeCache.txt'
        self.cache.write_text(f'MSVC_REDIST_DIR:PATH={self.selected}\n')

    def test_actual_runtime_is_distinct_from_notice_environment(self):
        with patch('windows_crt_provenance.file_version', return_value='14.29.30157.0'):
            records = bind_runtime(self.package, self.root, self.vs)
        data = json.loads(self.notice.read_text())
        self.assertEqual(data['notice_environment_redist_directory_version'], '14.51.36231')
        self.assertEqual(records[0]['file_version'], '14.29.30157.0')
        self.assertEqual(records[0]['matching_vs_relative_sources'],
                         ['VC/Redist/MSVC/14.29.30133/x64/Microsoft.VC142.CRT/vcruntime140.dll'])
        self.assertNotIn(str(self.root), self.notice.read_text())

    def test_changed_runtime_rejects_before_version_parsing(self):
        self.source.write_bytes(b'different runtime')
        with patch('windows_crt_provenance.file_version') as parse:
            with self.assertRaisesRegex(RuntimeError, 'does not match'):
                bind_runtime(self.package, self.root, self.vs)
            parse.assert_not_called()

    def test_missing_or_external_selected_root_rejects(self):
        for text in ('', f'MSVC_REDIST_DIR:PATH={self.root}\n'):
            self.cache.write_text(text)
            with self.assertRaises(RuntimeError):
                bind_runtime(self.package, self.root, self.vs)

    def test_precompile_inventory_is_metadata_only(self):
        with patch('windows_crt_provenance.file_version', return_value='14.29.30157.0'):
            data = preflight_runtime(self.root, self.vs)
        self.assertEqual(data['runtimes'][0]['file_version'], '14.29.30157.0')
        self.assertNotIn(str(self.root), json.dumps(data))
        self.assertEqual(len(data['runtimes']), 1)

    def test_precompile_empty_or_unreviewed_family_rejects(self):
        with patch('windows_crt_provenance.file_version', return_value='15.0.0.0'):
            with self.assertRaises(RuntimeError):
                preflight_runtime(self.root, self.vs)
        self.source.unlink()
        with self.assertRaises(RuntimeError):
            preflight_runtime(self.root, self.vs)

    def test_changed_terms_reject_binding(self):
        (self.notice.parent / 'retained.docx').write_bytes(b'changed')
        with patch('windows_crt_provenance.file_version', return_value='14.29.30157.0'):
            with self.assertRaisesRegex(RuntimeError, 'terms do not match'):
                bind_runtime(self.package, self.root, self.vs)

    def test_workflow_installs_parser_before_real_crt_preflight(self):
        workflow = (Path(__file__).resolve().parents[2] / '.github/workflows/new-horizons-windows.yml').read_text()
        install = workflow.index("python -m pip install 'conan>=2.25,<3' 'pefile==2024.8.26'")
        smoke = workflow.index('python -c "import pefile; assert pefile.__version__')
        gate = workflow.index('- name: Verify CMake-selected CRT bytes before compiling')
        self.assertLess(install, smoke)
        self.assertLess(smoke, workflow.index('- name: Package audit regression tests'))
        self.assertLess(smoke, gate)

    def test_missing_runtime_rejects(self):
        (self.package / self.source.name).unlink()
        with self.assertRaisesRegex(RuntimeError, 'No shipped'):
            bind_runtime(self.package, self.root, self.vs)

    def prepare_reviewed_profile(self):
        self.vs = self.root / '18/Enterprise'
        self.selected = self.vs / provenance.REVIEWED_PROVIDER
        self.redist = self.vs / 'VC/Redist/MSVC' / provenance.REVIEWED_ENVIRONMENT
        self.redist.mkdir(parents=True)
        self.sdk = self.root / 'sdk'
        self.sdk_notice = self.sdk / 'Licenses/10.0.26100.0/sdk_license.rtf'
        self.sdk_notice.parent.mkdir(parents=True)
        self.notice_files = {}
        for name in provenance.REVIEWED_NOTICE_SHA256:
            target = self.notice.parent / name
            target.write_bytes(('reviewed notice ' + name).encode())
            self.notice_files[name] = sha256(target)
        redist_notice = self.vs / 'Licenses/1033/Redist.txt'
        redist_notice.parent.mkdir(parents=True)
        redist_notice.write_bytes((self.notice.parent / 'VS2026-Redist.txt').read_bytes())
        self.sdk_notice.write_bytes((self.notice.parent / 'SDK-10.0.26100.0-license.rtf').read_bytes())
        runtime_pins = {}
        for name in provenance.REVIEWED_RUNTIME_SHA256:
            target = self.selected / 'x64/Microsoft.VC142.CRT' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(('reviewed runtime ' + name).encode())
            (self.package / name).write_bytes(target.read_bytes())
            runtime_pins[name] = sha256(target)
        self.cache.write_text(f'MSVC_REDIST_DIR:PATH={self.selected}\n')
        terms = [{'file': name, 'sha256': digest} for name, digest in self.notice_files.items() if name.endswith('.docx')]
        self.notice.write_text(json.dumps({'terms': terms,
            'notice_environment_redist_directory_version': provenance.REVIEWED_ENVIRONMENT}))
        self.enterContext(patch.dict(os.environ, {'VCToolsRedistDir': str(self.redist),
            'VCToolsVersion': '14.29.30133', 'ProgramFiles(x86)': str(self.root / 'program-files')}))
        self.enterContext(patch.object(provenance, 'REVIEWED_NOTICE_SHA256', self.notice_files))
        self.enterContext(patch.object(provenance, 'REVIEWED_RUNTIME_SHA256', runtime_pins))
        self.enterContext(patch.object(provenance, 'file_version', return_value=provenance.REVIEWED_RUNTIME_VERSION))
        self.instance = {'installationPath': str(self.vs), 'isPrerelease': False,
                         'isComplete': True, 'channelId': 'VisualStudio.18.Release'}
        self.locator = self.enterContext(patch.object(provenance.subprocess, 'check_output',
            side_effect=lambda *args, **kwargs: json.dumps([self.instance])))

    def test_historical_environment_remains_accepted_without_stable_probe(self):
        with patch.object(provenance.subprocess, 'check_output') as locate:
            self.assertFalse(provenance.review_notice_environment(self.root / '18/Professional', Path('14.51.36231')))
        locate.assert_not_called()

    def test_reviewed_environment_sources_and_final_package_pass(self):
        self.prepare_reviewed_profile()
        self.assertTrue(provenance.review_notice_environment(self.vs, self.redist))
        installed = provenance.retain_installed_notices(self.vs, self.sdk, self.notice.parent, True)
        self.assertEqual(len(installed), 2)
        report = preflight_runtime(self.root, self.vs)
        self.assertTrue(report['reviewed_v142_runtime_pins_verified'])
        self.assertEqual(len(report['runtimes']), 4)
        self.assertEqual(len(bind_runtime(self.package, self.root, self.vs)), 4)
        self.assertEqual(self.locator.call_args.kwargs['timeout'], 30)

    def test_preview_incomplete_or_wrong_channel_installation_rejects(self):
        self.prepare_reviewed_profile()
        for field, value in (('isPrerelease', True), ('isComplete', False),
                             ('channelId', 'VisualStudio.18.Preview'), ('installationPath', str(self.root))):
            with self.subTest(field=field):
                original = self.instance[field]
                self.instance[field] = value
                with self.assertRaisesRegex(RuntimeError, 'stable VS 18'):
                    provenance.review_notice_environment(self.vs, self.redist)
                self.instance[field] = original

    def test_wrong_redist_root_or_new_unreviewed_version_rejects(self):
        self.prepare_reviewed_profile()
        outside = self.root / provenance.REVIEWED_ENVIRONMENT
        outside.mkdir()
        for redist in (outside, Path('14.52.37026')):
            with self.assertRaises(RuntimeError):
                provenance.review_notice_environment(self.vs, redist)
        with patch.dict(os.environ, {'VCToolsVersion': '14.30.0'}):
            with self.assertRaisesRegex(RuntimeError, 'pinned v142'):
                provenance.review_notice_environment(self.vs, self.redist)

    def test_changed_installed_notice_and_added_sdk_reject(self):
        self.prepare_reviewed_profile()
        self.sdk_notice.write_bytes(b'changed legal terms')
        with self.assertRaisesRegex(RuntimeError, 'notice differs'):
            provenance.retain_installed_notices(self.vs, self.sdk, self.notice.parent, True)
        added = self.sdk / 'Licenses/10.0.99999.0/sdk_license.rtf'
        added.parent.mkdir()
        added.write_bytes(b'new SDK terms')
        with self.assertRaisesRegex(RuntimeError, 'SDK notice set'):
            provenance.retain_installed_notices(self.vs, self.sdk, self.notice.parent, True)

    def test_changed_selected_runtime_rejects_even_if_package_matches_it(self):
        self.prepare_reviewed_profile()
        name = 'vcruntime140.dll'
        target = self.selected / 'x64/Microsoft.VC142.CRT' / name
        target.write_bytes(b'unreviewed DLL')
        (self.package / name).write_bytes(target.read_bytes())
        with self.assertRaisesRegex(RuntimeError, 'Selected runtime differs'):
            preflight_runtime(self.root, self.vs)
        with self.assertRaisesRegex(RuntimeError, 'Selected runtime differs'):
            bind_runtime(self.package, self.root, self.vs)

    def test_changed_selected_provider_or_runtime_version_rejects(self):
        self.prepare_reviewed_profile()
        self.cache.write_text(f'MSVC_REDIST_DIR:PATH={self.redist}\n')
        with self.assertRaisesRegex(RuntimeError, 'CMake-selected v142'):
            preflight_runtime(self.root, self.vs)
        self.cache.write_text(f'MSVC_REDIST_DIR:PATH={self.selected}\n')
        with patch.object(provenance, 'file_version', return_value='14.29.99999.0'):
            with self.assertRaisesRegex(RuntimeError, 'Selected runtime version'):
                preflight_runtime(self.root, self.vs)

    def test_changed_package_runtime_set_and_notice_reject(self):
        self.prepare_reviewed_profile()
        binary = self.package / 'vcruntime140.dll'
        original = binary.read_bytes()
        binary.write_bytes(b'changed packaged DLL')
        with self.assertRaisesRegex(RuntimeError, 'does not match'):
            bind_runtime(self.package, self.root, self.vs)
        binary.write_bytes(original)
        binary.unlink()
        with self.assertRaisesRegex(RuntimeError, 'runtime set'):
            bind_runtime(self.package, self.root, self.vs)
        binary.write_bytes(original)
        (self.notice.parent / 'VS2026-Redist.txt').write_bytes(b'changed notice')
        with self.assertRaisesRegex(RuntimeError, 'notice differs'):
            bind_runtime(self.package, self.root, self.vs)

    def test_recorded_environment_cannot_hide_new_profile_checks(self):
        self.prepare_reviewed_profile()
        data = json.loads(self.notice.read_text())
        data['notice_environment_redist_directory_version'] = '14.51.36231'
        self.notice.write_text(json.dumps(data))
        with self.assertRaisesRegex(RuntimeError, 'notice environment differs'):
            bind_runtime(self.package, self.root, self.vs)

    def test_missing_recorded_environment_rejects_final_binding(self):
        self.prepare_reviewed_profile()
        data = json.loads(self.notice.read_text())
        del data['notice_environment_redist_directory_version']
        self.notice.write_text(json.dumps(data))
        with self.assertRaisesRegex(RuntimeError, 'notice environment differs'):
            bind_runtime(self.package, self.root, self.vs)


if __name__ == '__main__':
    unittest.main()
