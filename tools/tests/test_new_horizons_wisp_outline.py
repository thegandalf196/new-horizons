import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('wisp_outline', Path(__file__).parents[1] / 'install_new_horizons_wisp_outline.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

checker_spec = importlib.util.spec_from_file_location(
    'wisp_outline_check', Path(__file__).parents[2] / 'client/tests/check-wisp-outline-mask.py')
checker = importlib.util.module_from_spec(checker_spec)
checker_spec.loader.exec_module(checker)


class WispOutlineTest(unittest.TestCase):
    def test_shipping_descriptors_bind_solid_threshold(self):
        for name in ('Wisp', 'WispUpgrade'):
            descriptor = json.loads((checker.ArtPath() / f'{name}.json').read_text())
            self.assertEqual(checker.outline_threshold(descriptor), 64)

    def test_descriptor_check_rejects_missing_or_wrong_group_binding(self):
        descriptor = {'sequences': [
            {'generateOverlay': 1, 'overlayAlphaThreshold': 64} for _ in range(32)]}
        self.assertEqual(checker.outline_threshold(descriptor), 64)
        for invalid in (None, 0, 63, 65, True, '64'):
            with self.subTest(invalid=invalid):
                descriptor['sequences'][17]['overlayAlphaThreshold'] = invalid
                with self.assertRaises(ValueError):
                    checker.outline_threshold(descriptor)
        del descriptor['sequences'][17]['overlayAlphaThreshold']
        with self.assertRaises(ValueError):
            checker.outline_threshold(descriptor)

    def test_only_thresholds_change_and_inputs_remain_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / 'source', root / 'output'
            descriptor = {'basepath': 'preserved/', 'sequences': [
                {'group': i, 'generateOverlay': 1, 'frames': ['unchanged.png']} for i in range(32)]}
            data = json.dumps(descriptor).encode()
            for name in ('Wisp', 'WispUpgrade'):
                path = source / f'Mods/new-horizons/Content/sprites/{name}.json'
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            module.stage(source, output)
            for name in ('Wisp', 'WispUpgrade'):
                relative = Path(f'Mods/new-horizons/Content/sprites/{name}.json')
                result = json.loads((output / relative).read_bytes())
                for sequence in result['sequences']:
                    self.assertEqual(sequence.pop('overlayAlphaThreshold'), 64)
                self.assertEqual(result, descriptor)
                self.assertEqual((source / relative).read_bytes(), data)
            self.assertEqual(len(list(output.rglob('*.json'))), 2)
            with self.assertRaises(FileExistsError):
                module.stage(source, output)


if __name__ == '__main__':
    unittest.main()
