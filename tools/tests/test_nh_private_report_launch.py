# SPDX-License-Identifier: GPL-2.0-or-later
"""Process-free report-only admission and managed dispatch regressions."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    "nh_private_report_launch", Path(__file__).with_name("nh-private-launch.py"))
HELPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPER)


class PrivateReportLaunchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profile = self.root / "private profile"
        self.profile.mkdir(mode=0o700)
        (self.profile / ".nh-profile").write_bytes(b"New Horizons launcher profile v1\n")
        self.store = self.profile / "cache" / "vcmi" / "crash-reports"
        self.store.mkdir(parents=True, mode=0o700)
        for directory in (self.profile / "cache", self.store.parent):
            directory.chmod(0o700)
        self.session = self.store / "run-0123456789abcdef"
        self.session.mkdir(mode=0o700)
        self.snapshot = self.root / "snapshot"
        self.snapshot.mkdir()
        (self.snapshot / "new-horizons").write_bytes(b"synthetic image")
        (self.snapshot / "new-horizons").chmod(0o500)
        (self.snapshot / "libvcmi.so").write_bytes(b"synthetic facade")
        self.assets = self.root / "assets"
        self.assets.mkdir()
        self.launcher = self.root / "launcher"
        self.launcher.write_text("not executed")
        self.launcher.chmod(0o500)

    def argv(self, selected="manual", *tail):
        return ["--guard", str(self.root / "not-a-real-display-guard"),
                "--snapshot", str(self.snapshot), "--profile", str(self.profile),
                "--assets", str(self.assets), "--report-session", selected, *tail]

    def assert_refused_before_dispatch(self, arguments):
        with mock.patch.object(HELPER, "verify_owned_xvfb") as display, \
                mock.patch.object(HELPER.subprocess, "run") as run, \
                mock.patch.object(HELPER.subprocess, "Popen") as popen, \
                self.assertRaises(SystemExit) as error:
            HELPER.main(arguments)
        self.assertEqual(error.exception.code, 2)
        run.assert_not_called()
        popen.assert_not_called()
        display.assert_not_called()

    def test_manual_pending_exact_translate_with_empty_manual_argv(self):
        for selected, expected in (("manual", ""), ("pending", "pending"),
                                   (str(self.session), str(self.session))):
            with self.subTest(selected=selected), mock.patch.dict(os.environ, {}, clear=True), \
                    mock.patch.object(HELPER, "verify_owned_xvfb") as display, \
                    mock.patch.object(HELPER, "LAUNCHER", self.launcher), \
                    mock.patch.object(HELPER.subprocess, "run", return_value=mock.Mock(returncode=17)) as run:
                self.assertEqual(HELPER.main(self.argv(selected)), 17)
                display.assert_called_once()
                command = run.call_args.args[0]
                self.assertEqual(command[-3:], ["--", "--report-bug", expected])
                self.assertEqual(command[0], str(self.launcher))
                environment = run.call_args.kwargs["env"]
                for key in ("SDL_AUDIODRIVER", "SDL_AUDIO_DRIVER"):
                    self.assertEqual(environment[key], "dummy")
                self.assertEqual(environment["DISPLAY"], ":191")
                self.assertNotIn("WAYLAND_DISPLAY", environment)

    def test_verify_only_keeps_managed_route_and_real_guard_requirement(self):
        with mock.patch.object(HELPER, "verify_owned_xvfb", side_effect=HELPER.Refusal("no display")), \
                mock.patch.object(HELPER.subprocess, "run") as run:
            with self.assertRaises(SystemExit):
                HELPER.main(self.argv("manual", "--verify-only"))
            run.assert_not_called()
        with mock.patch.object(HELPER, "verify_owned_xvfb"), \
                mock.patch.object(HELPER, "LAUNCHER", self.launcher), \
                mock.patch.object(HELPER.subprocess, "run", return_value=mock.Mock(returncode=0)) as run:
            self.assertEqual(HELPER.main(self.argv("manual", "--verify-only")), 0)
            self.assertEqual(run.call_args.args[0][-4:], ["--verify-only", "--", "--report-bug", ""])

    def test_duplicate_and_all_trailing_cli_or_other_modes_refuse(self):
        for tail in (("--report-session", "pending"), ("--debugger-log", str(self.root / "gdb.log")),
                     ("--wine-fullgame", str(self.root / "client.exe")),
                     ("--wine-render-load-timings",), ("--", "--disable-video"),
                     ("--", "--testmap", "Maps/a.vmap"), ("--", "--spectate"),
                     ("--", "--report-bug", ""), ("--", "--onlyAI")):
            with self.subTest(tail=tail):
                self.assert_refused_before_dispatch(self.argv("manual", *tail))

    def test_fixture_environment_refuses_report_mode(self):
        for key in ("NH_CURE_BOOTSTRAP", "NH_CURE_FROZEN_FIXTURE"):
            with self.subTest(key=key), mock.patch.dict(os.environ, {key: "ordinary44b24"}, clear=True):
                self.assert_refused_before_dispatch(self.argv())

    def test_external_relative_traversal_malformed_or_missing_session_refuse(self):
        values = ("", "latest", "run-0123456789abcdef", str(self.root / self.session.name),
                  str(self.store / "run-UPPER0123456789abcdef"), str(self.store / "run-0123"),
                  str(self.store / "run-0123456789abcdef0" / ".." / self.session.name),
                  str(self.store) + "//" + self.session.name,
                  str(self.store / "run-aaaaaaaaaaaaaaaa"))
        for value in values:
            with self.subTest(value=value):
                self.assert_refused_before_dispatch(self.argv(value))

    def test_symlink_at_each_profile_log_or_session_component_refuses(self):
        for component in (self.profile, self.profile / "cache", self.store.parent, self.store, self.session):
            with self.subTest(component=component):
                moved = component.with_name(component.name + "-original")
                component.rename(moved)
                component.symlink_to(moved, target_is_directory=True)
                try:
                    self.assert_refused_before_dispatch(self.argv(str(self.session)))
                finally:
                    component.unlink()
                    moved.rename(component)

    def test_unmanaged_public_or_foreign_profile_and_logs_refuse(self):
        marker = self.profile / ".nh-profile"
        marker.write_text("wrong marker")
        self.assert_refused_before_dispatch(self.argv())
        marker.write_bytes(b"New Horizons launcher profile v1\n")
        for component in (self.profile, self.store.parent, self.session):
            component.chmod(0o755)
            try:
                self.assert_refused_before_dispatch(self.argv(str(self.session)))
            finally:
                component.chmod(0o700)
        with mock.patch.object(HELPER.os, "getuid", return_value=os.getuid() + 1):
            self.assert_refused_before_dispatch(self.argv())

    def test_marker_symlink_missing_store_and_non_directory_session_refuse(self):
        marker = self.profile / ".nh-profile"
        original = marker.with_name("marker-original")
        marker.rename(original)
        marker.symlink_to(original)
        self.assert_refused_before_dispatch(self.argv())
        marker.unlink()
        original.rename(marker)
        self.session.rmdir()
        self.session.write_text("not a directory")
        self.assert_refused_before_dispatch(self.argv(str(self.session)))
        self.session.unlink()
        self.store.rmdir()
        self.assert_refused_before_dispatch(self.argv("pending"))
        self.assert_refused_before_dispatch(self.argv("manual"))

    def test_raw_report_argument_still_not_allowlisted(self):
        with mock.patch.dict(os.environ, {}, clear=True), self.assertRaises(HELPER.Refusal):
            HELPER.validate_client_args(["--report-bug", ""])


if __name__ == "__main__":
    unittest.main()
