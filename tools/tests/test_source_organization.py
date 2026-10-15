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
from source_organization_inventory import classification, cpp_measurement_source, inventory, language, measure


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
    def normalized_cpp(self, data):
        from tree_sitter import Language, Parser
        import tree_sitter_cpp
        parser = Parser(Language(tree_sitter_cpp.language()))
        current = data
        for _ in range(3):
            updated = cpp_measurement_source(current, parser.parse(current).root_node)
            self.assertEqual(len(updated), len(data))
            self.assertEqual([i for i, byte in enumerate(updated) if byte == 10],
                             [i for i, byte in enumerate(data) if byte == 10])
            current = updated
        return current

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

    def test_calling_conventions_linkage_and_packing_recover_real_spans(self):
        data = b'''#define DLL_EXPORT __attribute__((visibility("default")))
#define MAIN_EXPORT DLL_EXPORT
class DLL_LINKAGE A {
public slots:
 void f() {
  for(auto & slots : values) { use(slots); }
 }
signals:
 void done();
};
static int SDLCALL read(void * p) { return 1; }
void CALLBACK callback(int x) { use(x); }
LONG WINAPI win(int x) { return x; }
int MAIN_EXPORT main() { return 0; }
VCMIQT_LINKAGE int helper() { return 2; }
PACKED_STRUCT_BEGIN struct Packed { int field; } PACKED_STRUCT_END;
int after() { return 3; }
'''
        normalized = self.normalized_cpp(data)
        self.assertIn(b'for(auto & slots : values)', normalized)
        self.assertTrue(normalized.startswith(data.split(b'class DLL_LINKAGE')[0]))
        file = measure('x.cpp', data)
        self.assertEqual(file['diagnostics'], [])
        self.assertEqual([(f['start'], f['end']) for f in file['functions']],
                         [(5, 7), (11, 11), (12, 12), (13, 13), (14, 14), (15, 15), (17, 17)])
        self.assertFalse(any('#define' in f['symbol'] or 'Packed' in f['symbol'] for f in file['functions']))

    def test_annotation_normalization_preserves_literals_definitions_and_identifier_uses(self):
        data = b'''#define DLL_LINKAGE __declspec(dllexport)
#define MACRO(x) CALLBACK x \\
 DLL_EXPORT x
#if defined(DLL_EXPORT)
#endif
// CALLBACK DLL_LINKAGE public slots: signals:
/* SDLCALL MAIN_EXPORT */
const char * text = R"tag(DLL_LINKAGE
public slots: signals: CALLBACK)tag";
const char * quoted = "Q_OBJECT SDLCALL";
int character = 'CALLBACK';
struct B { int signals:2; int DLL_EXPORT; };
int CALLBACK() { return 1; }
void ordinary() {
 int Q_OBJECT = 1;
 int DLL_LINKAGE = 2;
 for(auto & slots : values) { use(slots); }
 use(CALLBACK, DLL_LINKAGE, Q_OBJECT);
}
'''
        self.assertEqual(self.normalized_cpp(data), data)
        self.assertEqual(measure('x.cpp', data)['diagnostics'], [])
        # A known marker in an expression must not turn malformed code valid.
        malformed = b'void f() { return CALLBACK unexpected(); }'
        self.assertEqual(self.normalized_cpp(malformed), malformed)
        self.assertTrue(measure('x.cpp', malformed)['diagnostics'])

    def test_non_cpp_languages_do_not_receive_cpp_annotation_rewrites(self):
        data = b'function DLL_LINKAGE() return 1 end\n'
        file = measure('x.lua', data)
        self.assertEqual(file['diagnostics'], [])
        self.assertEqual(len(file['functions']), 1)
        self.assertIn('DLL_LINKAGE', file['functions'][0]['symbol'])

    def test_qt_declaration_contexts_and_noop_namespace_markers(self):
        data = b'''#define Q_OBJECT
#define VCMI_LIB_NAMESPACE_BEGIN
#define VCMI_LIB_NAMESPACE_END
VCMI_LIB_NAMESPACE_BEGIN
// Declaration documentation remains in place.
template<class T>
class Field { Q_OBJECT
 std::shared_ptr<T> data;
 int f() { return 1; }
};
class Nested { Q_OBJECT
 struct Value { int n; };
 int g() { return 2; }
};
class Enumerated { Q_OBJECT
 enum E { A };
 int h() { return 3; }
};
class Conditional { Q_OBJECT
#ifdef FEATURE
 int data;
#endif
 int i() { return 4; }
};
VCMI_LIB_NAMESPACE_END
'''
        self.normalized_cpp(data)
        file = measure('x.cpp', data)
        self.assertEqual(file['diagnostics'], [])
        self.assertEqual(len(file['functions']), 4)
        self.assertEqual([f['start'] for f in file['functions']], [9, 13, 17, 23])

    def test_marker_spellings_are_preserved_as_class_names_types_and_template_arguments(self):
        cases = [
            b'class CALLBACK { public: int f() { return 1; } };',
            b'class DLL_LINKAGE final { public: int f() { return 1; } };',
            b'using CALLBACK = int; CALLBACK value;',
            b'using DLL_LINKAGE = int; DLL_LINKAGE value;',
            b'namespace N { extern int value; } using DLL_LINKAGE=int; DLL_LINKAGE N::value;',
            b'std::vector<CALLBACK> value;',
            b'void f() { CALLBACK value; }',
            b'using Q_OBJECT = int; struct C { Q_OBJECT f() { return 1; } };',
        ]
        for data in cases:
            with self.subTest(data=data):
                self.assertEqual(self.normalized_cpp(data), data)
                self.assertEqual(measure('x.cpp', data)['diagnostics'], [])
        # Erasing a genuine multiline return type must not hide an 81-line unit.
        data = b'using CALLBACK=int;\nCALLBACK\nf()\n{\n' + b'// explanation\n' * 76 + b'return 1;\n}\n'
        file = measure('x.cpp', data)
        self.assertEqual(self.normalized_cpp(data), data)
        self.assertEqual(file['diagnostics'], [])
        self.assertEqual([(f['start'], f['end'], f['lines']) for f in file['functions']], [(2, 82, 81)])

    def test_multiline_real_annotation_remains_part_of_the_function_span(self):
        data = b'DLL_LINKAGE\nint f()\n{\n' + b'// explanation\n' * 76 + b'return 1;\n}\n'
        file = measure('x.cpp', data)
        self.assertEqual(file['diagnostics'], [])
        self.assertEqual([(f['start'], f['end'], f['lines']) for f in file['functions']], [(1, 81, 81)])
        qt = b'class C {\nQ_OBJECT\npublic slots:\nQ_INVOKABLE\nint f()\n{\nreturn 1;\n}\n};\n'
        file = measure('x.cpp', qt)
        self.assertEqual(file['diagnostics'], [])
        # Class markers/access labels are not part of the method signature.
        self.assertEqual([(f['start'], f['end'], f['lines']) for f in file['functions']], [(4, 8, 5)])

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

    def test_historical_mode_requires_exact_evidence_and_still_rejects_overlap(self):
        data = b'void CALLBACK callback() { return; }\n'
        parsed = report('x.cpp', data)
        baseline = {'coverage_pending': {'x.cpp': {'sha256': parsed['files'][0]['sha256'],
                    'owner': 'engine_engineer', 'reason': 'Historical calling convention gap'}}}
        original = copy.deepcopy(baseline)
        self.assertEqual(apply_manual(copy.deepcopy(parsed), baseline, lambda path: data,
                                      historical_baseline=True), [])
        self.assertEqual(baseline, original)
        self.assertTrue(apply_manual(copy.deepcopy(parsed), baseline, lambda path: data))
        for key in ('sha256', 'owner', 'reason'):
            invalid = copy.deepcopy(baseline)
            invalid['coverage_pending']['x.cpp'][key] = ''
            self.assertTrue(apply_manual(copy.deepcopy(parsed), invalid, lambda path: data,
                                         historical_baseline=True))
        overlap = copy.deepcopy(baseline)
        overlap['manual_functions'] = {'x.cpp': {'sha256': parsed['files'][0]['sha256'],
            'review': 'synthetic', 'rationale': 'Complete synthetic spans', 'functions': []}}
        self.assertTrue(apply_manual(copy.deepcopy(parsed), overlap, lambda path: data,
                                    historical_baseline=True))
        unsupported = report('fixture.nix', b'let x = 1; in x\n')
        pending = {'coverage_pending': {'fixture.nix': {'sha256': unsupported['files'][0]['sha256'],
                   'owner': 'engine_engineer', 'reason': 'Unsupported syntax'}}}
        for previous in ({'files': []}, report('fixture.nix', b'old bytes\n'), copy.deepcopy(unsupported)):
            self.assertTrue(apply_manual(copy.deepcopy(unsupported), pending, lambda path: b'',
                                         previous, {}, historical_baseline=True))

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

    def test_cli_parser_improvement_accepts_historical_freeze_but_rejects_current_one(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            data = b'void CALLBACK callback() { return; }\n'
            file = measure('x.cpp', data)
            self.assertEqual(file['diagnostics'], [])
            old_baseline = {'schema': 1, 'units': {}, 'coverage_pending': {'x.cpp': {
                'sha256': file['sha256'], 'owner': 'engine_engineer',
                'reason': 'Historical parser could not interpret the calling convention'}}}
            (root / 'x.cpp').write_bytes(data)
            (root / 'baseline.json').write_text(json.dumps(old_baseline))
            subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
            subprocess.run(['git', '-C', str(root), '-c', 'user.name=thegandalf196',
                            '-c', 'user.email=thegandalf196@users.noreply.github.com',
                            'commit', '-qm', 'Synthetic historical coverage freeze'], check=True)
            command = [sys.executable, str(Path(__file__).resolve().parents[1] / 'ci/source_organization.py'),
                       'check', '--root', str(root), '--base-ref', 'HEAD', '--baseline', 'baseline.json']
            (root / 'baseline.json').write_text(json.dumps({'schema': 1, 'units': {}}))
            clean = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(clean.returncode, 0, clean.stderr + clean.stdout)
            self.assertEqual(json.loads(clean.stdout)['coverage_pending'], [])
            (root / 'baseline.json').write_text(json.dumps(old_baseline))
            stale_current = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(stale_current.returncode, 1)
            self.assertEqual(json.loads(stale_current.stdout)['errors'], [
                'x.cpp: remove resolved/overlapping pending coverage entry'])


if __name__ == '__main__':
    unittest.main()
