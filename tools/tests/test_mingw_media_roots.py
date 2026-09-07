#!/usr/bin/env python3
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from package_new_horizons_windows import media_runtime_roots


class MinGWMediaRootsTest(unittest.TestCase):
    def test_only_verified_import_archive_alias_is_excluded(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'libpng16.dll').write_bytes(b'MZsynthetic PE identity is separately audited')
            (root / 'libpng16.dll.a').write_bytes(b'!<arch>\nsynthetic archive')
            (root / 'libpng.dll').symlink_to('libpng16.dll.a')
            graph = root / 'graph.json'
            graph.write_text(json.dumps({'graph': {'nodes': {'1': {'ref': 'sdl_image/3', 'package_folder': str(root), 'context': 'host'}}}}))
            legacy, _ = media_runtime_roots(graph)
            self.assertEqual(legacy, {'libpng.dll', 'libpng16.dll'})
            local, provenance = media_runtime_roots(graph, allow_mingw_import_archive_links=True)
            self.assertEqual(local, {'libpng16.dll'})
            self.assertEqual(provenance[0]['link_only_import_archives'][0]['target'], 'libpng16.dll.a')
            (root / 'libpng16.dll.a').write_bytes(b'not an import archive')
            rejected, _ = media_runtime_roots(graph, allow_mingw_import_archive_links=True)
            self.assertEqual(rejected, legacy)  # Must reach deployment failure, not be silently dropped.


if __name__ == '__main__':
    unittest.main()
