"""Private-output guards using synthetic bytes, never authored art inputs."""

from contextlib import ExitStack, redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import export_new_horizons_academy_map_v2 as v2
import export_new_horizons_academy_map_v3 as v3

EXPORTERS = (v2, v3)


class AcademyMapOutputRootTest(unittest.TestCase):
    def setUp(self):
        self.contexts = ExitStack()
        self.addCleanup(self.contexts.close)
        self.base = Path(self.contexts.enter_context(tempfile.TemporaryDirectory()))
        self.workspace = self.base / "private"
        self.workspace.mkdir()
        for module in EXPORTERS:
            self.contexts.enter_context(patch.object(module, "ROOT", module.CHECKOUT))
            self.contexts.enter_context(patch.object(module, "PRIVATE_ROOT", None))
        self.contexts.enter_context(patch.object(v3, "REVISION_ROOT", v3.CHECKOUT / v3.SOURCE_ROOT / "map-revisions/v3"))
        self.contexts.enter_context(patch.object(v3, "BASELINE_ROOT", v3.CHECKOUT / v3.SOURCE_ROOT / "map-revisions/v2"))

    def test_cli_requires_private_root_even_in_check_mode(self):
        for module in EXPORTERS:
            with self.subTest(module=module.__name__):
                with patch.object(sys, "argv", ["exporter", "--check"]), patch.object(module, "process") as process:
                    with redirect_stderr(StringIO()), self.assertRaises(SystemExit) as refusal:
                        module.main()
                    self.assertEqual(refusal.exception.code, 2)
                    process.assert_not_called()

    def test_cli_rejects_checkout_children_equal_and_ancestors_before_processing(self):
        for module in EXPORTERS:
            for root in (module.CHECKOUT, module.CHECKOUT / "generated", module.CHECKOUT.parent, Path("/")):
                with self.subTest(module=module.__name__, root=root):
                    argv = ["exporter", "--private-root", str(root), "--check"]
                    with patch.object(sys, "argv", argv), patch.object(module, "process") as process:
                        with redirect_stderr(StringIO()), self.assertRaises(SystemExit) as refusal:
                            module.main()
                        self.assertEqual(refusal.exception.code, 1)
                        process.assert_not_called()

    def test_external_root_rebases_every_absolute_authoring_path(self):
        for module in EXPORTERS:
            module.configure_private_root(self.workspace)
            self.assertEqual(module.ROOT, self.workspace)
            self.assertEqual(module.PRIVATE_ROOT, self.workspace)
        self.assertEqual(v2.ROOT / v2.REVISION_ROOT, self.workspace / v2.SOURCE_ROOT / "map-revisions/v2")
        self.assertEqual(v3.REVISION_ROOT, self.workspace / v3.SOURCE_ROOT / "map-revisions/v3")
        self.assertEqual(v3.BASELINE_ROOT, self.workspace / v3.SOURCE_ROOT / "map-revisions/v2")

    def test_private_root_cannot_use_a_symlink_or_symlinked_parent(self):
        alias = self.base / "alias"
        alias.symlink_to(self.workspace, target_is_directory=True)
        for module in EXPORTERS:
            for root in (alias, alias / "child"):
                with self.subTest(module=module.__name__, root=root):
                    with self.assertRaisesRegex(ValueError, "symlinks"):
                        module.configure_private_root(root)
                    self.assertIsNone(module.PRIVATE_ROOT)

    def test_writer_requires_configuration_for_write_and_check(self):
        for module in EXPORTERS:
            for check in (False, True):
                with self.subTest(module=module.__name__, check=check):
                    with self.assertRaisesRegex(ValueError, "explicit private root"):
                        module.write_or_check(self.workspace / "output", b"fixture", check)
        self.assertEqual(list(self.workspace.iterdir()), [])

    def test_writer_rejects_sibling_and_parent_traversal(self):
        for module in EXPORTERS:
            module.configure_private_root(self.workspace)
            for path in (self.base / "private-sibling/output", self.workspace / "../outside"):
                for check in (False, True):
                    with self.subTest(module=module.__name__, path=path, check=check):
                        with self.assertRaisesRegex(ValueError, "escapes the private root"):
                            module.write_or_check(path, b"fixture", check)
        self.assertFalse((self.base / "private-sibling").exists())
        self.assertFalse((self.base / "outside").exists())

    def test_writer_rejects_symlinked_file_and_parent_without_read_or_write(self):
        outside = self.base / "outside"
        outside.mkdir()
        target = outside / "output"
        target.write_bytes(b"untouched")
        (self.workspace / "parent").symlink_to(outside, target_is_directory=True)
        (self.workspace / "file").symlink_to(target)
        for module in EXPORTERS:
            module.configure_private_root(self.workspace)
            for path in (self.workspace / "parent/output", self.workspace / "file"):
                for check in (False, True):
                    with self.subTest(module=module.__name__, path=path, check=check):
                        with self.assertRaisesRegex(ValueError, "symlinks"):
                            module.write_or_check(path, b"replacement", check)
        self.assertEqual(target.read_bytes(), b"untouched")

    def test_replaced_private_root_is_rejected(self):
        replacement = self.base / "replacement"
        replacement.mkdir()
        for module in EXPORTERS:
            module.configure_private_root(self.workspace)
        self.workspace.rmdir()
        self.workspace.symlink_to(replacement, target_is_directory=True)
        for module in EXPORTERS:
            for check in (False, True):
                with self.assertRaisesRegex(ValueError, "symlinks"):
                    module.write_or_check(self.workspace / "output", b"fixture", check)
        self.assertEqual(list(replacement.iterdir()), [])

    def test_valid_write_and_read_only_check_preserve_exact_bytes(self):
        for index, module in enumerate(EXPORTERS):
            module.configure_private_root(self.workspace)
            path = self.workspace / str(index) / "output"
            with self.assertRaisesRegex(RuntimeError, "missing or stale"):
                module.write_or_check(path, b"fixture", True)
            self.assertFalse(path.parent.exists())
            module.write_or_check(path, b"fixture", False)
            before = path.stat()
            module.write_or_check(path, b"fixture", True)
            self.assertEqual(path.read_bytes(), b"fixture")
            self.assertEqual(path.stat().st_mtime_ns, before.st_mtime_ns)
            with self.assertRaisesRegex(RuntimeError, "missing or stale"):
                module.write_or_check(path, b"other bytes", True)
            self.assertEqual(path.read_bytes(), b"fixture")

    def test_process_preflights_all_outputs_before_loading_authoring_inputs(self):
        outside = self.base / "outside"
        outside.mkdir()
        for module in EXPORTERS:
            module.configure_private_root(self.workspace)
            revision = module.ROOT / module.REVISION_ROOT
            revision.mkdir(parents=True)
            (revision / "comparisons").symlink_to(outside, target_is_directory=True)
            operation = "export_body" if module is v2 else "build_revision"
            with patch.object(module, operation) as load:
                for check in (False, True):
                    with self.assertRaisesRegex(ValueError, "symlinks"):
                        module.process("fort", check) if module is v2 else module.process(check)
                load.assert_not_called()
            self.assertFalse((revision / "exports").exists())
            self.assertFalse((revision / "manifest.json").exists())
        self.assertEqual(list(outside.iterdir()), [])

    def test_v2_process_routes_only_synthetic_outputs_under_private_root(self):
        v2.configure_private_root(self.workspace)
        with patch.object(v2, "export_body", return_value=(None, None, {"synthetic": True})), \
                patch.object(v2, "png_bytes", return_value=b"synthetic export"), \
                patch.object(v2, "comparison_bytes", return_value=(b"synthetic comparison", {})), \
                redirect_stdout(StringIO()):
            v2.process("fort", False)
            before = {p: p.read_bytes() for p in self.workspace.rglob("*") if p.is_file()}
            self.assertEqual(len(before), 3)
            v2.process("fort", True)
            self.assertEqual({p: p.read_bytes() for p in before}, before)
        self.assertTrue(all(p.is_relative_to(self.workspace / v2.REVISION_ROOT) for p in before))

    def test_v3_process_routes_manifest_and_all_bodies_under_private_root(self):
        v3.configure_private_root(self.workspace)
        exports = {row["runtime"]: b"synthetic export" for row in v3.REGISTRATIONS.values()}
        comparisons = {name: (b"synthetic comparison", b"{}\n") for name in v3.REGISTRATIONS}
        with patch.object(v3, "build_revision", return_value=({"synthetic": True}, exports, comparisons)), \
                redirect_stdout(StringIO()):
            v3.process(False)
            before = {p: p.read_bytes() for p in self.workspace.rglob("*") if p.is_file()}
            self.assertEqual(len(before), 10)
            v3.process(True)
            self.assertEqual({p: p.read_bytes() for p in before}, before)
        self.assertTrue(all(p.is_relative_to(v3.REVISION_ROOT) for p in before))

    def test_cli_retains_name_order_and_check_semantics(self):
        for module in EXPORTERS:
            argv = ["exporter", "--private-root", str(self.workspace), "--check"]
            if module is v2:
                argv.extend(["--name", "village", "--name", "fort"])
            with patch.object(sys, "argv", argv), patch.object(module, "process") as process, \
                    redirect_stdout(StringIO()):
                self.assertEqual(module.main(), 0)
                self.assertEqual(module.ROOT, self.workspace)
                if module is v2:
                    self.assertEqual(process.call_args_list, [(("village", True),), (("fort", True),)])
                else:
                    process.assert_called_once_with(True)


if __name__ == "__main__":
    unittest.main()
