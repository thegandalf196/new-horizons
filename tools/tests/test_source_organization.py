#!/usr/bin/env python3
"""Synthetic parser and growth-policy acceptance, with no native compilation."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from source_organization import apply_manual, check, proposal, units
from source_organization_inventory import classification, inventory, language, measure


def report(path, data):
    return {'schema': 1, 'files': [measure(path, data)]}


def baseline_for(value, kind='legacy', review='synthetic-review-1'):
    entries = {}
    for key, unit in units(value).items():
        if unit['lines'] > (500 if unit['kind'] == 'file' else 80):
            entries[key] = {'lines': unit['lines'], 'sha256': unit['sha256'],
                            'kind': kind, 'review': review, 'rationale': 'Synthetic cohesive fixture'}
    return {'schema': 1, 'units': entries}


class SourceOrganizationTest(unittest.TestCase):
    def test_cpp_templates_overloads_lambdas_and_unchanged_symbols_after_shift(self):
        data = b'''namespace A {
template<class T>
T f(T x)
{
 return x;
}
int f(int x) {
 auto callback = [x]() {
   return x;
 };
 return callback();
}
}
'''
        file = measure('lib/x.h', data)
        self.assertEqual(file['diagnostics'], [])
        functions = file['functions']
        self.assertEqual(len(functions), 3)
        self.assertEqual([(f['start'], f['end']) for f in functions], [(2, 6), (7, 12), (8, 10)])
        self.assertEqual(len({f['symbol'] for f in functions}), 3)
        shifted = measure('lib/x.h', b'// unrelated line\n' + data)
        self.assertEqual([f['symbol'] for f in functions], [f['symbol'] for f in shifted['functions']])
        self.assertEqual([f['sha256'] for f in functions], [f['sha256'] for f in shifted['functions']])

    def test_markers_preserve_slots_variables_and_all_preprocessor_branches(self):
        data = b'''class DLL_LINKAGE A {
public slots:
 void foo() { int slots = 3; }
};
#ifdef ANYTHING
int first() { return 1; }
#else
int second() { return 2; }
#endif
#define BODY(x) \\
{ return x; }
'''
        file = measure('x.h', data)
        self.assertEqual(file['diagnostics'], [])
        self.assertEqual(len(file['functions']), 4)
        self.assertTrue(any(f['symbol'].startswith('#define BODY') for f in file['functions']))

    def test_actual_language_callable_nodes(self):
        cases = {
            'x.py': (b'class C:\n def f(self):\n  return lambda x: x\n', 2),
            'x.lua': (b'local function f(x)\n return function() return x end\nend', 2),
            'x.mjs': (b'function f() {}\nconst g = x => x;\nclass C { f() {} }', 3),
            'x.java': (b'class C { C() {} int f() { return 1; } }', 2),
            'x.m': (b'@implementation C\n-(int)foo:(int)x { return x; }\n@end', 1),
            'x.sh': (b'foo() {\n true\n}\nfunction bar { true; }', 2),
        }
        for path, (data, count) in cases.items():
            with self.subTest(path=path):
                file = measure(path, data)
                self.assertEqual(file['diagnostics'], [])
                self.assertEqual(len(file['functions']), count)

    def test_explicit_classification_and_build_language_limits(self):
        self.assertEqual(classification('lib/GeneratedImageRecipes.cpp'), 'production')
        self.assertEqual(classification('launcher/modManager/cmodlistview_moc.cpp'), 'production')
        self.assertEqual(classification('dependencies/conanfile.py'), 'vendor')
        self.assertEqual(classification('client/lib/new_first_party.cpp'), 'production')
        self.assertEqual(classification('launcher/lib/new_first_party.cpp'), 'production')
        self.assertEqual(classification('tools/ci/test_inventory.py'), 'production')
        self.assertEqual(classification('tools/ci/test_windows_ai_compile_flags.py'), 'test')
        self.assertEqual(classification('test/x.cpp'), 'test')
        self.assertEqual(language('Version.cpp.in'), 'cpp')
        self.assertEqual(language('.github/workflows/github.yml'), 'workflow-yaml')
        self.assertEqual(measure('.github/workflows/github.yml', b'run: echo ok')['measurement'], 'manual-required')
        self.assertIsNone(language('launcher/translation/english.ts'))
        self.assertIsNone(language('docs/maintainers/scripts/web/wiki.vcmi.eu'))
        self.assertEqual(measure('CMakeLists.txt', b'function(f)\nendfunction()')['measurement'], 'balanced-cmake-commands')

    def test_cmake_nested_comments_brackets_quotes_and_escapes(self):
        data = b'''#[=[ function(fake) endfunction() ]=]
function(real arg)
 set(x "function(quote) )")
 set(y [==[endfunction() ( ]==])
 set(z escaped\\)paren)
 macro(nested)
  # endfunction()
 endmacro(nested)
endfunction(real)
'''
        file = measure('CMakeLists.txt', data)
        self.assertEqual(file['diagnostics'], [])
        self.assertEqual([(f['start'], f['end']) for f in file['functions']], [(2, 9), (6, 8)])
        self.assertIn('macro(nested)', file['functions'][1]['symbol'])
        for invalid in (b'function(f)\n', b'function(f)\nendmacro()', b'set(x [==[broken)'):
            self.assertTrue(measure('CMakeLists.txt', invalid)['diagnostics'])

    def test_new_review_triggers_and_ceiling_exceptions(self):
        value = report('x.py', b'\n' * 501)
        self.assertTrue(check(value, {'files': []}, {'units': {}}, {})['errors'])
        approved = baseline_for(value, 'review', 'new-review')
        self.assertEqual(check(value, {'files': []}, approved, {})['errors'], [])
        self.assertTrue(check(value, {'files': []}, proposal(value), {})['errors'])
        value = report('x.py', b'\n' * 1001)
        approved = baseline_for(value, 'review')
        self.assertTrue(check(value, {'files': []}, approved, {})['errors'])
        approved = baseline_for(value, 'exception')
        approved['units']['x.py']['deferral'] = 'Extract bounded synthetic fixture, assigned owner, verify physical counts'
        self.assertEqual(check(value, {'files': []}, approved, {})['errors'], [])

    def test_growth_unchanged_hotspot_extraction_and_ratchet(self):
        old = report('x.py', b'# old\n' * 1200)
        baseline = baseline_for(old)
        self.assertEqual(check(old, old, baseline, baseline)['errors'], [])
        grown = report('x.py', b'# old\n' * 1201)
        self.assertTrue(check(grown, old, baseline_for(grown), baseline)['errors'])
        self.assertTrue(check(grown, old, baseline_for(grown, 'review', 'fresh-review'), baseline)['errors'])
        shrunk = report('x.py', b'# old\n' * 1100)
        self.assertTrue(check(shrunk, old, baseline, baseline)['errors'])
        reviewed = baseline_for(shrunk, review='extraction-review-2')
        self.assertEqual(check(shrunk, old, reviewed, baseline)['errors'], [])
        regrown = report('x.py', b'# old\n' * 1150)
        self.assertTrue(check(regrown, shrunk, baseline_for(regrown), reviewed)['errors'])

    def test_move_cannot_inherit_hotspot_baseline_and_digest_prevents_same_size_change(self):
        old = report('before.py', b'# old\n' * 1200)
        moved = report('after.py', b'# old\n' * 1200)
        self.assertTrue(check(moved, old, baseline_for(old), baseline_for(old))['errors'])
        self.assertTrue(check(moved, old, baseline_for(moved), baseline_for(old))['errors'])
        edited = report('before.py', b'# new\n' * 1200)
        self.assertTrue(check(edited, old, baseline_for(old), baseline_for(old))['errors'])

    def test_pending_gaps_are_frozen_and_manual_inventory_is_hash_pinned(self):
        data = b'function(foo)\n message(ok)\nendfunction()\n'
        current = report('fixture.nix', data)
        file = current['files'][0]
        baseline = {'coverage_pending': {'fixture.nix': {
            'sha256': file['sha256'], 'owner': 'engine engineer', 'reason': 'CMake grammar unavailable'}}}
        self.assertEqual(apply_manual(copy.deepcopy(current), baseline, lambda path: data, current, baseline), [])
        edited = report('fixture.nix', data + b'\n')
        self.assertTrue(apply_manual(edited, baseline, lambda path: data, current, baseline))
        manual = {'manual_functions': {'fixture.nix': {
            'sha256': file['sha256'], 'review': 'synthetic', 'rationale': 'Complete named-function review',
            'functions': [{'symbol': 'foo', 'start': 1, 'end': 3}]}}}
        self.assertEqual(apply_manual(current, manual, lambda path: data), [])
        self.assertEqual(current['files'][0]['functions'][0]['lines'], 3)
        self.assertEqual(current['files'][0]['measurement'], 'manual-reviewed')

    def test_recovery_is_visible_and_fails_closed_without_reviewed_spans(self):
        current = report('x.cpp', b'void f( {\n nonsense\n')
        self.assertTrue(current['files'][0]['diagnostics'])
        self.assertTrue(apply_manual(current, {}, lambda path: b''))

    def test_tracked_new_deleted_sources_and_determinism(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / 'x.py').write_text('def f():\n return 1\n')
            (root / '.gitignore').write_text('ignored.py\n')
            (root / 'ignored.py').write_text('bad syntax here')
            subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
            (root / 'new.lua').write_text('function f() end\n')
            first = inventory(root, include_untracked=True)
            self.assertEqual(first, inventory(root, include_untracked=True))
            self.assertEqual([f['path'] for f in first['files']], ['new.lua', 'x.py'])
            self.assertEqual([f['provenance'] for f in first['files']], ['untracked', 'tracked'])
            self.assertEqual([f['path'] for f in inventory(root)['files']], ['x.py'])
            self.assertEqual(inventory(root)['untracked_source_paths'], ['new.lua'])
            (root / 'x.py').unlink()
            self.assertEqual([f['path'] for f in inventory(root, include_untracked=True)['files']], ['new.lua'])
            (root / 'broken.py').symlink_to('missing-target')
            with self.assertRaisesRegex(ValueError, 'Source symlink'):
                inventory(root, include_untracked=True)

    def test_cli_comparison_and_no_implicit_bootstrap(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            data = b'# fixture\n' * 501
            (root / 'x.py').write_bytes(data)
            baseline = baseline_for(report('x.py', data))
            (root / 'baseline.json').write_text(json.dumps(baseline))
            subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
            subprocess.run(['git', '-C', str(root), '-c', 'user.name=thegandalf196',
                            '-c', 'user.email=thegandalf196@users.noreply.github.com',
                            'commit', '-qm', 'Synthetic inventory fixture'], check=True)
            command = [sys.executable, str(Path(__file__).resolve().parents[1] / 'ci/source_organization.py'),
                       'check', '--root', str(root), '--base-ref', 'HEAD', '--baseline', 'baseline.json']
            clean = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(clean.returncode, 0, clean.stderr + clean.stdout)
            self.assertEqual(json.loads(clean.stdout)['errors'], [])
            (root / 'x.py').write_bytes(data + b'# growth\n')
            grown = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(grown.returncode, 1)
            self.assertTrue(json.loads(grown.stdout)['errors'])
            (root / 'unreviewed.json').write_text(json.dumps(baseline))
            missing_base = subprocess.run(command[:-1] + ['unreviewed.json'], capture_output=True, text=True)
            self.assertEqual(missing_base.returncode, 2)
            self.assertIn('bootstrap needs independent review', missing_base.stderr)


if __name__ == '__main__':
    unittest.main()
