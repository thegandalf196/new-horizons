"""Process-free inventory vectors; no configure, compiler, assets or runtime."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('test_inventory', ROOT / 'tools/ci/test_inventory.py')
INVENTORY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INVENTORY)


class TestInventoryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'test').mkdir()
        (self.root / 'CMakeLists.txt').write_text('')
        (self.root / 'test/CMakeLists.txt').write_text('')
        self.policy = dict(roots=['test'], cmakeFiles=['CMakeLists.txt', 'test/CMakeLists.txt'],
                           excludedDirectories=['googletest'], unregistered={},
                           rootPolicies={'test': 'Native fixture prerequisites remain required.'})

    def test_unknown_unregistered_declaration_refuses(self):
        (self.root / 'test/Missing.cpp').write_text('TEST_F(Fixture, Example) {}')
        rows, errors = INVENTORY.inventory(self.root, self.policy)
        self.assertEqual(rows[0]['lexicalDeclarations'][0]['name'], 'Fixture.Example')
        self.assertEqual(len(errors), 1)

    def test_explicit_disposition_preserves_reason(self):
        (self.root / 'test/Exporter.cpp').write_text('TEST_F(Exporter, Write) {}')
        self.policy['unregistered']['test/Exporter.cpp'] = dict(classification='exporter', reason='Requires explicit private destination.')
        rows, errors = INVENTORY.inventory(self.root, self.policy)
        self.assertFalse(errors)
        self.assertEqual(rows[0]['classification'], 'exporter')

    def test_stale_disposition_refuses(self):
        self.policy['unregistered']['test/Removed.cpp'] = dict(classification='manual', reason='Old probe')
        self.assertTrue(INVENTORY.inventory(self.root, self.policy)[1])

    def test_conditional_reference_is_not_execution(self):
        (self.root / 'test/AI.cpp').write_text('TEST(AI, Choice) {}')
        (self.root / 'test/CMakeLists.txt').write_text('if(ENABLE_BATTLE_AI)\nlist(APPEND test_SRCS AI.cpp)\nendif()\n')
        rows, errors = INVENTORY.inventory(self.root, self.policy)
        self.assertFalse(errors)
        self.assertEqual(rows[0]['classification'], 'conditional')
        self.assertEqual(rows[0]['cmakeReferences'][0]['conditions'], ['if(ENABLE_BATTLE_AI)'])

    def test_registered_exporter_is_not_default(self):
        (self.root / 'test/Export.cpp').write_text('TEST(E, Write) { getenv("NH_EXPORT_MAP"); }')
        (self.root / 'test/CMakeLists.txt').write_text('list(APPEND test_SRCS Export.cpp)')
        rows, errors = INVENTORY.inventory(self.root, self.policy)
        self.assertFalse(errors)
        self.assertEqual(rows[0]['classification'], 'exporter')

    def test_missing_configured_target_is_reported(self):
        path = self.root / 'ctest.json'
        path.write_text(json.dumps({'tests': [{'name': 'Registered'}]}))
        row = INVENTORY.discovered_tests(path, ['Registered', 'Missing'])
        self.assertEqual(row['missingPureTargets'], ['Missing'])
        self.assertFalse(row['executionEstablished'])

    def test_actual_inventory_and_pure_contract(self):
        policy = json.loads((ROOT / 'tools/ci/test_inventory.json').read_text())
        self.assertFalse(INVENTORY.inventory(ROOT, policy)[1])
        self.assertFalse(INVENTORY.pure_contract(ROOT, policy))


if __name__ == '__main__':
    unittest.main()
