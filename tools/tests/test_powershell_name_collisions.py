#!/usr/bin/env python3
"""PowerShell variable names are case-insensitive: a local `$track` inside a function
with a `[string]$Track` parameter overwrites (and converts) the parameter. That broke
the Windows launcher of 0.94.0 Beta 1 and Beta 2. Forbid differently cased
assignments to a function's own parameters in the shipped launcher scripts."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function_bodies(source):
    for match in re.finditer(r'function\s+([\w-]+)\s*(?:\(([^)]*)\))?\s*\{', source):
        depth, index = 1, match.end()
        while index < len(source) and depth:
            depth += {'{': 1, '}': -1}.get(source[index], 0)
            index += 1
        yield match.group(1), match.group(2) or '', source[match.end():index]


def collisions(source):
    found = []
    for name, params, body in function_bodies(source):
        declared = re.search(r'^\s*param\s*\(([^)]*)\)', body, re.M)
        if declared:
            params += ',' + declared.group(1)
        names = {param.lower(): param for param in re.findall(r'\$(\w+)', params)}
        for assigned in sorted(set(re.findall(r'^\s*\$(\w+)\s*=', body, re.M))):
            original = names.get(assigned.lower())
            if original and assigned != original:
                found.append(f'{name}: parameter ${original} reassigned as ${assigned}')
    return found


class PowerShellNameCollisionTest(unittest.TestCase):
    def test_detects_the_beta_launcher_bug(self):
        source = 'function New-T($Form, [string]$Track) {\n    $track = New-Object Windows.Forms.ComboBox\n}\n'
        self.assertEqual(collisions(source), ['New-T: parameter $Track reassigned as $track'])

    def test_same_case_reuse_is_allowed(self):
        self.assertEqual(collisions('function F([string]$Path) {\n    $Path = $Path.Trim()\n}\n'), [])

    def test_shipped_launcher_scripts_have_no_collisions(self):
        problems = []
        for script in sorted((ROOT / 'tools' / 'windows').rglob('*.ps1')):
            text = script.read_text(encoding='utf-8-sig', errors='replace')
            problems += [f'{script.relative_to(ROOT)}: {problem}' for problem in collisions(text)]
        self.assertEqual(problems, [])


if __name__ == '__main__':
    unittest.main()
