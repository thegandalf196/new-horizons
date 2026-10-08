"""Focused desktop branding contracts; not a build or rendered acceptance gate."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NewHorizonsBrandingTest(unittest.TestCase):
    def test_desktop_name_exec_and_upstream_icon_fallback(self):
        text = (ROOT / "clientapp/icons/vcmiclient.desktop").read_text()
        fields = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
        self.assertEqual(fields["Name"], "New Horizons")
        self.assertEqual(fields["Exec"], "new-horizons")
        self.assertEqual(fields["Icon"], "vcmiclient")
        self.assertIn("VCMI", fields["Comment"])
        self.assertEqual(fields["Name[cs]"], "New Horizons")
        self.assertEqual(fields["Name[de]"], "New Horizons")

    def test_binary_target_and_mobile_abi_remain_stable(self):
        text = (ROOT / "clientapp/CMakeLists.txt").read_text()
        self.assertIn("add_executable(vcmiclient", text)
        self.assertIn('OUTPUT_NAME "new-horizons"', text)
        self.assertIn('OUTPUT_NAME "vcmiclient_${ANDROID_ABI}"', text)
        self.assertIn("if(NOT IOS)", text)
        self.assertIn("RENAME new-horizons.desktop", text)

    def test_both_sdl_backends_title_and_help_keep_engine_attribution(self):
        for backend in ("clientsdl2", "clientsdl3"):
            with self.subTest(backend=backend):
                text = (ROOT / backend / "render/ScreenHandler.cpp").read_text()
                self.assertIn('SDL_CreateWindow("New Horizons",', text)
        text = (ROOT / "clientapp/EntryPoint.cpp").read_text()
        self.assertIn("New Horizons - powered by %s", text)
        self.assertIn("VCMI dev team - see AUTHORS file", text)
        self.assertIn("This is free software", text)

    def test_credits_add_thanks_without_removing_original_contributors(self):
        text = (ROOT / "client/mainmenu/CreditsScreen.cpp").read_text()
        self.assertIn("Thank you to the VCMI team and contributors", text)
        self.assertIn("GNU General Public License v2.0 or later", text)
        self.assertIn('#include "../../AUTHORS.h"', text)
        self.assertIn("for (auto & element : contributors)", text)
        self.assertIn('ResourcePath("DATA/CREDITS.TXT")', text)
        self.assertIn('translate("vcmi.credits.heroes")', text)
        self.assertIn("https://vcmi.eu", text)

    def test_windows_metadata_configuration_is_client_scoped(self):
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake, "Focused Windows metadata check requires CMake")
        source = (ROOT / "clientapp/CMakeLists.txt").read_text()
        helper = re.search(r"function\(configure_new_horizons_version_info\).*?endfunction\(\)", source, re.S).group()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "client.rc"
            script = Path(directory) / "configure.cmake"
            script.write_text(
                f'set(CMAKE_SOURCE_DIR "{ROOT.as_posix()}")\n'
                f'set(client_VERSIONINFO_RC "{output.as_posix()}")\n'
                'set(VCMI_PROJECT_NAME "VCMI")\n'
                'set(VCMI_FILE_DESCRIPTION "New Horizons")\n'
                'set(VCMI_ORIGINAL_FILENAME "new-horizons.exe")\n'
                'set(VCMI_VERSION_STRING "1.0.0")\n' + helper + '\nconfigure_new_horizons_version_info()\n'
                'if(NOT VCMI_PROJECT_NAME STREQUAL "VCMI")\nmessage(FATAL_ERROR "Engine name leaked")\nendif()\n')
            subprocess.run([cmake, "-P", str(script)], check=True, capture_output=True)
            result = output.read_text()
            self.assertIn('VALUE "ProductName", "New Horizons"', result)
            self.assertIn('VALUE "OriginalFilename", "new-horizons.exe"', result)
            self.assertIn('VALUE "CompanyName", "VCMI Team"', result)
            self.assertIn("VCMI Team. All rights reserved.", result)


if __name__ == "__main__":
    unittest.main()
