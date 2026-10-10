# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the fail-closed, private-display launch wrapper."""
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
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
        client = self.snapshot / "new-horizons"
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
            WINE_INSTALL_ROOT=self.root / "usr",
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

    def test_debugger_is_explicitly_forwarded_on_managed_route(self):
        log = self.profile / "debugger log.txt"
        with self._patch_boundaries(), \
                mock.patch.object(HELPER.subprocess, "run", return_value=mock.Mock(returncode=255)) as run:
            result = HELPER.main(self._args("--debugger-log", str(log), "--", "--disable-video"))
        self.assertEqual(result, 255)
        command = run.call_args.args[0]
        self.assertEqual(command[0], str(self.launcher))
        boundary = command.index("--debugger-log")
        self.assertEqual(command[boundary:boundary + 2], ["--debugger-log", str(log)])
        self.assertEqual(command[-2:], ["--", "--disable-video"])
        self.assertEqual(run.call_args.kwargs["env"]["DISPLAY"], HELPER.PRIVATE_DISPLAY)

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
            "--client", str(self.snapshot / "new-horizons"),
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

    def test_historical_client_name_is_preserved(self):
        (self.snapshot / 'new-horizons').rename(self.snapshot / 'vcmiclient')
        with self._patch_boundaries(), mock.patch.object(HELPER.subprocess, 'run') as run:
            run.return_value.returncode = 0
            result = HELPER.main(self._args('--verify-only'))
        self.assertEqual(result, 0)
        command = run.call_args.args[0]
        self.assertEqual(command[command.index('--client') + 1], str(self.snapshot / 'vcmiclient'))

    def test_ambiguous_current_and_legacy_clients_refused(self):
        (self.snapshot / 'vcmiclient').write_bytes((self.snapshot / 'new-horizons').read_bytes())
        (self.snapshot / 'vcmiclient').chmod(0o755)
        with self._patch_boundaries(), mock.patch.object(HELPER.subprocess, 'run') as run:
            with self.assertRaises(SystemExit):
                HELPER.main(self._args('--verify-only'))
        run.assert_not_called()

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

    def _wine_args(self):
        executable = self.root / "nhSdl3GrayscaleMaskRuntimeTest.exe"
        image = bytearray(128)
        image[:2] = b"MZ"
        struct.pack_into("<I", image, 60, 64)
        image[64:68] = b"PE\0\0"
        struct.pack_into("<H", image, 68, 0x8664)
        struct.pack_into("<H", image, 86, 2)
        executable.write_bytes(image)
        fixtures = self.root / "masks"
        fixtures.mkdir()
        manifest = {}
        for name in ("archMage", "genie", "giant", "gremlin", "ironGolem", "mage",
                     "masterGenie", "masterGremlin", "nagaQueen", "naga", "stoneGolem", "titan"):
            filename = f"NH_academy_{name}_portrait_mask.png"
            (fixtures / filename).write_bytes(name.encode())
            manifest[filename] = hashlib.sha256(name.encode()).hexdigest()
        receipt = self.root / "fixture-manifest.json"
        receipt.write_text(json.dumps(manifest))
        dll_manifest = {}
        for name in ("SDL3.dll", "SDL3_image.dll", "libpng16.dll", "zlib1.dll",
                     "vcruntime140.dll", "vcruntime140_1.dll"):
            (self.root / name).write_bytes(name.encode())
            dll_manifest[name] = hashlib.sha256(name.encode()).hexdigest()
        dll_receipt = self.root / "dll-manifest.json"
        dll_receipt.write_text(json.dumps(dll_manifest))
        run_root = self.root / "wine-run"
        run_root.mkdir(mode=0o700)
        return ["--guard", str(self.guard), "--wine-surface-test", str(executable),
                "--wine-test-sha256", hashlib.sha256(image).hexdigest(),
                "--wine-sha256", "0" * 64, "--wine-binary", "/usr/lib/wine/wine64",
                "--wine-fixtures", str(fixtures),
                "--wine-fixture-manifest", str(receipt),
                "--wine-fixture-manifest-sha256", hashlib.sha256(receipt.read_bytes()).hexdigest(),
                "--wine-dll-manifest", str(dll_receipt),
                "--wine-dll-manifest-sha256", hashlib.sha256(dll_receipt.read_bytes()).hexdigest(),
                "--wine-run-root", str(run_root)]

    def _wine_pin(self):
        original = HELPER._pinned_file
        wine = self.root / "usr" / "wine64"
        wine.parent.mkdir(exist_ok=True)
        wine.write_bytes(b"\x7fELFsynthetic")
        wine.chmod(0o755)
        server = wine.parent / "wineserver"
        server.write_bytes(b"synthetic")
        server.chmod(0o755)
        return mock.patch.object(HELPER, "_pinned_file", side_effect=lambda path, digest:
                                 wine if path == Path("/usr/lib/wine/wine64")
                                 else original(path, digest))

    def test_wine_dry_run_pins_exact_pe_and_twelve_fixtures_without_spawn(self):
        args = self._wine_args()
        with self._patch_boundaries(), self._wine_pin(), mock.patch.object(HELPER.subprocess, "Popen") as spawn:
            self.assertEqual(HELPER.main([*args, "--verify-only"]), 0)
        spawn.assert_not_called()

    def test_wine_refuses_reused_prefix_arbitrary_args_and_wrong_pe_hash(self):
        args = self._wine_args()
        cases = [args + ["--", "--anything"],
                 ["incorrect" if value == args[args.index("--wine-test-sha256") + 1] else value
                  for value in args], args + ["--snapshot", str(self.snapshot)]]
        with self._patch_boundaries(), self._wine_pin(), mock.patch.object(HELPER.subprocess, "Popen") as spawn:
            for case in cases:
                with self.assertRaises(SystemExit) as rejected:
                    HELPER.main(case)
                self.assertEqual(rejected.exception.code, 2)
            (self.root / "wine-run" / "prefix").mkdir()
            with self.assertRaises(SystemExit):
                HELPER.main(args)
        spawn.assert_not_called()

    def test_wine_rejects_changed_extra_or_symlink_fixture(self):
        args = self._wine_args()
        path = self.root / "masks" / "NH_academy_genie_portrait_mask.png"
        path.write_bytes(b"changed")
        with self._patch_boundaries(), self._wine_pin(), mock.patch.object(HELPER.subprocess, "Popen") as spawn:
            with self.assertRaises(SystemExit):
                HELPER.main(args)
        spawn.assert_not_called()

    def test_wine_actual_child_environment_is_private_and_cleanup_is_prefix_scoped(self):
        args = self._wine_args()
        child = mock.Mock(pid=9021)
        child.wait.return_value = 0
        child.poll.return_value = 0
        def spawn(command, **kwargs):
            environment = kwargs["env"]
            self.assertEqual(kwargs["start_new_session"], True)
            self.assertNotIn("PULSE_SERVER", environment)
            self.assertNotIn("PIPEWIRE_REMOTE", environment)
            self.assertNotIn("WAYLAND_DISPLAY", environment)
            for name in ("SDL_VIDEODRIVER", "SDL_VIDEO_DRIVER", "SDL_AUDIODRIVER", "SDL_AUDIO_DRIVER"):
                self.assertEqual(environment[name], "dummy")
            self.assertEqual(environment["WINEDLLOVERRIDES"],
                             "SDL3,SDL3_image=n;libpng16,zlib1,vcruntime140,vcruntime140_1=n,b;mscoree,mshtml=")
            process = self.proc_root / str(child.pid)
            process.mkdir()
            (process / "environ").write_bytes(b"\0".join(
                key.encode() + b"=" + value.encode() for key, value in environment.items()) + b"\0")
            (process / "exe").symlink_to(command[0])
            (process / "stat").write_text("9021 (stopped) " + " ".join(["T"] + ["0"] * 18 + ["3456"]))
            (self.root / "wine-run" / "pe-environment-proof.txt").write_bytes(b"WINE_PRIVATE_ENV_VERIFIED\n")
            return child
        with self._patch_boundaries(), self._wine_pin(), \
                mock.patch.object(HELPER.subprocess, "Popen", side_effect=spawn), \
                mock.patch.object(HELPER.os, "kill") as resume, \
                mock.patch.object(HELPER.subprocess, "run", return_value=mock.Mock(returncode=0)) as cleanup:
            self.assertEqual(HELPER.main(args), 0)
        self.assertEqual([call.args[0][-1] for call in cleanup.call_args_list], ["-k", "-w"])
        for call in cleanup.call_args_list:
            self.assertEqual(call.kwargs["env"]["WINEPREFIX"], str(self.root / "wine-run" / "prefix"))
        proof = json.loads((self.root / "wine-run" / "wine-proof.json").read_text())
        self.assertTrue(proof["verified_environment"])
        self.assertTrue(proof["reaped"])
        self.assertTrue(proof["verified_windows_environment"])
        resume.assert_called_once_with(child.pid, HELPER.signal.SIGCONT)

    def _cleanup_failure_run(self, *, race=False, server_timeout=False, wait_status=0,
                             child_timeout=True, write_marker=True, bad_environment=False,
                             stopped=True, stop_timeout=False,
                             marker_payload=b"WINE_PRIVATE_ENV_VERIFIED\n"):
        args = self._wine_args()
        child = mock.Mock(pid=9022)
        child.wait.side_effect = ([HELPER.subprocess.TimeoutExpired("wine", 90), 0]
                                  if child_timeout else [0])
        child.poll.side_effect = [None, 0] if child_timeout else None
        child.poll.return_value = 0
        if stop_timeout:
            child.poll.side_effect = [None, None, 0]
        def spawned(command, **kwargs):
            process = self.proc_root / str(child.pid)
            process.mkdir()
            environment = kwargs["env"].copy()
            if bad_environment:
                environment["SDL_AUDIO_DRIVER"] = "pulseaudio"
            (process / "environ").write_bytes(b"\0".join(
                key.encode() + b"=" + value.encode() for key, value in environment.items()) + b"\0")
            (process / "exe").symlink_to(command[0])
            (process / "stat").write_text("9022 (stopped) " + " ".join(["T" if stopped else "S"] + ["0"] * 18 + ["3457"]))
            if not child_timeout and write_marker:
                (self.root / "wine-run" / "pe-environment-proof.txt").write_bytes(marker_payload)
            return child
        def cleaned(command, **kwargs):
            if command[-1] == "-k" and server_timeout:
                raise HELPER.subprocess.TimeoutExpired("wineserver -k", 10)
            return mock.Mock(returncode=wait_status if command[-1] == "-w" else 0)
        with self._patch_boundaries(), self._wine_pin(), \
                mock.patch.object(HELPER.subprocess, "Popen", side_effect=spawned), \
                mock.patch.object(HELPER.subprocess, "run", side_effect=cleaned) as cleanup, \
                mock.patch.object(HELPER.os, "kill"), \
                mock.patch.object(HELPER.time, "monotonic", side_effect=[0, 6] if stop_timeout else None), \
                mock.patch.object(HELPER.os, "killpg", side_effect=ProcessLookupError() if race else None):
            with self.assertRaises(SystemExit) as rejected:
                HELPER.main(args)
            self.assertEqual(rejected.exception.code, 2)
        self.assertEqual([call.args[0][-1] for call in cleanup.call_args_list], ["-k", "-w"])
        return json.loads((self.root / "wine-run" / "wine-proof.json").read_text())

    def test_wine_timeout_exit_race_still_reaps_and_cleans_prefix(self):
        proof = self._cleanup_failure_run(race=True)
        self.assertTrue(proof["reaped"])
        self.assertEqual(proof["server_cleanup"], {"-k": 0, "-w": 0})

    def test_wine_server_timeout_still_attempts_wait_and_records_failure(self):
        proof = self._cleanup_failure_run(server_timeout=True)
        self.assertTrue(proof["cleanup_errors"])
        self.assertEqual(proof["server_cleanup"]["-w"], 0)

    def test_wine_nonzero_server_wait_cannot_report_success(self):
        proof = self._cleanup_failure_run(wait_status=7, child_timeout=False)
        self.assertEqual(proof["returncode"], 0)
        self.assertTrue(proof["cleanup_errors"])
        self.assertEqual(proof["server_cleanup"]["-w"], 7)

    def test_wine_child_success_without_actual_windows_environment_proof_is_rejected(self):
        proof = self._cleanup_failure_run(child_timeout=False, write_marker=False)
        self.assertEqual(proof["returncode"], 0)
        self.assertIn("did not prove", proof["original_error"]["message"])

    def test_wine_stopped_actual_audio_environment_remains_fail_closed(self):
        proof = self._cleanup_failure_run(child_timeout=False, bad_environment=True)
        self.assertEqual(proof["environment_mismatches"], ["SDL_AUDIO_DRIVER"])
        self.assertFalse(proof["verified_environment"])

    def test_wine_must_reach_trusted_stop_before_environment_acceptance(self):
        proof = self._cleanup_failure_run(child_timeout=False, stopped=False)
        self.assertIn("pre-exec stop", proof["original_error"]["message"])
        self.assertFalse(proof["verified_environment"])

    def test_wine_trusted_stop_has_a_five_second_bound(self):
        proof = self._cleanup_failure_run(child_timeout=False, stopped=False, stop_timeout=True)
        self.assertIn("five seconds", proof["original_error"]["message"])
        self.assertTrue(proof["reaped"])

    def test_wine_wrong_windows_environment_marker_cannot_report_success(self):
        proof = self._cleanup_failure_run(child_timeout=False, marker_payload=b"wrong\n")
        self.assertIn("did not prove", proof["original_error"]["message"])


if __name__ == "__main__":
    unittest.main()
