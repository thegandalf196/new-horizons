# SPDX-License-Identifier: GPL-2.0-or-later
"""Process-free source contracts supplement the bootstrap's real C++ pure mode."""
from pathlib import Path
import unittest

SOURCE = Path(__file__).with_name('nh-wine-bootstrap.cpp').read_text()

class BootstrapFixtureSourceTest(unittest.TestCase):
    def test_typed_argument_mode_is_fixed_and_legacy_unchanged(self):
        body = SOURCE.split('std::wstring fixtureCommand(', 1)[1].split('std::vector<wchar_t> childEnvironment', 1)[0]
        self.assertIn('if(argc == 1) return {};', body)
        self.assertIn('argc != 3', body)
        self.assertIn('std::wstring(argv[1]) != L"--fixture-map"', body)
        self.assertIn('!fixtureName(argv[2])', body)
        self.assertIn('L" --spectate --savefrequency 0"', body)
        self.assertNotIn('--onlyAI', SOURCE)

    def test_fixture_root_and_plain_file_checked_before_createprocess(self):
        start = SOURCE.index('const auto map = local +')
        self.assertIn('\\\\HeroesIII-NewHorizons\\\\content\\\\Maps\\\\', SOURCE[start:])
        self.assertIn('!boundedRoute(map) || !plainFile(map)', SOURCE[start:])
        self.assertLess(start, SOURCE.index('CreateProcessW('))
        self.assertIn('auto command = L"\\\"" + target + L"\\\"" + fixture;', SOURCE)
        self.assertIn('NH_WINE_BOOTSTRAP_FIXTURE_V1', SOURCE)

    def test_pure_cpp_mode_covers_real_parser_and_retains_environment_tests(self):
        pure = SOURCE.split('#ifdef NH_WINE_BOOTSTRAP_ENV_TEST', 1)[1].split('#elif defined(_WIN32)', 1)[0]
        self.assertIn('fixtureName(name)', pure)
        self.assertIn('fixtureCommand(3, arguments)', pure)
        self.assertIn('fixtureCommand(2, arguments)', pure)
        self.assertIn('childEnvironment(', pure)

if __name__ == '__main__':
    unittest.main()
