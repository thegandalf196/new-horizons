# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the fail-closed, private-display launch wrapper."""
import importlib.util
import json
import os
from pathlib import Path
import socket
from types import SimpleNamespace
import tempfile
import unittest
from unittest import mock


HELPER_PATH = Path(__file__).with_name("nh-private-launch.py")
SPEC = importlib.util.spec_from_file_location("nh_private_launch", HELPER_PATH)
HELPER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPER)


class PrivateLaunchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.proc_root = self.root / "proc"
        self.pid = 7319
        process = self.proc_root / str(self.pid)
        process.mkdir(parents=True)
        self.start_ticks = "987654"
        stat_fields = ["S"] + ["0"] * 18 + [self.start_ticks]
        (process / "stat").write_text("7319 (Xvfb) " + " ".join(stat_fields), encoding="ascii")
        (process / "cmdline").write_bytes(
            b"/usr/bin/Xvfb\x00:191\x00-nolisten\x00tcp\x00-screen\x000\x001280x800x24\x00"
        )
        self.xvfb_executable = self.root / "bin" / "Xvfb"
        self.xvfb_executable.parent.mkdir()
        self.xvfb_executable.write_bytes(b"synthetic Xvfb executable placeholder\n")
        self.xvfb_executable.chmod(0o755)
        (process / "exe").symlink_to(self.xvfb_executable)
        self.boot_id_path = self.root / "boot_id"
        self.boot_id = "d916fa5f-9d12-4f87-a813-9581d728ef55"
        self.boot_id_path.write_text(self.boot_id + "\n", encoding="ascii")
        self.display_socket = self.root / "X191"
        self.x_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.x_socket.bind(str(self.display_socket))
        self.guard = self.root / "display-guard.json"
        self._write_guard()

        self.snapshot = self.root / "candidate snapshot"
        self.snapshot.mkdir()
        client = self.snapshot / "vcmiclient"
        client.write_text("synthetic client placeholder\n", encoding="utf-8")
        client.chmod(0o755)
        (self.snapshot / "libvcmi.so").write_bytes(b"synthetic library placeholder\n")
        self.assets = self.root / "purchaser assets"
        self.assets.mkdir()
        self.profile = self.root / "private NH profile"
        self.launcher = self.root / "new-horizons-launch.sh"
        self.launcher.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        self.launcher.chmod(0o755)

    def tearDown(self):
        self.x_socket.close()
        self.temp.cleanup()

    def _write_guard(self):
        self.guard.write_text(json.dumps({
            "pid": self.pid,
            "start_ticks": self.start_ticks,
            "boot_id": self.boot_id,
            "socket_inode": self.display_socket.stat().st_ino,
        }), encoding="utf-8")

    def _patch_boundaries(self):
        stack = mock.patch.multiple(
            HELPER,
            PROC_ROOT=self.proc_root,
            BOOT_ID_PATH=self.boot_id_path,
            DISPLAY_SOCKET=self.display_socket,
            LAUNCHER=self.launcher,
        )
        return stack

    def _args(self, *tail):
        return [
            "--guard", str(self.guard),
            "--snapshot", str(self.snapshot),
            "--profile", str(self.profile),
            "--assets", str(self.assets),
            *tail,
        ]

    def test_parent_host_display_is_never_inherited(self):
        with self._patch_boundaries(), \
                mock.patch.dict(os.environ, {
                    "DISPLAY": ":0",
                    "WAYLAND_DISPLAY": "wayland-0",
                    "SDL_VIDEODRIVER": "wayland",
                    "SDL_AUDIODRIVER": "pulse",
                    "SDL_VIDEO_DRIVER": "wayland",
                    "SDL_AUDIO_DRIVER": "pulse",
                }), \
                mock.patch.object(HELPER.subprocess, "run", return_value=mock.Mock(returncode=0)) as run:
            result = HELPER.main(self._args("--verify-only", "--", "--disable-video"))

        self.assertEqual(result, 0)
        command = run.call_args.args[0]
        environment = run.call_args.kwargs["env"]
        self.assertEqual(command[1:], [
            "--client", str(self.snapshot / "vcmiclient"),
            "--resources", str(self.snapshot),
            "--profile", str(self.profile),
            "--assets", str(self.assets),
            "--verify-only", "--", "--disable-video",
        ])
        self.assertEqual(environment["DISPLAY"], ":191")
        self.assertEqual(environment["SDL_VIDEODRIVER"], "x11")
        self.assertEqual(environment["SDL_AUDIODRIVER"], "dummy")
        self.assertEqual(environment["SDL_VIDEO_DRIVER"], "x11")
        self.assertEqual(environment["SDL_AUDIO_DRIVER"], "dummy")
        self.assertNotIn("WAYLAND_DISPLAY", environment)

    def test_missing_and_stale_guards_refuse_before_subprocess(self):
        with self._patch_boundaries(), mock.patch.object(
                HELPER.subprocess, "run", return_value=mock.Mock(returncode=0)) as run:
            missing = self.root / "missing-guard.json"
            missing_args = self._args("--verify-only")
            missing_args[missing_args.index("--guard") + 1] = str(missing)
            with self.assertRaises(SystemExit):
                HELPER.main(missing_args)
            run.assert_not_called()

            (self.proc_root / str(self.pid) / "stat").write_text(
                "7319 (Xvfb) " + " ".join(["S"] + ["0"] * 18 + ["123"]),
                encoding="ascii",
            )
            with self.assertRaises(SystemExit):
                HELPER.main(self._args("--verify-only"))
            run.assert_not_called()

    def test_wrong_boot_or_command_refuses_before_subprocess(self):
        with self._patch_boundaries(), mock.patch.object(HELPER.subprocess, "run") as run:
            self.boot_id_path.write_text("different-boot\n", encoding="ascii")
            with self.assertRaises(SystemExit):
                HELPER.main(self._args("--verify-only"))
            run.assert_not_called()

            self.boot_id_path.write_text(self.boot_id + "\n", encoding="ascii")
            (self.proc_root / str(self.pid) / "cmdline").write_bytes(
                b"/usr/bin/Xvfb\x00:0\x00-nolisten\x00tcp\x00"
            )
            with self.assertRaises(SystemExit):
                HELPER.main(self._args("--verify-only"))
            run.assert_not_called()

    def test_argv0_cannot_substitute_for_real_xvfb_executable(self):
        with self._patch_boundaries(), mock.patch.object(HELPER.subprocess, "run") as run:
            impostor = self.root / "bin" / "not-Xvfb"
            impostor.write_bytes(b"synthetic impostor executable placeholder\n")
            impostor.chmod(0o755)
            executable_link = self.proc_root / str(self.pid) / "exe"
            executable_link.unlink()
            executable_link.symlink_to(impostor)
            with self.assertRaises(SystemExit):
                HELPER.main(self._args("--verify-only"))
            run.assert_not_called()

    def test_wrong_socket_inode_or_uid_refuses_before_subprocess(self):
        with self._patch_boundaries(), mock.patch.object(HELPER.subprocess, "run") as run:
            saved = json.loads(self.guard.read_text(encoding="utf-8"))
            saved["socket_inode"] += 1
            self.guard.write_text(json.dumps(saved), encoding="utf-8")
            with self.assertRaises(SystemExit):
                HELPER.main(self._args("--verify-only"))
            run.assert_not_called()

            self._write_guard()
            with mock.patch.object(HELPER.os, "getuid", return_value=os.getuid() + 1):
                with self.assertRaises(SystemExit):
                    HELPER.main(self._args("--verify-only"))
            run.assert_not_called()

            self.x_socket.close()
            self.display_socket.unlink()
            self.display_socket.write_bytes(b"not a UNIX socket")
            self._write_guard()
            with self.assertRaises(SystemExit):
                HELPER.main(self._args("--verify-only"))
            run.assert_not_called()

    def test_socket_must_be_owned_by_the_current_uid(self):
        original_stat = Path.stat

        def foreign_socket_owner(path, *args, **kwargs):
            result = original_stat(path, *args, **kwargs)
            if path == self.display_socket:
                return SimpleNamespace(st_mode=result.st_mode, st_uid=os.getuid() + 1,
                                       st_ino=result.st_ino)
            return result

        with self._patch_boundaries(), mock.patch.object(HELPER.subprocess, "run") as run, \
                mock.patch.object(Path, "stat", new=foreign_socket_owner):
            with self.assertRaises(SystemExit):
                HELPER.main(self._args("--verify-only"))
            run.assert_not_called()

    def test_client_args_are_allowlisted_and_paths_are_argv_not_shell(self):
        marker = self.root / "should-not-be-created"
        hostile = f"--testmap Maps/fake.h3m; touch {marker}"
        with self._patch_boundaries(), mock.patch.object(
                HELPER.subprocess, "run", return_value=mock.Mock(returncode=0)) as run:
            with self.assertRaises(SystemExit):
                HELPER.main(self._args("--", hostile))
            run.assert_not_called()
            self.assertFalse(marker.exists())

            odd_profile = self.root / "private profile; touch should-not-exist"
            argv = self._args("--verify-only")
            argv[argv.index(str(self.profile))] = str(odd_profile)
            result = HELPER.main(argv)

        self.assertEqual(result, 0)
        passed = run.call_args.args[0]
        self.assertIsInstance(passed, list)
        self.assertIn(str(odd_profile), passed)
        self.assertFalse(run.call_args.kwargs.get("shell", False))
        self.assertFalse((self.root / "should-not-exist").exists())

    def test_shell_metacharacters_stay_inside_one_safe_client_argument(self):
        map_name = "Maps/Map; touch should-not-exist.h3m"
        with self._patch_boundaries(), mock.patch.object(
                HELPER.subprocess, "run", return_value=mock.Mock(returncode=0)) as run:
            HELPER.main(self._args("--verify-only", "--", "--testmap", map_name))

        passed = run.call_args.args[0]
        marker = passed.index("--testmap")
        self.assertEqual(passed[marker + 1], map_name)
        self.assertFalse(run.call_args.kwargs.get("shell", False))
        self.assertFalse((self.root / "should-not-exist.h3m").exists())


if __name__ == "__main__":
    unittest.main()
