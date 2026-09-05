#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Package import-classification regression; mocked dumpbin, no PE execution."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / "ci/package_new_horizons_windows.py"
spec = importlib.util.spec_from_file_location("nh_windows_package", SOURCE)
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class WindowsImportAuditTest(unittest.TestCase):
    def audit(self, extra_import):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("VCMI_Client.exe", "VCMI_lib.dll", "avformat-63.dll"):
                (root / name).write_bytes(b"synthetic; not executable")

            def dumpbin(tool, option, *arguments):
                # Actual calls include /nologo before the operation.
                operation, filename = arguments
                self.assertEqual(option, "/nologo")
                name = Path(filename).name.lower()
                if operation == "/headers":
                    return "8664 machine (x64)"
                if operation == "/exports":
                    return ""
                self.assertEqual(operation, "/dependents")
                imports = {
                    "vcmi_client.exe": "VCMI_lib.dll",
                    "vcmi_lib.dll": "avformat-63.dll",
                    "avformat-63.dll": extra_import,
                }
                return "    " + imports[name] + "\n"

            with patch.object(packager.shutil, "which", return_value="dumpbin"), \
                    patch.object(packager, "run", side_effect=dumpbin):
                return packager.audit_pe_tree(root)

    def test_transitive_ncrypt_is_windows_provided_case_insensitively(self):
        report = self.audit("NCrypt.DLL")
        self.assertEqual(report["avformat-63.dll"]["NCrypt.DLL"],
                         "Windows system/API contract")
        self.assertEqual(report["VCMI_lib.dll"]["avformat-63.dll"], "bundled")

    def test_unknown_transitive_dependency_still_fails(self):
        with self.assertRaisesRegex(RuntimeError, "Unresolved non-system PE import.*missing-codec.dll"):
            self.audit("missing-codec.dll")

    def test_ms_crt_must_not_be_mistaken_for_os_dependency(self):
        with self.assertRaisesRegex(RuntimeError, "Unresolved non-system PE import.*VCRUNTIME140.dll"):
            self.audit("VCRUNTIME140.dll")


if __name__ == "__main__":
    unittest.main()
