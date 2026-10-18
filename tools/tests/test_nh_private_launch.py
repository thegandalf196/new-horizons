# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the fail-closed, private-display launch wrapper."""
import importlib.util
import hashlib
import json
import os
import re
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


class BootstrapClientArgumentsTest(unittest.TestCase):
    def test_absent_optin_preserves_ordinary_argument_policy(self):
        with mock.patch.dict(HELPER.os.environ, {}, clear=True):
            for arguments in ([], ["--disable-video", "--savefrequency", "0"],
                              ["--testmap", "Maps/map.vmap", "--spectate"]):
                HELPER.validate_client_args(arguments)
            with self.assertRaises(HELPER.Refusal):
                HELPER.validate_client_args(["--onlyAI"])

    def test_invalid_or_empty_present_optin_refuses(self):
        for value in ("", "1", "Ordinary44b24", "ordinary44b24 ", "Maps/other.vmap"):
            with self.subTest(value=value), mock.patch.dict(HELPER.os.environ,
                    {"NH_CURE_BOOTSTRAP": value, "NH_CURE_CALLBACK_CASE": "same-session"}, clear=True):
                with self.assertRaises(HELPER.Refusal):
                    HELPER.validate_client_args([])

    def test_exact_optin_requires_selected_callback_mode(self):
        for mode in (None, "", "same-session ", "unsupported"):
            environment = {"NH_CURE_BOOTSTRAP": "ordinary44b24"}
            if mode is not None:
                environment["NH_CURE_CALLBACK_CASE"] = mode
            with self.subTest(mode=mode), mock.patch.dict(HELPER.os.environ, environment, clear=True):
                with self.assertRaises(HELPER.Refusal):
                    HELPER.validate_client_args([])
        for mode in ("same-session", "late-session", "replaced-owner"):
            with mock.patch.dict(HELPER.os.environ,
                    {"NH_CURE_BOOTSTRAP": "ordinary44b24", "NH_CURE_CALLBACK_CASE": mode}, clear=True):
                HELPER.validate_client_args(["--disable-video", "--savefrequency", "0"])

    def test_conflicts_refuse_without_process_dispatch(self):
        environment = {"NH_CURE_BOOTSTRAP": "ordinary44b24", "NH_CURE_CALLBACK_CASE": "same-session"}
        with mock.patch.dict(HELPER.os.environ, environment, clear=True), \
                mock.patch.object(HELPER.subprocess, "Popen") as popen, \
                mock.patch.object(HELPER.subprocess, "run") as run:
            for arguments in (["--testmap", "Maps/map.vmap"], ["--testsave", "Saves/map.vcgm1"],
                              ["--onlyAI"], ["--headless"], ["--spectate"], ["--spectate=true"],
                              ["--testmap=Maps/map.vmap"]):
                with self.subTest(arguments=arguments), self.assertRaises(HELPER.Refusal):
                    HELPER.validate_client_args(arguments)
            popen.assert_not_called()
            run.assert_not_called()


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

    def test_native_vmap_spectate_preserves_exact_argv(self):
        client_args = ['--disable-video', '--savefrequency', '0', '--testmap',
                       'Maps/Controlled fixture/Animation witness.vmap', '--spectate']
        with self._patch_boundaries(), mock.patch.object(
                HELPER.subprocess, 'run', return_value=mock.Mock(returncode=0)) as run:
            self.assertEqual(HELPER.main(self._args('--verify-only', '--', *client_args)), 0)
        passed = run.call_args.args[0]
        self.assertEqual(passed[passed.index('--') + 1:], client_args)
        self.assertFalse(run.call_args.kwargs.get('shell', False))

    def test_native_testmap_bad_paths_refuse_before_spawn(self):
        paths = ['/Maps/map.vmap', 'C:/Maps/map.vmap', 'Maps/../map.vmap',
                 'Maps/./map.vmap', 'Maps//map.vmap', 'Maps\\map.vmap',
                 'Maps/map\0.vmap', 'Maps/map.zip', 'Maps/map.vmap.exe', '', 'Maps/']
        with self._patch_boundaries(), mock.patch.object(HELPER.subprocess, 'run') as run:
            for name in paths:
                with self.subTest(name=repr(name)), self.assertRaises(SystemExit) as caught:
                    HELPER.main(self._args('--verify-only', '--', '--testmap', name, '--spectate'))
                self.assertEqual(caught.exception.code, 2)
            run.assert_not_called()

    def test_native_duplicate_missing_or_unapproved_flags_refuse(self):
        invalid = [['--spectate', '--spectate'], ['--testmap'],
                   ['--testmap', '--spectate'],
                   ['--testmap', 'Maps/a.vmap', '--testmap', 'Maps/b.h3m'],
                   ['--spectate=true'], ['--onlyAI'], ['--nointro'],
                   ['--testmap', 'Maps/a.vmap', '--spectate', 'true']]
        with self._patch_boundaries(), mock.patch.object(HELPER.subprocess, 'run') as run:
            for arguments in invalid:
                with self.subTest(arguments=arguments), self.assertRaises(SystemExit) as caught:
                    HELPER.main(self._args('--verify-only', '--', *arguments))
                self.assertEqual(caught.exception.code, 2)
            run.assert_not_called()

    def test_native_h3m_and_vmap_extensions_share_existing_path_policy(self):
        for name in ('Maps/map.h3m', 'Maps/map.vmap', 'Maps/Nested/map.VMAP'):
            HELPER.validate_client_args(['--testmap', name, '--spectate'])

    def test_wine_still_refuses_native_vmap_spectate_arguments(self):
        # Client arguments are rejected before accessing any Wine paths/pins.
        with mock.patch.object(HELPER.subprocess, 'Popen') as popen:
            with self.assertRaises(HELPER.Refusal):
                HELPER.wine_fullgame_plan(SimpleNamespace(), ['--testmap', 'Maps/map.vmap', '--spectate'])
            with self.assertRaises(HELPER.Refusal):
                HELPER.wine_surface_plan(SimpleNamespace(), ['--spectate'])
            popen.assert_not_called()

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


    def test_fullgame_admission_is_wired_without_spawning_in_verify_only(self):
        argv=self._wine_args()
        pe=self.root/'new-horizons.exe'
        pe.write_bytes((self.root/'nhSdl3GrayscaleMaskRuntimeTest.exe').read_bytes())
        wine=self.root/'usr/wine';wine.parent.mkdir();wine.write_bytes(b'\x7fELFsynthetic');wine.chmod(0o700)
        server=wine.parent/'wineserver';server.write_bytes(b'\x7fELFserver');server.chmod(0o700)
        pins={}
        for name in ('VCMI_lib.dll','SDL3.dll','SDL3_image.dll','SDL3_mixer.dll','SDL3_ttf.dll'):
            path=self.root/name;path.write_bytes(name.encode());pins[name]=hashlib.sha256(path.read_bytes()).hexdigest()
        receipt=self.root/'full-dlls.json';receipt.write_text(json.dumps(pins))
        original = {}
        for group in ('data', 'music'):
            directory = self.root / ('content-' + group)
            directory.mkdir()
            file = directory / 'synthetic.bin';file.write_bytes(group.encode())
            original[group] = {'path':str(directory), 'files':{file.name:hashlib.sha256(file.read_bytes()).hexdigest()}}
        content_map = self.root/'fixture.vmap';content_map.write_bytes(b'synthetic map')
        content_settings = self.root/'fixture-settings.json';content_settings.write_text('{}')
        content_preset = self.root/'fixture-preset.json'
        content_preset.write_text(json.dumps({'activePreset':'default','presets':{'default':{
            'mods':['vcmi','core','new-horizons'],'settings':{}}}}))
        pin = lambda p: {'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
        content_manifest = self.root/'content-manifest.json'
        content_manifest.write_text(json.dumps({'version':1,'original':original,
            'map':{**pin(content_map),'filename':'fixture.vmap'},
            'settings':pin(content_settings),'modPreset':pin(content_preset)}))
        args=['--guard',str(self.guard),'--wine-fullgame',str(pe),
              '--wine-test-sha256',hashlib.sha256(pe.read_bytes()).hexdigest(),
              '--wine-binary',str(wine),'--wine-sha256',hashlib.sha256(wine.read_bytes()).hexdigest(),
              '--wine-server-sha256',hashlib.sha256(server.read_bytes()).hexdigest(),
              '--wine-input-sha256',hashlib.sha256(HELPER_PATH.with_name('nh-private-input.py').read_bytes()).hexdigest(),
              '--wine-dll-manifest',str(receipt),'--wine-dll-manifest-sha256',hashlib.sha256(receipt.read_bytes()).hexdigest(),
              '--wine-content-manifest',str(content_manifest),
              '--wine-content-manifest-sha256',hashlib.sha256(content_manifest.read_bytes()).hexdigest(),
              '--wine-run-root',str(self.root/'wine-run')]
        with self._patch_boundaries(),mock.patch.object(HELPER.subprocess,'Popen') as popen:
            self.assertEqual(HELPER.main([*args,'--verify-only']),0)
            popen.assert_not_called()
            self.assertEqual(list((self.root/'wine-run').iterdir()),[])
            parsed=HELPER._parser().parse_args(args)
            self.assertIsNone(HELPER.wine_fullgame_plan(parsed,[])['fixture_map'])
            fixture_args=[*args,'--wine-fixture-map','fixture.vmap']
            fixture=HELPER.wine_fullgame_plan(HELPER._parser().parse_args(fixture_args),[])
            self.assertEqual(fixture['fixture_map'],'fixture.vmap')
            for name in ('', 'other.vmap', '../fixture.vmap', 'Maps/fixture.vmap',
                         'CON.vmap', 'fixture.VMAP', 'a b.vmap', 'é.vmap', 'a'*65+'.vmap'):
                invalid=HELPER._parser().parse_args([*args,'--wine-fixture-map='+name])
                with self.assertRaises(HELPER.Refusal):HELPER.wine_fullgame_plan(invalid,[])
            with self.assertRaises(HELPER.Refusal):
                HELPER.wine_fullgame_plan(HELPER._parser().parse_args(fixture_args),['--spectate'])
            # Existing expanded cache route fits, but the typed map route does
            # not: enforce its own UTF-16 MAX_PATH bound before any staging.
            long_root=Path('/'+'r'*196)
            invalid=HELPER._parser().parse_args(fixture_args)
            invalid.wine_run_root=long_root
            with mock.patch.object(HELPER,'validate_wine_private_root'):
                with self.assertRaises(HELPER.Refusal):HELPER.wine_fullgame_plan(invalid,[])
            self.assertEqual(HELPER.wine_fullgame_plan(parsed,[])['render_timings'],{'enabled':False,'limit':None})
            metric=HELPER._parser().parse_args([*args,'--wine-render-load-timings'])
            self.assertEqual(HELPER.wine_fullgame_plan(metric,[])['render_timings'],{'enabled':True,'limit':1024})
            metric=HELPER._parser().parse_args([*args,'--wine-render-load-timings','--wine-render-load-timing-limit','16384'])
            self.assertEqual(HELPER.wine_fullgame_plan(metric,[])['render_timings'],{'enabled':True,'limit':16384})
            bootstrap=self.root/'nh-wine-bootstrap.exe'
            bootstrap.write_bytes(pe.read_bytes()+'NH_WINE_BOOTSTRAP_V1'.encode('utf-16-le'))
            bootstrap_pin=hashlib.sha256(bootstrap.read_bytes()).hexdigest()
            bootstrap_args=[*args,'--wine-bootstrap',str(bootstrap),'--wine-bootstrap-sha256',bootstrap_pin]
            admitted=HELPER.wine_fullgame_plan(HELPER._parser().parse_args(bootstrap_args),[])
            self.assertEqual(admitted['bootstrap'],str(bootstrap))
            self.assertEqual(admitted['bootstrap_sha256'],bootstrap_pin)
            with self.assertRaises(HELPER.Refusal):
                HELPER.wine_fullgame_plan(HELPER._parser().parse_args(
                    [*bootstrap_args,'--wine-fixture-map','fixture.vmap']),[])
            capable=self.root/'fixture-bootstrap.exe'
            capable.write_bytes(bootstrap.read_bytes()+'NH_WINE_BOOTSTRAP_FIXTURE_V1'.encode('utf-16-le'))
            capable_pin=hashlib.sha256(capable.read_bytes()).hexdigest()
            typed=HELPER.wine_fullgame_plan(HELPER._parser().parse_args([*fixture_args,
                '--wine-bootstrap',str(capable),'--wine-bootstrap-sha256',capable_pin]),[])
            self.assertEqual(typed['bootstrap_sha256'],capable_pin)
            self.assertEqual(typed['fixture_map'],'fixture.vmap')
            for path,pin in ((bootstrap,'0'*64),(None,bootstrap_pin),(bootstrap,None),
                             (self.root/'missing.exe',bootstrap_pin),(Path('relative.exe'),bootstrap_pin)):
                invalid=HELPER._parser().parse_args(args)
                invalid.wine_bootstrap,invalid.wine_bootstrap_sha256=path,pin
                with self.assertRaises(HELPER.Refusal):HELPER.wine_fullgame_plan(invalid,[])
            alias=self.root/'bootstrap-alias.exe';alias.symlink_to(bootstrap)
            invalid=HELPER._parser().parse_args(bootstrap_args)
            invalid.wine_bootstrap=alias
            with self.assertRaises(HELPER.Refusal):HELPER.wine_fullgame_plan(invalid,[])
            popen.assert_not_called()
            with self.assertRaises(HELPER.Refusal):HELPER.wine_fullgame_plan(parsed,['--disable-video'])
            parsed.wine_input_sha256='0'*64
            with self.assertRaises(HELPER.Refusal):HELPER.wine_fullgame_plan(parsed,[])
            popen.assert_not_called()
            parsed.wine_input_sha256 = hashlib.sha256(HELPER_PATH.with_name('nh-private-input.py').read_bytes()).hexdigest()
            parsed.wine_content_manifest_sha256 = '0'*64
            with self.assertRaises(HELPER.Refusal):HELPER.wine_fullgame_plan(parsed,[])
            parsed.wine_content_manifest = None
            with self.assertRaises(HELPER.Refusal):HELPER.wine_fullgame_plan(parsed,[])
            popen.assert_not_called()

    def test_fixture_option_refused_outside_fullgame_without_spawn(self):
        surface_args=self._wine_args()
        for name in ('fixture.vmap',''):
            for args in (self._args('--wine-fixture-map='+name),
                         [*surface_args,'--wine-fixture-map='+name]):
                with self._patch_boundaries(), self._wine_pin(), mock.patch.object(HELPER.subprocess,'Popen') as popen:
                    with self.assertRaises(SystemExit) as exit:HELPER.main(args)
                    self.assertEqual(exit.exception.code,2)
                    popen.assert_not_called()

    def test_fixture_option_duplicate_refuses_before_spawn(self):
        for options in (
                ['--wine-fixture-map','fixture.vmap','--wine-fixture-map','fixture.vmap'],
                ['--wine-fixture-map=','--wine-fixture-map='],
                ['--wine-fixture-map=fixture.vmap','--wine-fixture-map','other.vmap'],
                ['--wine-fixture-map','','--wine-fixture-map=fixture.vmap']):
            with mock.patch.object(HELPER.subprocess,'Popen') as popen:
                with self.assertRaises(SystemExit) as exit:
                    HELPER.main(['--guard',str(self.guard),*options])
                self.assertEqual(exit.exception.code,2)
                popen.assert_not_called()

    def test_content_options_refused_outside_fullgame_without_spawn(self):
        for args in (self._args('--wine-content-manifest',str(self.root/'unused.json')),
                     [*self._wine_args(),'--wine-content-manifest-sha256','0'*64]):
            with self._patch_boundaries(), self._wine_pin(), mock.patch.object(HELPER.subprocess,'Popen') as popen:
                with self.assertRaises(SystemExit) as exit:
                    HELPER.main(args)
                self.assertEqual(exit.exception.code,2)
                popen.assert_not_called()

    def test_bootstrap_options_refused_outside_fullgame_without_spawn(self):
        for args in (self._args('--wine-bootstrap',str(self.root/'unused.exe')),
                     [*self._wine_args(),'--wine-bootstrap-sha256','0'*64]):
            with self._patch_boundaries(), self._wine_pin(), mock.patch.object(HELPER.subprocess,'Popen') as popen:
                with self.assertRaises(SystemExit) as exit:
                    HELPER.main(args)
                self.assertEqual(exit.exception.code,2)
                popen.assert_not_called()

    def test_render_timing_options_refused_outside_fullgame_without_spawn(self):
        surface_args = self._wine_args()
        for args in (self._args('--wine-render-load-timings'),
                     self._args('--wine-render-load-timing-limit','1024'),
                     [*surface_args,'--wine-render-load-timings'],
                     [*surface_args,'--wine-render-load-timing-limit','1024']):
            with self._patch_boundaries(), self._wine_pin(), mock.patch.object(HELPER.subprocess,'Popen') as popen:
                with self.assertRaises(SystemExit) as exit:HELPER.main(args)
                self.assertEqual(exit.exception.code,2)
                popen.assert_not_called()


class WineFullgameAdmissionTest(unittest.TestCase):
    def test_render_timing_admission_defaults_and_canonical_bounds(self):
        parse=lambda *args:HELPER._parser().parse_args(['--guard','synthetic-guard',*args])
        self.assertEqual(HELPER.render_timing_contract(parse()),{'enabled':False,'limit':None})
        self.assertEqual(HELPER.render_timing_contract(parse('--wine-render-load-timings')),{'enabled':True,'limit':1024})
        for value in ('1','1024','16384'):
            self.assertEqual(HELPER.render_timing_contract(parse('--wine-render-load-timings','--wine-render-load-timing-limit',value)),
                             {'enabled':True,'limit':int(value)})

    def test_render_timing_bad_limit_or_unrequested_limit_refuses_without_spawn(self):
        with mock.patch.object(HELPER.subprocess,'Popen') as popen:
            for value in ('0','16385','-1','+1','01','',' 2','2.0','\u0661'):
                args=HELPER._parser().parse_args(['--guard','synthetic-guard','--wine-render-load-timings',
                    '--wine-render-load-timing-limit='+value])
                with self.assertRaises(HELPER.Refusal):HELPER.render_timing_contract(args)
            args=HELPER._parser().parse_args(['--guard','synthetic-guard','--wine-render-load-timing-limit','1024'])
            with self.assertRaises(HELPER.Refusal):HELPER.render_timing_contract(args)
            popen.assert_not_called()

    def test_owner_render_timing_factory_emits_only_explicit_two_keys(self):
        path=HELPER_PATH.with_name('nh_wine_fullgame_owner.py')
        spec=importlib.util.spec_from_file_location('render_timing_owner_test',path)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        self.assertEqual(module.render_timing_environment({}),{})
        self.assertEqual(module.render_timing_environment({'render_timings':{'enabled':False,'limit':None}}),{})
        self.assertEqual(module.render_timing_environment({'render_timings':{'enabled':True,'limit':16384}}),
                         {'NH_RENDER_LOAD_TIMINGS':'1','NH_RENDER_LOAD_TIMING_LIMIT':'16384'})
        for contract in ({'enabled':1,'limit':1024},{'enabled':True,'limit':True},{'enabled':True,'limit':0},
                         {'enabled':True,'limit':16385},{'enabled':False,'limit':1024},
                         {'enabled':True,'limit':1024,'arbitrary':'environment'}):
            with self.assertRaises(module.Refusal):module.render_timing_environment({'render_timings':contract})

    def test_actual_render_timing_environment_missing_wrong_or_disabled_leak_refuses(self):
        enabled={'enabled':True,'limit':1024}
        actual={'NH_RENDER_LOAD_TIMINGS':'1','NH_RENDER_LOAD_TIMING_LIMIT':'1024'}
        HELPER.validate_render_timing_environment(actual,enabled)
        for changed in ({},{'NH_RENDER_LOAD_TIMINGS':'1'},{'NH_RENDER_LOAD_TIMING_LIMIT':'1024'},
                        {**actual,'NH_RENDER_LOAD_TIMINGS':'0'},{**actual,'NH_RENDER_LOAD_TIMING_LIMIT':'01024'},
                        {**actual,'NH_RENDER_LOAD_TIMING_LIMIT':'16384'}):
            with self.assertRaises(HELPER.Refusal):HELPER.validate_render_timing_environment(changed,enabled)
        disabled={'enabled':False,'limit':None}
        HELPER.validate_render_timing_environment({},disabled)
        for leaked in (actual,{'NH_RENDER_LOAD_TIMINGS':''},{'NH_RENDER_LOAD_TIMING_LIMIT':''}):
            with self.assertRaises(HELPER.Refusal):HELPER.validate_render_timing_environment(leaked,disabled)

    def witness(self, *, nonce="a"*64, local=r"Z:\private\localappdata", pid="24"):
        exe=r"Z:\package\new-horizons.exe"
        facade=r"Z:\package\VCMI_lib.dll"
        suffixes=(r"\HeroesIII-NewHorizons\content", r"\HeroesIII-NewHorizons\config",
                  r"\HeroesIII-NewHorizons\cache", r"\HeroesIII-NewHorizons\logs",r"\new-horizons\Saves")
        lines=["NH_WINE_PRE_SDL_V1",nonce,pid,exe,facade,local,"dummy","dummy","windows","windows",
               *[local+s for s in suffixes],""]
        return "\n".join(lines).encode("utf-16-le"),dict(nonce=nonce,executable=exe,facade=facade,localappdata=local)

    def test_exact_witness_new_save_route_has_no_input_authority(self):
        payload,args=self.witness()
        proof=HELPER.parse_wine_fullgame_witness(payload,**args)
        self.assertEqual(proof['windows_pid'],24)
        self.assertFalse(proof['input_authorized'])

    def test_wrong_audio_nonce_encoding_and_pid_are_refused(self):
        payload,args=self.witness()
        for bad in (payload.replace('dummy'.encode('utf-16-le'),'pulse'.encode('utf-16-le')),
                    payload[:-1],payload+b'xx',payload.replace(('a'*64).encode('utf-16-le'),('b'*64).encode('utf-16-le'))):
            with self.assertRaises(HELPER.Refusal):HELPER.parse_wine_fullgame_witness(bad,**args)
        bad,args=self.witness(pid=str(2**32))
        with self.assertRaises(HELPER.Refusal):HELPER.parse_wine_fullgame_witness(bad,**args)

    def test_long_path_is_rejected_before_any_profile_write(self):
        payload,args=self.witness(local='Z:\\'+'x'*250+r'\localappdata')
        with mock.patch.object(HELPER.Path,'mkdir') as mkdir:
            with self.assertRaises(HELPER.Refusal):HELPER.parse_wine_fullgame_witness(payload,**args)
            mkdir.assert_not_called()

    def test_identity_cannot_match_missing_or_foreign_values(self):
        valid=dict(pid=2,start_ticks=3,uid=os.getuid(),boot_id='d916fa5f-9d12-4f87-a813-9581d728ef55')
        HELPER.validate_wine_identity(valid,owner_uid=os.getuid())
        for key,bad in [('pid',None),('start_ticks',False),('uid',os.getuid()+1),('boot_id','')]:
            with self.assertRaises(HELPER.Refusal):HELPER.validate_wine_identity({**valid,key:bad},owner_uid=os.getuid())

    def test_maps_require_exact_six_selected_files_and_reject_alternative(self):
        names=['new-horizons.exe','VCMI_lib.dll','SDL3.dll','SDL3_image.dll','SDL3_mixer.dll','SDL3_ttf.dll']
        expected=[dict(path='/selected/'+name,device='00:01',inode=i+1,sha256='a'*64) for i,name in enumerate(names)]
        HELPER.validate_wine_mapping_rows(expected,expected)
        with self.assertRaises(HELPER.Refusal):HELPER.validate_wine_mapping_rows(expected[:-1],expected)
        with self.assertRaises(HELPER.Refusal):HELPER.validate_wine_mapping_rows([*expected,{**expected[1],'path':'/other/VCMI_lib.dll'}],expected)
        with self.assertRaises(HELPER.Refusal):HELPER.validate_wine_mapping_rows([*expected,{**expected[2],'path':'/other/sdl3.dll'}],expected)

    def test_bootstrap_transition_accepts_only_two_exact_argv_spellings(self):
        identity=dict(pid=2,start_ticks=3,uid=os.getuid(),boot_id='d916fa5f-9d12-4f87-a813-9581d728ef55')
        stopped={**identity,'state':'T'}
        for tail in ([], ['--fixture-map','playback-cabir.vmap']):
            loader=dict(exe='/pinned/wine',sha256='b'*64,
                        argv=['/pinned/wine','/pinned/bootstrap.exe',*tail])
            for argv in (loader['argv'],loader['argv'][1:]):
                actual={**identity,**loader,'state':'S','argv':list(argv)}
                original=json.dumps([stopped,actual,loader],sort_keys=True)
                HELPER.validate_wine_exec_transition(stopped,actual,loader,owner_uid=os.getuid(),
                                                    expected_bootstrap='/pinned/bootstrap.exe')
                self.assertEqual(json.dumps([stopped,actual,loader],sort_keys=True),original)
                if argv != loader['argv']:
                    with self.assertRaises(HELPER.Refusal):
                        HELPER.validate_wine_exec_transition(stopped,actual,loader,owner_uid=os.getuid())

    def test_bootstrap_transition_rejects_unpinned_or_changed_commands(self):
        identity=dict(pid=2,start_ticks=3,uid=os.getuid(),boot_id='d916fa5f-9d12-4f87-a813-9581d728ef55')
        stopped={**identity,'state':'T'}
        loader=dict(exe='/pinned/wine',sha256='b'*64,
                    argv=['/pinned/wine','/pinned/bootstrap.exe','--fixture-map','playback-cabir.vmap'])
        actual={**identity,**loader,'state':'S'}
        for argv in (loader['argv'][2:],loader['argv'][1:-1],loader['argv'][1:]+['extra'],
                     ['/pinned/bootstrap.exe','playback-cabir.vmap','--fixture-map'],
                     ['/pinned/bootstrap.exe','--fixture-map','other.vmap']):
            with self.assertRaises(HELPER.Refusal):
                HELPER.validate_wine_exec_transition(stopped,{**actual,'argv':argv},loader,
                    owner_uid=os.getuid(),expected_bootstrap='/pinned/bootstrap.exe')
        for bootstrap in ('/foreign/bootstrap.exe','relative.exe'):
            with self.assertRaises(HELPER.Refusal):
                HELPER.validate_wine_exec_transition(stopped,actual,loader,owner_uid=os.getuid(),
                                                    expected_bootstrap=bootstrap)
        foreign_loader={**loader,'argv':['/other/wine',*loader['argv'][1:]]}
        with self.assertRaises(HELPER.Refusal):
            HELPER.validate_wine_exec_transition(stopped,{**actual,'argv':foreign_loader['argv']},foreign_loader,
                owner_uid=os.getuid(),expected_bootstrap='/pinned/bootstrap.exe')
        for key,value in (('exe','/foreign/wine'),('sha256','c'*64),('pid',99),('start_ticks',99),
                          ('uid',os.getuid()+1),('boot_id','a916fa5f-9d12-4f87-a813-9581d728ef55'),('state','T')):
            with self.assertRaises(HELPER.Refusal):
                HELPER.validate_wine_exec_transition(stopped,{**actual,key:value},loader,
                    owner_uid=os.getuid(),expected_bootstrap='/pinned/bootstrap.exe')

    def test_whole_runtime_context_is_plan_bound_and_rechecked(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'run';root.mkdir(mode=0o700)
            for name in ('home','prefix','data','config','cache','state','runtime','localappdata'):
                (root/name).mkdir(mode=0o700)
            exe=str(Path(temp)/'new-horizons.exe')
            dlls={name:'a'*64 for name in ('VCMI_lib.dll','SDL3.dll','SDL3_image.dll','SDL3_mixer.dll','SDL3_ttf.dll')}
            identity=dict(pid=2,start_ticks=3,uid=os.getuid(),boot_id='d916fa5f-9d12-4f87-a813-9581d728ef55')
            stopped={**identity,'state':'T'}
            loader=dict(exe='/usr/wine',sha256='b'*64,argv=['/usr/wine',exe])
            current={**identity,**loader,'state':'S'}
            plan=dict(root=str(root),executable=exe,dlls=dlls,executable_sha256='a'*64,
                      launch_nonce='a'*64,stopped_identity=stopped,loader_identity=loader)
            required={key:str(root/name) for key,name in (('HOME','home'),('WINEHOME','home'),('WINEPREFIX','prefix'),
                     ('XDG_DATA_HOME','data'),('XDG_CONFIG_HOME','config'),('XDG_CACHE_HOME','cache'),
                     ('XDG_STATE_HOME','state'),('XDG_RUNTIME_DIR','runtime'))}
            local='Z:'+str(root/'localappdata').replace('/','\\');required['LOCALAPPDATA']=local
            wexe='Z:'+exe.replace('/','\\');wfacade='Z:'+str(Path(exe).parent/'VCMI_lib.dll').replace('/','\\')
            suffixes=(r'\HeroesIII-NewHorizons\content',r'\HeroesIII-NewHorizons\config',r'\HeroesIII-NewHorizons\cache',r'\HeroesIII-NewHorizons\logs',r'\new-horizons\Saves')
            witness='\n'.join(['NH_WINE_PRE_SDL_V1','a'*64,'24',wexe,wfacade,local,'dummy','dummy','windows','windows',*[local+s for s in suffixes],'']).encode('utf-16-le')
            paths=[exe,*[str(Path(exe).parent/name) for name in dlls]]
            maps=[dict(path=path,sha256='a'*64,inode=i+1,device='00:01') for i,path in enumerate(paths)]
            context=dict(before=current,after=current,stopped=stopped,loader=loader,nonce='a'*64,
                environment={**required,'DISPLAY':':191','SDL_VIDEODRIVER':'windows','SDL_VIDEO_DRIVER':'windows','SDL_AUDIODRIVER':'dummy','SDL_AUDIO_DRIVER':'dummy'},
                required_environment=required,mappings=maps,expected_mappings=maps,witness=witness,
                win32_executable=wexe,win32_facade=wfacade,win32_localappdata=local,
                supervisor_ready_before_resume=True,owned_service_graph_verified=True,age_after_io=1)
            self.assertFalse(HELPER.verify_wine_runtime(context,plan=plan)['input_authorized'])
            timed_plan={**plan,'render_timings':{'enabled':True,'limit':1024}}
            with self.assertRaises(HELPER.Refusal):HELPER.verify_wine_runtime(context,plan=timed_plan)
            timed_context={**context,'environment':{**context['environment'],
                'NH_RENDER_LOAD_TIMINGS':'1','NH_RENDER_LOAD_TIMING_LIMIT':'1024'}}
            self.assertFalse(HELPER.verify_wine_runtime(timed_context,plan=timed_plan)['input_authorized'])
            with self.assertRaises(HELPER.Refusal):HELPER.verify_wine_runtime(timed_context,plan=plan)
            with self.assertRaises(HELPER.Refusal):
                HELPER.verify_wine_runtime({**timed_context,'environment':{**timed_context['environment'],
                    'NH_RENDER_LOAD_TIMING_LIMIT':'16384'}},plan=timed_plan)
            bootstrap_path=str(Path(temp)/'nh-wine-bootstrap.exe')
            bootstrap_map=dict(path=bootstrap_path,sha256='c'*64,inode=20,device='00:01')
            bootstrap_loader={**loader,'argv':['/usr/wine',bootstrap_path]}
            bootstrap_current={**current,**bootstrap_loader}
            game_current={**current,'pid':3,'start_ticks':4,'argv':['selected-game-child']}
            bootstrap_plan={**plan,'wine':'/usr/wine','wine_sha256':'b'*64,
                'bootstrap':bootstrap_path,'bootstrap_sha256':'c'*64,'bootstrap_mapping':bootstrap_map,
                'loader_identity':bootstrap_loader,'game_identity':game_current}
            bootstrap_context={**context,'before':game_current,'after':game_current,'loader':bootstrap_loader,
                'game_identity':game_current,'bootstrap':{'before':bootstrap_current,'after':bootstrap_current,
                    'mappings':[bootstrap_map]}}
            self.assertFalse(HELPER.verify_wine_runtime(bootstrap_context,plan=bootstrap_plan)['input_authorized'])
            stripped={**bootstrap_current,'argv':bootstrap_loader['argv'][1:]}
            stripped_context={**bootstrap_context,'bootstrap':{'before':stripped,'after':stripped,
                'mappings':[bootstrap_map]}}
            self.assertFalse(HELPER.verify_wine_runtime(stripped_context,plan=bootstrap_plan)['input_authorized'])
            loader_environment={**bootstrap_context['environment'],'WINELOADER':'/usr/wine'}
            matched_context={**bootstrap_context,'environment':loader_environment}
            self.assertFalse(HELPER.verify_wine_runtime(matched_context,plan=bootstrap_plan)['input_authorized'])
            with self.assertRaises(HELPER.Refusal):
                HELPER.verify_wine_runtime({**context,'environment':loader_environment},plan=plan)
            for change in ({'environment':{**loader_environment,'WINELOADER':'/foreign/wine'}},
                           {'environment':{**loader_environment,'WINESERVER':'/usr/wineserver'}},
                           {'after':{**game_current,'exe':'/foreign/wine'}},
                           {'after':{**game_current,'sha256':'d'*64}}):
                with self.assertRaises(HELPER.Refusal):
                    HELPER.verify_wine_runtime({**matched_context,**change},plan=bootstrap_plan)
            for change in ({'wine':'/foreign/wine'},{'wine_sha256':'d'*64}):
                with self.assertRaises(HELPER.Refusal):
                    HELPER.verify_wine_runtime(matched_context,plan={**bootstrap_plan,**change})
            for snapshot in ('before','after'):
                for key,value in (('argv',['unapproved']),('exe','/foreign/wine'),('sha256','d'*64),
                                  ('pid',99),('state','T')):
                    with self.assertRaises(HELPER.Refusal):
                        HELPER.verify_wine_runtime({**bootstrap_context,'bootstrap':{
                            **bootstrap_context['bootstrap'],snapshot:{**bootstrap_current,key:value}}},
                            plan=bootstrap_plan)
            for change in ({'game_identity':{**game_current,'pid':99}}, {'after':current},
                           {'bootstrap':{'before':bootstrap_current,'after':bootstrap_current,'mappings':[]}},
                           {'mappings':maps[:-1]}, {'witness':b''}):
                with self.assertRaises(HELPER.Refusal):
                    HELPER.verify_wine_runtime({**bootstrap_context,**change},plan=bootstrap_plan)
            for change in ({'nonce':'c'*64},{'loader':{**loader,'sha256':'c'*64}},
                           {'required_environment':{**required,'HOME':'/foreign'}},{'age_after_io':600}):
                with self.assertRaises(HELPER.Refusal):HELPER.verify_wine_runtime({**context,**change},plan=plan)
            root.chmod(0o755)
            with self.assertRaises(HELPER.Refusal):HELPER.verify_wine_runtime(context,plan=plan)

    def test_actual_environment_requires_silent_windows_and_private_routes(self):
        required={k:'/private/'+k for k in ('HOME','WINEHOME','WINEPREFIX','LOCALAPPDATA','XDG_DATA_HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_STATE_HOME','XDG_RUNTIME_DIR')}
        actual={**required,'DISPLAY':':191','SDL_VIDEODRIVER':'windows','SDL_VIDEO_DRIVER':'windows','SDL_AUDIODRIVER':'dummy','SDL_AUDIO_DRIVER':'dummy'}
        HELPER.validate_wine_environment(actual,required)
        for key,value in [('SDL_AUDIO_DRIVER','pulse'),('WAYLAND_DISPLAY','wayland-0'),('LOCALAPPDATA','/host')]:
            with self.assertRaises(HELPER.Refusal):HELPER.validate_wine_environment({**actual,key:value},required)

    def test_host_route_refusals_report_only_fixed_ordered_key_names(self):
        required={k:'/private/'+k for k in ('HOME','WINEHOME','WINEPREFIX','LOCALAPPDATA',
            'XDG_DATA_HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_STATE_HOME','XDG_RUNTIME_DIR')}
        actual={**required,'DISPLAY':':191','SDL_VIDEODRIVER':'windows','SDL_VIDEO_DRIVER':'windows',
            'SDL_AUDIODRIVER':'dummy','SDL_AUDIO_DRIVER':'dummy'}
        keys=('WAYLAND_DISPLAY','LD_PRELOAD','LD_AUDIT','LD_LIBRARY_PATH',
              'PULSE_SERVER','PIPEWIRE_REMOTE','WINELOADER','WINESERVER')
        for key in keys:
            with self.subTest(key=key), self.assertRaises(HELPER.Refusal) as caught:
                HELPER.validate_wine_environment({**actual,key:'secret-value'},required)
            self.assertEqual(str(caught.exception),'Wine host/injection route refused: keys='+key)
            HELPER.validate_wine_environment({**actual,key:''},required)
        with self.assertRaises(HELPER.Refusal) as caught:
            HELPER.validate_wine_environment({**actual,**dict.fromkeys(reversed(keys),'secret-value')},required)
        self.assertEqual(str(caught.exception),'Wine host/injection route refused: keys='+','.join(keys))
        self.assertNotIn('secret-value',str(caught.exception))

    def test_bootstrap_loader_environment_accepts_only_exact_canonical_pin(self):
        required={k:'/private/'+k for k in ('HOME','WINEHOME','WINEPREFIX','LOCALAPPDATA',
            'XDG_DATA_HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_STATE_HOME','XDG_RUNTIME_DIR')}
        actual={**required,'DISPLAY':':191','SDL_VIDEODRIVER':'windows','SDL_VIDEO_DRIVER':'windows',
            'SDL_AUDIODRIVER':'dummy','SDL_AUDIO_DRIVER':'dummy','WINELOADER':'/pinned/wine'}
        HELPER.validate_wine_environment(actual,required,expected_loader='/pinned/wine')
        with self.assertRaises(HELPER.Refusal):
            HELPER.validate_wine_environment(actual,required)
        for loader in ('/foreign/wine','/pinned//wine','/pinned/./wine','/pinned/wine/','/pinned/wine extra'):
            with self.assertRaises(HELPER.Refusal):
                HELPER.validate_wine_environment({**actual,'WINELOADER':loader},required,expected_loader='/pinned/wine')
        for pin in ('','wine','/pinned/../wine','/pinned//wine','/pinned/wine\0',False):
            with self.assertRaises(HELPER.Refusal):
                HELPER.validate_wine_environment(actual,required,expected_loader=pin)
        for key in ('WAYLAND_DISPLAY','LD_PRELOAD','LD_AUDIT','LD_LIBRARY_PATH',
                    'PULSE_SERVER','PIPEWIRE_REMOTE','WINESERVER'):
            with self.assertRaises(HELPER.Refusal):
                HELPER.validate_wine_environment({**actual,key:'secret-route'},required,expected_loader='/pinned/wine')

    def test_fresh_root_no_symlink_no_reuse(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'run';root.mkdir(mode=0o700)
            HELPER.validate_wine_private_root(root,uid=os.getuid())
            (root/'old').write_text('old')
            with self.assertRaises(HELPER.Refusal):HELPER.validate_wine_private_root(root,uid=os.getuid())
            alias=Path(temp)/'alias';alias.symlink_to(root)
            with self.assertRaises(HELPER.Refusal):HELPER.validate_wine_private_root(alias,uid=os.getuid())


class PrivateWineWitnessDiagnosticContractTest(unittest.TestCase):
    """Source contracts only; these do not execute Win32 APIs or launch a PE."""

    def setUp(self):
        self.source = (Path(__file__).resolve().parents[2] /
                       'clientapp/PrivateWineStartupWitness.h').read_text()

    def test_no_optin_returns_before_diagnostics_or_filesystem(self):
        body = self.source.split('inline int verifyBeforeSDL()', 1)[1]
        early = body.split('auto stage = RefusalStage::nonce;', 1)[0]
        self.assertIn('enabled == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND', early)
        self.assertIn('return 0;', early)
        for forbidden in ('refusal(', 'fprintf', 'CreateFile', 'VCMIDirs::get', 'Sleep('):
            self.assertNotIn(forbidden, early)

    def test_shared_root_policy_precedes_stop_before_drive_relative_parent(self):
        self.assertIn('#include "PrivateWinePathPolicy.h"', self.source)
        body = self.source.split('inline bool plainAncestors(', 1)[1].split('inline bool boundedPath(', 1)[0]
        self.assertIn('privateWinePathPolicy::isDirectoryAllowed(current.wstring(), attributes)', body)
        self.assertIn('privateWinePathPolicy::isCanonicalRoot(current.wstring()) || current == current.parent_path()', body)
        self.assertLess(body.index('isDirectoryAllowed('), body.index('isCanonicalRoot('))
        self.assertIn('return false;', body)
        bootstrap = (Path(__file__).resolve().parents[2] / 'tools/tests/nh-wine-bootstrap.cpp').read_text()
        self.assertIn('#include "../../clientapp/PrivateWinePathPolicy.h"', bootstrap)
        ancestors = bootstrap.split('bool plainDirectoryAncestors(', 1)[1].split('bool plainFile(', 1)[0]
        self.assertIn('privateWinePathPolicy::isDirectoryAllowed(path, attrs)', ancestors)
        self.assertLess(ancestors.index('isDirectoryAllowed('), ancestors.index('if(path.size() == 3) return true;'))

    def test_fixed_stage_codes_cover_every_phase_and_all_refusals(self):
        stages = ['nonce', 'local', 'audioVideo', 'executable', 'facade', 'gate',
                  'configRead', 'configShape', 'expansion', 'resolvedDirs',
                  'proofCreate', 'proofWrite', 'ack']
        enum = self.source.split('enum class RefusalStage', 1)[1].split('};', 1)[0]
        self.assertEqual(re.findall(r'(\w+)\s*=\s*(\d+)', enum),
                         [(name, str(index)) for index, name in enumerate(stages, 1)])
        body = self.source.split('inline int verifyBeforeSDL()', 1)[1]
        self.assertEqual(re.findall(r'stage = RefusalStage::(\w+)', body), stages)
        self.assertNotIn('return 86;', body)
        self.assertIn('return accepted ? 0 : refusal(stage);', body)
        self.assertTrue(body.rstrip().endswith('return refusal(stage);\n}\n}\n#endif'))

    def test_stderr_contains_only_fixed_stage_code_not_private_values(self):
        diagnostic = self.source.split('inline int refusal(RefusalStage stage)', 1)[1].split('\n}', 1)[0]
        self.assertIn('"NH_WINE_PRE_SDL_REFUSAL stage=%u\\n", static_cast<unsigned int>(stage)', diagnostic)
        self.assertEqual(self.source.count('std::fprintf('), 2)
        self.assertIn('std::fflush(stderr);', diagnostic)
        self.assertIn('return 86;', diagnostic)
        for forbidden in ('nonce', 'environment(', 'GetLastError', '.what()', 'wstring', '%s', '%ls'):
            self.assertNotIn(forbidden, diagnostic)

    def test_local_detail_codes_are_fixed_and_do_not_print_environment_values(self):
        details = ['readActual', 'readExpected', 'actualMissing', 'actualOversized',
                   'actualNewline', 'expectedMissing', 'expectedOversized',
                   'expectedNewline', 'unequal', 'unbounded', 'tooShort', 'prefix',
                   'dots', 'trailingSeparator', 'suffix']
        enum = self.source.split('enum class LocalRefusalDetail', 1)[1].split('};', 1)[0]
        self.assertEqual(re.findall(r'(\w+)\s*=\s*(\d+)', enum),
                         [(name, str(index)) for index, name in enumerate(details, 1)])
        diagnostic = self.source.split('inline int localRefusal(LocalRefusalDetail detail)', 1)[1].split('\n}', 1)[0]
        self.assertIn('"NH_WINE_PRE_SDL_LOCAL_DETAIL detail=%u\\n", static_cast<unsigned int>(detail)', diagnostic)
        self.assertIn('return refusal(RefusalStage::local);', diagnostic)
        for forbidden in ('nonce', 'environment(', 'GetLastError', '.what()', 'wstring', '%s', '%ls'):
            self.assertNotIn(forbidden, diagnostic)

    def test_local_reads_and_predicate_order_preserve_fail_closed_contract(self):
        read = self.source.split('inline std::wstring localEnvironment(', 1)[1].split('\n}', 1)[0]
        for required in ('wchar_t buffer[2048];', 'GetEnvironmentVariableW(name, buffer, 2048)',
                         'length == 0', 'length >= 2048', 'value.find_first_of(L"\\r\\n")',
                         'LocalRefusalDetail::readExpected : LocalRefusalDetail::readActual',
                         'LocalRefusalDetail::expectedMissing : LocalRefusalDetail::actualMissing',
                         'LocalRefusalDetail::expectedOversized : LocalRefusalDetail::actualOversized',
                         'LocalRefusalDetail::expectedNewline : LocalRefusalDetail::actualNewline'):
            self.assertIn(required, read)
        body = self.source.split('stage = RefusalStage::local;', 1)[1].split('stage = RefusalStage::audioVideo;', 1)[0]
        clauses = ['localEnvironment(L"LOCALAPPDATA", false, localDetail)',
                   'localEnvironment(L"NH_PRIVATE_WINE_LOCALAPPDATA", true, localDetail)',
                   'if(!boundedPath(local))', 'if(local != expected)', 'if(local.size() < 20)',
                   'if(local.substr(0, 3) != L"Z:\\\\")',
                   'if(local.find(L"..") != std::wstring::npos)',
                   "if(local.back() == L'\\\\')",
                   'if(local.substr(local.size() - 13) != L"\\\\localappdata")']
        positions = [body.index(clause) for clause in clauses]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(re.findall(r'return localRefusal\(LocalRefusalDetail::(\w+)\)', body),
                         ['unbounded', 'unequal', 'tooShort', 'prefix', 'dots', 'trailingSeparator', 'suffix'])
        self.assertIn('if(stage == RefusalStage::local)\n\t\treturn localRefusal(localDetail);', self.source)
        self.assertNotIn('SetEnvironmentVariable', self.source)


if __name__ == "__main__":
    unittest.main()
