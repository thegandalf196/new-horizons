#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Selected manifest policy checks using synthetic data, not original pixels."""

import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "nhart_manifest_builder", ROOT / "tools/build_new_horizons_art_manifest.py")
BUILDER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILDER)


class NHArtManifestBuilderTest(unittest.TestCase):
    def test_unchanged_castle_icon_is_not_selected_but_descriptor_is(self):
        self.assertFalse(BUILDER.selected(BUILDER.EXTERNAL_CASTLE_HALL_ICON))
        self.assertTrue(BUILDER.selected(BUILDER.CASTLE_HALL_DESCRIPTOR))
        self.assertTrue(BUILDER.selected(
            BUILDER.CONTENT_ROOT + "sprites/HALLFORT/mage-guild-5.png"))

    def test_original_icon_override_becomes_exact_external_frame_alias(self):
        synthetic = json.dumps({"basepath": "HALLCSTL/", "images": [
            {"group": 0, "frame": 4, "file": "mage-guild-5.png"}]}).encode()
        untouched = bytes(synthetic)
        result = BUILDER.runtime_payload(BUILDER.CASTLE_HALL_DESCRIPTOR, synthetic)
        self.assertEqual(synthetic, untouched)
        self.assertEqual(json.loads(result), {"images": [
            {"group": 0, "frame": 4, "defFile": "HALLCSTL.def",
             "defGroup": 0, "defFrame": 3}]})
        self.assertEqual(list(BUILDER.descriptor_references(json.loads(result))), [])
        self.assertEqual(hashlib.sha256(result).hexdigest(),
                         "7460ec69c7925bbb1ba09eba412c32e5a1f059f146a127398beaa955bac77b90")

    def test_unrelated_selected_bytes_are_not_changed(self):
        payload = b"synthetic modified artwork\0\xff"
        self.assertIs(BUILDER.runtime_payload(
            BUILDER.CONTENT_ROOT + "sprites/HALLFORT/mage-guild-5.png", payload), payload)


if __name__ == "__main__":
    unittest.main()
