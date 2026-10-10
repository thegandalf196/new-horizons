"""Focused desktop branding contracts; not a build or rendered acceptance gate."""

from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


class NewHorizonsBrandingTest(unittest.TestCase):
    def test_desktop_name_exec_and_new_horizons_icon(self):
        text = (ROOT / "clientapp/icons/vcmiclient.desktop").read_text()
        fields = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
        self.assertEqual(fields["Name"], "New Horizons")
        self.assertEqual(fields["Exec"], "new-horizons")
        self.assertEqual(fields["Icon"], "new-horizons")
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

    def test_platform_icon_install_and_windows_resource_bindings(self):
        text = (ROOT / "clientapp/CMakeLists.txt").read_text()
        self.assertIn('icons/new-horizons.ico', text)
        self.assertNotIn('icons/vcmi.ico', text)
        self.assertIn('foreach(iconSize 16 22 32 48 64 128 256 512 1024)', text)
        self.assertIn('icons/new-horizons.${iconSize}x${iconSize}.png', text)
        self.assertIn('share/icons/hicolor/${iconSize}x${iconSize}/apps', text)
        self.assertIn('RENAME new-horizons.png', text)
        self.assertNotIn('install(FILES icons/vcmiclient.svg', text)
        self.assertTrue((ROOT / "clientapp/icons/vcmi.ico").is_file())

    def test_platform_icon_outputs_match_provenance_and_native_sizes(self):
        record = json.loads((ROOT / "assets/new-horizons/platform-icon-provenance.json").read_text())
        expected = {f"clientapp/icons/new-horizons.{size}x{size}.png"
                    for size in record["conversion"]["pngSizes"]}
        expected.add("clientapp/icons/new-horizons.ico")
        self.assertEqual(set(record["outputs"]), expected)
        for relative, digest in record["outputs"].items():
            with self.subTest(path=relative):
                path = ROOT / relative
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), digest)
                with Image.open(path) as image:
                    if path.suffix == ".png":
                        size = int(path.stem.split(".")[-1].split("x")[0])
                        self.assertEqual(image.size, (size, size))
                        self.assertEqual(image.mode, "RGB")
                        self.assertFalse(image.info)
                    else:
                        sizes = {(size, size) for size in record["conversion"]["icoSizes"]}
                        self.assertEqual(image.ico.sizes(), sizes)
                        for size in sizes:
                            frame = image.ico.getimage(size).convert("RGBA")
                            self.assertEqual(frame.size, size)
                            self.assertEqual(frame.getchannel("A").getextrema(), (255, 255))

    def test_platform_icon_master_stays_private_and_no_creative_transform(self):
        record = json.loads((ROOT / "assets/new-horizons/platform-icon-provenance.json").read_text())
        self.assertEqual(record["authoringMaster"]["sha256"],
                         "93e8d0ebe18e60d88db214fe81b400ed7a2d1e85ac3ae27850e7e5079435d89d")
        self.assertEqual(record["authoringMaster"]["width"], 1254)
        for key in ("cropping", "redesign", "backgroundRemoval", "upscaling"):
            self.assertFalse(record["conversion"][key])
        self.assertFalse((ROOT / "clientapp/icons/new-horizons.1254x1254.png").exists())
        self.assertIn("outside NewHorizons.nhart", record["distribution"]["loosePlatformException"])

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
        icon_resource = re.search(r'^\s*set\(VCMI_ICON_RESOURCE .*$', source, re.M).group().strip()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "client.rc"
            script = Path(directory) / "configure.cmake"
            script.write_text(
                f'set(CMAKE_SOURCE_DIR "{ROOT.as_posix()}")\n'
                f'set(CMAKE_CURRENT_SOURCE_DIR "{(ROOT / "clientapp").as_posix()}")\n'
                f'set(client_VERSIONINFO_RC "{output.as_posix()}")\n' +
                icon_resource + '\n' +
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
            self.assertIn(f'IDI_ICON1 ICON "{(ROOT / "clientapp/icons/new-horizons.ico").as_posix()}"', result)


if __name__ == "__main__":
    unittest.main()
