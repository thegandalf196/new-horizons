#!/usr/bin/env python3
"""Offline checks for the authoritative New Horizons product version and menu label."""
import importlib.util
import json
import re
from pathlib import Path
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[2]
GENERATOR_PATH = ROOT / 'tools' / 'update-new-horizons-module.py'
MENU_HEADER = (ROOT / 'client' / 'mainmenu' / 'CMainMenu.h').read_text(encoding='utf-8')
MENU_SOURCE = (ROOT / 'client' / 'mainmenu' / 'CMainMenu.cpp').read_text(encoding='utf-8')

spec = importlib.util.spec_from_file_location('new_horizons_module_generator', GENERATOR_PATH)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class ProductVersionTests(unittest.TestCase):
    def test_live_module_and_generator_share_the_root_version(self):
        version_config = json.loads((ROOT / 'config' / 'newHorizonsVersion.json').read_text(encoding='utf-8'))
        self.assertRegex(version_config['version'], r'^\d+\.\d+\.\d+$')
        self.assertEqual(generator.load_product_version(ROOT), version_config['version'])
        self.assertEqual(generator.resolve_module_version(version_config['version']), version_config['version'])

        live_module = json.loads((ROOT / 'Mods' / 'new-horizons' / 'mod.json').read_text(encoding='utf-8'))
        self.assertEqual(live_module['version'], version_config['version'])

    def test_historical_diagnostic_module_versions_remain_isolated(self):
        product_version = generator.load_product_version(ROOT)
        cases = (
            ('hero-preview', 'hero_preview_output', '0.3.0'),
            ('capability-preview', 'capability_preview_output', '0.4.0'),
            ('mastery-preview', 'mastery_preview_output', '0.5.0'),
        )
        for kind, selected_option, diagnostic_version in cases:
            with self.subTest(kind=kind):
                options = {
                    'hero_preview_output': None,
                    'capability_preview_output': None,
                    'capability_only_control_output': None,
                    'mastery_preview_output': None,
                }
                options[selected_option] = object()
                args = SimpleNamespace(**options)
                self.assertEqual(generator.get_diagnostic_kind(args), kind)
                self.assertEqual(generator.resolve_module_version(product_version, kind), diagnostic_version)

        self.assertEqual(generator.get_diagnostic_kind(SimpleNamespace(
            hero_preview_output=None,
            capability_preview_output=None,
            capability_only_control_output=object(),
            mastery_preview_output=None,
        )), 'capability-preview')

    def test_menu_loads_and_displays_the_root_version(self):
        self.assertIn('productVersionConfig(JsonPath::builtin("config/newHorizonsVersion.json"))', MENU_SOURCE)
        self.assertIn('std::string getProductVersion() const;', MENU_HEADER)
        self.assertIn('return productVersionConfig["version"].String();', MENU_SOURCE)
        self.assertIn('"New Horizons " + CMainMenuConfig::get().getProductVersion()', MENU_SOURCE)
        self.assertNotRegex(MENU_SOURCE + MENU_HEADER, r'0\.14\.0')

    def test_menu_label_uses_native_style_and_background_bounded_placement(self):
        compact_source = re.sub(r'\s+', '', MENU_SOURCE)
        self.assertIn('std::shared_ptr<CLabel> versionLabel;', MENU_HEADER)
        self.assertLess(MENU_SOURCE.index('tabs = std::make_shared<CTabbedInt>'),
                        MENU_SOURCE.index('versionLabel = std::make_shared<CLabel>'))
        self.assertRegex(compact_source, r'constexprintversionLabelInset=\d+;')
        self.assertIn('background->pos.w-2*versionLabelInset', compact_source)
        self.assertIn('versionLabelInset,background->pos.h-versionLabelBottomInset,FONT_SMALL,ETextAlignment::BOTTOMLEFT,Colors::YELLOW', compact_source)

        inset = int(re.search(r'constexpr int versionLabelInset = (\d+);', MENU_SOURCE).group(1))
        background_width, background_height = 800, 600
        maximum_text_width = max(1, background_width - 2 * inset)
        measured_text_width = background_width
        visible_width = min(measured_text_width, maximum_text_width)
        left = inset
        bottom_inset = int(re.search(r'constexpr int versionLabelBottomInset = (\d+);', MENU_SOURCE).group(1))
        bottom = background_height - bottom_inset
        top = bottom - 20  # Conservative label height for this bounds check.

        self.assertGreaterEqual(left, 0)
        self.assertLessEqual(left + visible_width, background_width - inset)
        self.assertGreaterEqual(top, 0)
        self.assertLessEqual(bottom, background_height)


if __name__ == '__main__':
    unittest.main()
