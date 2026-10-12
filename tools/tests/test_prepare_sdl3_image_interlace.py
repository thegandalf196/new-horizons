"""Synthetic source-artifact controls; no providers, art, builds or processes."""
import difflib
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


TOOL = Path(__file__).resolve().parents[1] / 'ci/prepare_sdl3_image_interlace.py'
SPEC = importlib.util.spec_from_file_location('interlace_source_tool', TOOL)
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


class InterlaceSourcePreparationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        (self.source / 'src').mkdir(parents=True)
        self.destination = self.root / 'IMG_libpng.c'
        self.source_file = self.source / 'src/IMG_libpng.c'
        self.source_bytes = (b'/* Synthetic upstream license notice */\n'
                             b'int (*png_set_interlace_handling)(void *);\n'
                             b'FUNCTION_LOADER_LIBPNG(png_set_interlace_handling, fake)\n\n'
                             b'    lib.png_read_update_info(vars->png_ptr, vars->info_ptr);\n\n'
                             b'    lib.png_read_update_info(context->png_ptr, context->info_ptr);\n')
        self.source_file.write_bytes(self.source_bytes)
        self.cmake = self.source / 'CMakeLists.txt'
        self.cmake_bytes = b'set(MAJOR_VERSION 3)\nset(MINOR_VERSION 4)\nset(MICRO_VERSION 4)\n'
        self.cmake.write_bytes(self.cmake_bytes)
        self.patch_file = self.root / 'selected.patch'
        result = self.source_bytes
        for receiver in (b'vars', b'context'):
            marker = b'    lib.png_read_update_info(' + receiver + b'->png_ptr, ' + receiver + b'->info_ptr);'
            result = result.replace(marker, b'    lib.png_set_interlace_handling(' + receiver + b'->png_ptr);\n' + marker)
        self.expected = result
        self.patch_file.write_bytes(''.join(difflib.unified_diff(
            self.source_bytes.decode().splitlines(keepends=True), result.decode().splitlines(keepends=True),
            fromfile='a/src/IMG_libpng.c', tofile='b/src/IMG_libpng.c')).encode('utf-8'))
        for name, value in (('SOURCE_SHA256', M.digest(self.source_bytes)),
                            ('CMAKE_SHA256', M.digest(self.cmake_bytes)), ('PATCH', self.patch_file)):
            context = patch.object(M, name, value)
            context.start()
            self.addCleanup(context.stop)

    def test_check_only_preserves_all_inputs_and_creates_nothing(self):
        before = {p: p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        result = M.prepare(self.source)
        self.assertFalse(result['published'])
        self.assertEqual(result['patchedSourceSha256'], M.digest(self.expected))
        self.assertEqual({p: p.read_bytes() for p in self.root.rglob('*') if p.is_file()}, before)

    @unittest.skipUnless(hasattr(os, 'getuid'), 'Publication requires POSIX ownership')
    def test_new_artifact_contains_only_two_calls_and_preserves_license_and_source(self):
        result = M.prepare(self.source, self.destination)
        self.assertTrue(result['published'])
        self.assertEqual(self.destination.read_bytes(), self.expected)
        self.assertEqual(self.source_file.read_bytes(), self.source_bytes)
        self.assertEqual(self.destination.stat().st_mode & 0o777, 0o600)
        self.assertIn(b'Synthetic upstream license notice', self.destination.read_bytes())
        self.assertFalse(list(self.root.glob('.interlace-source-*')))

    def test_modified_source_is_refused(self):
        self.source_file.write_bytes(self.source_bytes + b'changed')
        with self.assertRaisesRegex(RuntimeError, 'modified IMG_libpng'):
            M.prepare(self.source, self.destination)
        self.assertFalse(self.destination.exists())

    def test_post_stat_growth_is_refused_with_a_bounded_read(self):
        class GrowingSource(io.BytesIO):
            def read(self, size=-1):
                self.requested_size = size
                return super().read(size)

        stream = GrowingSource(b'x' * (M.LIMIT + 2))
        # The real file still supplies a small stat; only its opened contents
        # model growth between stat and read, without allocating an unbound read.
        with patch.object(Path, 'open', return_value=stream), self.assertRaisesRegex(
                RuntimeError, 'grew beyond bound'):
            M.prepare(self.source, self.destination)
        self.assertEqual(stream.requested_size, M.LIMIT + 1)
        self.assertFalse(self.destination.exists())

    def test_wrong_version_is_refused_even_when_cmake_hash_matches(self):
        data = self.cmake_bytes.replace(b'MICRO_VERSION 4', b'MICRO_VERSION 5')
        self.cmake.write_bytes(data)
        with patch.object(M, 'CMAKE_SHA256', M.digest(data)), self.assertRaisesRegex(RuntimeError, 'source version'):
            M.prepare(self.source, self.destination)

    def test_patch_drift_is_refused(self):
        self.patch_file.write_bytes(self.patch_file.read_bytes() + b'unselected change\n')
        with self.assertRaisesRegex(RuntimeError, 'Maintained patch differs'):
            M.prepare(self.source, self.destination)

    @unittest.skipUnless(hasattr(os, 'getuid'), 'Publication requires POSIX ownership')
    def test_existing_and_linked_output_are_refused_without_overwrite(self):
        self.destination.write_bytes(b'retain')
        with self.assertRaisesRegex(RuntimeError, 'Existing output'):
            M.prepare(self.source, self.destination)
        self.assertEqual(self.destination.read_bytes(), b'retain')
        self.destination.unlink()
        self.destination.symlink_to(self.source_file)
        with self.assertRaisesRegex(RuntimeError, 'Existing output'):
            M.prepare(self.source, self.destination)
        self.assertEqual(self.source_file.read_bytes(), self.source_bytes)

    @unittest.skipUnless(hasattr(os, 'getuid'), 'Publication requires POSIX ownership')
    def test_atomic_publication_failure_removes_only_owned_temporary(self):
        with patch.object(M.os, 'link', side_effect=PermissionError('synthetic write refusal')):
            with self.assertRaises(PermissionError):
                M.prepare(self.source, self.destination)
        self.assertFalse(self.destination.exists())
        self.assertFalse(list(self.root.glob('.interlace-source-*')))
        self.assertEqual(self.source_file.read_bytes(), self.source_bytes)

    @unittest.skipUnless(hasattr(os, 'getuid'), 'Publication requires POSIX ownership')
    def test_racing_output_is_not_replaced(self):
        def race(*args, **kwargs):
            self.destination.write_bytes(b'new owner')
            raise FileExistsError('synthetic publication race')
        with patch.object(M.os, 'link', side_effect=race), self.assertRaises(FileExistsError):
            M.prepare(self.source, self.destination)
        self.assertEqual(self.destination.read_bytes(), b'new owner')
        self.assertFalse(list(self.root.glob('.interlace-source-*')))

    @unittest.skipUnless(hasattr(os, 'getuid'), 'Publication requires POSIX ownership')
    def test_source_tree_output_and_nonprivate_parent_are_refused(self):
        (self.source / 'src').chmod(0o700)
        with self.assertRaisesRegex(RuntimeError, 'In-place'):
            M.prepare(self.source, self.source_file)
        self.root.chmod(0o755)
        with self.assertRaisesRegex(RuntimeError, 'owned0700'):
            M.prepare(self.source, self.destination)
        self.root.chmod(0o700)

    def test_output_is_refused_without_posix_ownership_support(self):
        with patch.object(M, 'os', SimpleNamespace()):
            # Check-only remains available on the unsupported writer platform.
            self.assertFalse(M.prepare(self.source)['published'])
            with self.assertRaisesRegex(RuntimeError, 'Publication requires POSIX'):
                M.prepare(self.source, self.destination)
        self.assertFalse(self.destination.exists())
        self.assertFalse(list(self.root.glob('.interlace-source-*')))

    def test_linked_source_or_missing_api_is_refused(self):
        alias = self.root / 'alias'
        alias.symlink_to(self.source, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, 'Linked'):
            M.prepare(alias)
        source = self.source_bytes.replace(b'FUNCTION_LOADER_LIBPNG', b'missing_loader')
        self.source_file.write_bytes(source)
        with patch.object(M, 'SOURCE_SHA256', M.digest(source)), self.assertRaisesRegex(RuntimeError, 'API declaration/loader'):
            M.prepare(self.source)


if __name__ == '__main__':
    unittest.main()
