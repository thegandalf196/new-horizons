import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from windows_crt_provenance import bind_runtime, preflight_runtime, sha256


class WindowsCRTProvenanceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
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


if __name__ == '__main__':
    unittest.main()
