import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('wisp_outline', Path(__file__).parents[1] / 'install_new_horizons_wisp_outline.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class WispOutlineTest(unittest.TestCase):
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
