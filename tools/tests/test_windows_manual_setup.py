"""Manual picker packaging must preserve the released game and its source identity."""
# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import package_windows_manual_setup as manual


class ManualSetupPackageTest(unittest.TestCase):
    def baseline(self):
        payload = {'new-horizons.exe': b'original root PE',
                   'runtime/new-horizons.exe': b'original game PE',
                   'runtime/VCMI_lib.dll': b'original library',
                   'runtime/Start-New-Horizons.ps1': b'old script',
                   'runtime/BUILD-IDENTITY.json': b'original provenance',
                   'runtime/Mods/new-horizons/NewHorizons.nhart': b'original art'}
        payload[manual.CHECKSUMS] = ''.join(f'{manual.digest(data)}  {name}\r\n'
                                          for name, data in payload.items()).encode()
        return payload

    def test_only_explicit_setup_changes_and_checksums(self):
        original = self.baseline()
        sources = {'tools/windows/Start-New-Horizons.ps1': b'new script',
                   'tools/windows/Complete-Assets.ps1': b'new resolver'}
        result = manual.compose(original, sources, b'new Setup PE', 'a' * 40, {'KERNEL32.dll': 'system'})
        self.assertEqual(set(result) - set(original), manual.ADDED)
        for name, data in original.items():
            if name not in manual.REPLACED | {manual.CHECKSUMS}:
                self.assertEqual(result[name], data, name)
        manual.verify_checksums(result)
        self.assertEqual(original, self.baseline())

    def test_checksum_tamper_extra_missing_duplicate_fail(self):
        for mutation in ('changed', 'extra', 'missing', 'duplicate'):
            payload = self.baseline()
            if mutation == 'changed':
                payload['runtime/VCMI_lib.dll'] = b'wrong DLL'
            elif mutation == 'extra':
                payload['runtime/hidden.dll'] = b'unlisted'
            elif mutation == 'missing':
                del payload['runtime/new-horizons.exe']
            else:
                payload[manual.CHECKSUMS] *= 2
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                manual.verify_checksums(payload)

    def test_wrong_release_rejected_before_output(self):
        import tempfile
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'wrong.zip'
            path.write_bytes(b'not the audited release')
            with self.assertRaisesRegex(ValueError, 'exact audited'):
                manual.read_payload(path)


if __name__ == '__main__':
    unittest.main()
