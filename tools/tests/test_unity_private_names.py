"""Fixture checks for the lexical Unity-batch private-name collision scan."""
# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path
import sys
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/ci'))
import unity_private_names as scan


FIRST = '''
#include "StdInc.h"
#define SAME_MACRO(x) ((x) + 1)
#define OTHER_MACRO 1
// void commented(int) {}
namespace outer
{
namespace
{
using detail::shared;
using Bundle = std::vector<int>;
using Changed = int;
struct Helper { int value; };
constexpr int LIMIT = 3;
int scaled(const JsonNode & row, const char * key = "a{")
{
	return row[key].Integer();
}
int overload(int value) { return value; }
void declaredOnly(int);
}
static bool isHero(const Hero & hero) { return true; }
void Visible::method(int) {}
}
'''

SECOND = '''
#define SAME_MACRO(x) ((x) + 1)
#define OTHER_MACRO 2
namespace outer
{
namespace
{
using detail::shared;
using Bundle = std::vector<int>;
using Changed = long;
struct Helper { int other; };
constexpr int LIMIT = 4;
int scaled(const JsonNode & node, const char * name)
{
	return 1;
}
int overload(const char * value) { return 0; }
void declaredOnly(int) {}
}
static bool isHero(const Hero & other) { return false; }
void Visible::method(int) {}
}
'''


class UnityPrivateNamesTest(unittest.TestCase):
    def collide(self, first, second):
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for index, text in enumerate((first, second)):
                path = Path(directory) / f'source{index}.cpp'
                path.write_text(textwrap.dedent(text))
                paths.append(str(path))
            return {(scope, name) for scope, name, _ in scan.collisions(paths)}

    def test_reports_only_definitions_that_clash_in_one_unit(self):
        self.assertEqual(self.collide(FIRST, SECOND), {
            ('#', 'OTHER_MACRO'),
            ('outer::(anonymous)', 'Changed'),
            ('outer::(anonymous)', 'Helper'),
            ('outer::(anonymous)', 'LIMIT'),
            ('outer::(anonymous)', 'scaled'),
            ('outer', 'isHero'),
        })

    def test_a_file_never_collides_with_itself(self):
        self.assertEqual(self.collide(FIRST, 'int unrelated() { return 0; }\n'), set())

    def test_undefined_macros_and_pointer_overloads_are_independent(self):
        first = '#define LOCAL 1\n#undef LOCAL\nnamespace { int level(const Hero & h) { return 1; } }\n'
        second = '#define LOCAL 2\nnamespace { int level(const Hero * h) { return 2; } }\n'
        self.assertEqual(self.collide(first, second), set())


if __name__ == '__main__':
    unittest.main()
