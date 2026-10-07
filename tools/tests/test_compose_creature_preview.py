"""Exercise private creature-preview composition and launcher isolation."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/ci"))
import compose_creature_preview as preview


LAUNCHER = ROOT / "tools/new-horizons-launch.sh"


class ComposeCreaturePreviewTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nh-creature-preview-")
        self.root = Path(self.temp.name)
        self.resources = self.root / "source resources"
        self.resources.mkdir()
        self.build = self.root / "build"
        self.build.mkdir()
        self.client = self.build / "vcmiclient"
        self.client.write_text(
            "#!/usr/bin/python3\n"
            "import json, os, sys\n"
            "from pathlib import Path\n"
            "capture = os.environ.get('NH_PREVIEW_CAPTURE')\n"
            "if capture:\n"
            "    runtime = Path(os.environ['XDG_DATA_DIRS'])\n"
            "    modroot = runtime / 'Mods/new-horizons'\n"
            "    result = {\n"
            "        'argv': sys.argv[1:],\n"
            "        'client': str(Path(sys.argv[0]).resolve()),\n"
            "        'runtime': str(runtime),\n"
            "        'config_home': os.environ.get('XDG_CONFIG_HOME'),\n"
            "        'data_home': os.environ.get('XDG_DATA_HOME'),\n"
            "        'cache_home': os.environ.get('XDG_CACHE_HOME'),\n"
            "        'modroot': str(modroot.resolve()),\n"
            "        'tower': (modroot / 'Content/config/factions/tower.json').read_text(),\n"
            "        'conflux': (modroot / 'Content/config/factions/conflux.json').read_text(),\n"
            "    }\n"
            "    Path(capture).write_text(json.dumps(result), encoding='utf-8')\n",
            encoding="utf-8",
        )
        self.client.chmod(0o755)
        (self.build / "libvcmi.so").write_bytes(b"fixture shared library\n")

        self._write_text("config/filesystem.json", "{}\n")
        self._write_text("config/newHorizonsCombat.json", "{}\n")
        self._write_text("config/newHorizonsMagic.json", "{}\n")
        self._write_text("scripts/damage/damageCalculator.lua", "return {}\n")
        self._write_text("Mods/vcmi/mod.json", "{}\n")
        self._write_text("Mods/new-horizons/mod.json", "{}\n")
        self.base_image = self._write_image(
            self.resources / "Mods/new-horizons/Images/base.png", (12, 34, 56, 255)
        )

    def tearDown(self):
        self.temp.cleanup()

    def _write_text(self, relative, contents):
        path = self.resources / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")
        return path

    def _write_image(self, path, color):
        path.parent.mkdir(parents=True, exist_ok=True)
        Image.new("RGBA", (3, 2), color).save(path, format="PNG")
        return path

    def _write_overlay(self, name, faction, color):
        overlay = self.root / name
        mod_root = overlay / "Mods/new-horizons"
        config = mod_root / "Content/config/factions" / f"{faction}.json"
        config.parent.mkdir(parents=True, exist_ok=True)
        config.write_text(json.dumps({"overlay": faction}), encoding="utf-8")
        image = self._write_image(mod_root / "Images" / f"{faction}.png", color)
        return overlay, config, image

    @staticmethod
    def _image_state(path):
        with Image.open(path) as image:
            pixels = image.convert("RGBA").tobytes()
        return path.stat().st_ino, path.read_bytes(), pixels

    def test_tower_and_conflux_overlays_coexist_without_mutating_sources(self):
        tower, tower_config, tower_image = self._write_overlay(
            "tower overlay", "tower", (210, 30, 40, 255)
        )
        conflux, conflux_config, conflux_image = self._write_overlay(
            "conflux overlay", "conflux", (30, 180, 70, 255)
        )
        sources = [self.base_image, tower_config, tower_image, conflux_config, conflux_image]
        before = {path: self._image_state(path) if path.suffix == ".png" else (path.stat().st_ino, path.read_bytes())
                  for path in sources}

        destination = self.root / "combined private candidate"
        copied = preview.compose(self.client, self.resources, [tower, conflux], destination)

        self.assertGreater(copied, 0)
        self.assertEqual(
            json.loads((destination / "Mods/new-horizons/Content/config/factions/tower.json").read_text()),
            {"overlay": "tower"},
        )
        self.assertEqual(
            json.loads((destination / "Mods/new-horizons/Content/config/factions/conflux.json").read_text()),
            {"overlay": "conflux"},
        )
        for path in sources:
            if path.suffix == ".png":
                self.assertEqual(self._image_state(path), before[path])
                copied_path = (
                    destination / path.relative_to(self.resources)
                    if self.resources in path.parents
                    else destination / "Mods/new-horizons/Images" / path.name
                )
                self.assertNotEqual(path.stat().st_ino, copied_path.stat().st_ino)
            else:
                self.assertEqual((path.stat().st_ino, path.read_bytes()), before[path])

        with Image.open(destination / "Mods/new-horizons/Images/tower.png") as image:
            self.assertEqual(image.convert("RGBA").getpixel((1, 1)), (210, 30, 40, 255))
        with Image.open(destination / "Mods/new-horizons/Images/conflux.png") as image:
            self.assertEqual(image.convert("RGBA").getpixel((1, 1)), (30, 180, 70, 255))

    def test_nested_overlay_symlink_is_rejected_before_output_creation(self):
        overlay, _config, _image = self._write_overlay(
            "symlink overlay", "tower", (200, 25, 30, 255)
        )
        outside = self.root / "outside.png"
        outside.write_bytes(b"must not be copied through a link")
        link = overlay / "Mods/new-horizons/Images/outside.png"
        try:
            link.symlink_to(outside)
        except OSError:
            self.skipTest("file symlink creation is unavailable")

        destination = self.root / "rejected candidate"
        with self.assertRaisesRegex(RuntimeError, "symlink or non-regular file"):
            preview.compose(self.client, self.resources, [overlay], destination)
        self.assertFalse(destination.exists())

    def test_existing_and_symlink_destinations_are_refused_without_changes(self):
        overlay, _config, _image = self._write_overlay(
            "ordinary overlay", "tower", (200, 25, 30, 255)
        )
        existing = self.root / "existing candidate"
        existing.mkdir()
        sentinel = existing / "keep.txt"
        sentinel.write_text("preserve", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "must not already exist"):
            preview.compose(self.client, self.resources, [overlay], existing)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "preserve")

        target = self.root / "real destination"
        target.mkdir()
        linked_destination = self.root / "destination link"
        try:
            linked_destination.symlink_to(target, target_is_directory=True)
        except OSError:
            self.skipTest("directory symlink creation is unavailable")
        with self.assertRaisesRegex(ValueError, "must not already exist"):
            preview.compose(self.client, self.resources, [overlay], linked_destination)
        self.assertFalse(any(target.iterdir()))

    def test_launcher_uses_composed_resources_and_a_separate_private_profile(self):
        tower, _tower_config, _tower_image = self._write_overlay(
            "tower overlay", "tower", (210, 30, 40, 255)
        )
        conflux, _conflux_config, _conflux_image = self._write_overlay(
            "conflux overlay", "conflux", (30, 180, 70, 255)
        )
        candidate = self.root / "temporary combined candidate"
        preview.compose(self.client, self.resources, [tower, conflux], candidate)

        assets = self.root / "read only purchaser assets"
        for directory in ("Data", "Maps", "Mp3"):
            (assets / directory).mkdir(parents=True, exist_ok=True)
        for archive in ("h3bitmap.lod", "h3sprite.lod"):
            (assets / "Data" / archive).write_bytes(b"fixture marker, not original game data")
        asset_before = {
            path: (path.stat().st_ino, path.read_bytes())
            for path in (assets / "Data/h3bitmap.lod", assets / "Data/h3sprite.lod")
        }
        profile = self.root / "isolated preview profile"
        capture = self.root / "client environment.json"
        host_data = self.root / "preexisting host data"
        host_config = self.root / "preexisting host config"
        host_cache = self.root / "preexisting host cache"
        env = os.environ.copy()
        env.update({
            "XDG_DATA_HOME": str(host_data),
            "XDG_CONFIG_HOME": str(host_config),
            "XDG_CACHE_HOME": str(host_cache),
            "XDG_DATA_DIRS": str(self.root / "untrusted host data dirs"),
            "NH_PREVIEW_CAPTURE": str(capture),
        })
        common = [
            "bash", str(LAUNCHER),
            "--assets", str(assets),
            "--profile", str(profile),
            "--client", str(candidate / "vcmiclient"),
            "--resources", str(candidate),
        ]

        verified = subprocess.run(
            [*common, "--verify-only"], env=env, capture_output=True, text=True, check=False, timeout=10
        )
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertFalse(profile.exists(), "verify-only must not create the private profile")
        self.assertFalse(capture.exists(), "verify-only must not execute the client")

        launched = subprocess.run(common, env=env, capture_output=True, text=True, check=False, timeout=10)
        self.assertEqual(launched.returncode, 0, launched.stderr)
        self.assertTrue(capture.is_file())
        observed = json.loads(capture.read_text(encoding="utf-8"))
        self.assertEqual(observed["argv"], ["--nointro"])
        self.assertEqual(observed["client"], str(candidate / "vcmiclient"))
        self.assertEqual(observed["config_home"], str(profile / "config"))
        self.assertEqual(observed["data_home"], str(profile / "data"))
        self.assertEqual(observed["cache_home"], str(profile / "cache"))
        self.assertTrue(observed["runtime"].startswith(str(profile / "runtime.")))
        self.assertEqual(observed["modroot"], str(candidate / "Mods/new-horizons"))
        self.assertEqual(json.loads(observed["tower"]), {"overlay": "tower"})
        self.assertEqual(json.loads(observed["conflux"]), {"overlay": "conflux"})
        self.assertFalse(any(profile.glob("runtime.*")), "launcher should remove its temporary runtime links")
        self.assertTrue((profile / ".nh-profile").is_file())
        self.assertFalse(host_data.exists())
        self.assertFalse(host_config.exists())
        self.assertFalse(host_cache.exists())
        self.assertEqual(
            {path: (path.stat().st_ino, path.read_bytes()) for path in asset_before}, asset_before
        )


if __name__ == "__main__":
    unittest.main()
