#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Launch New Horizons only through the managed profile on the guarded :191 Xvfb.

This is the GUI launch entry point for a future authorized tester. Always pass
an explicit owned-Xvfb guard, frozen snapshot, private profile, and purchaser
asset directory. It invokes tools/new-horizons-launch.sh with argv (never a
shell command), pins the child display/video/audio drivers, and removes the
host Wayland display. Future GUI sessions must enter through this helper; do
not start a raw client or manually bypass the managed launcher. This helper
delegates the game launch to tools/new-horizons-launch.sh. --verify-only/--dry-run
asks the managed launcher to validate paths without executing the game. Bound
real runs in the caller.

Example (the profile is a dedicated writable path):
  python3 tools/tests/nh-private-launch.py --guard /tmp/private/display-guard.json \
    --snapshot /tmp/snapshot --profile '/tmp/PRIVATE profile' --assets /mnt/complete

Only these optional client arguments are accepted after --:
  --disable-video
  --savefrequency 0
  --testmap Maps/<relative-map>.h3m
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import signal
import struct
import subprocess
import sys
import time


PRIVATE_DISPLAY = ":191"
PROC_ROOT = Path("/proc")
BOOT_ID_PATH = Path("/proc/sys/kernel/random/boot_id")
DISPLAY_SOCKET = Path("/tmp/.X11-unix/X191")
LAUNCHER = Path(__file__).resolve().parents[1] / "new-horizons-launch.sh"
WINE_INSTALL_ROOT = Path("/usr")
WINE_STOP_EXEC = "import os,signal,sys;os.kill(os.getpid(),signal.SIGSTOP);os.execv(sys.argv[1],sys.argv[1:])"


class Refusal(Exception):
    """A fail-closed validation error."""


def _decimal(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        raise Refusal(f"Invalid {name} in owned-Xvfb guard")
    try:
        result = int(value)
    except ValueError as error:
        raise Refusal(f"Invalid {name} in owned-Xvfb guard") from error
    if result <= 0 or str(result) != str(value):
        raise Refusal(f"Invalid {name} in owned-Xvfb guard")
    return result


def verify_owned_xvfb(guard_path, *, proc_root=None, boot_id_path=None,
                      display_socket=None, uid=None):
    """Check the same PID/start/boot/socket identity used by nh-private-input."""
    proc_root = PROC_ROOT if proc_root is None else Path(proc_root)
    boot_id_path = BOOT_ID_PATH if boot_id_path is None else Path(boot_id_path)
    display_socket = DISPLAY_SOCKET if display_socket is None else Path(display_socket)
    uid = os.getuid() if uid is None else uid

    try:
        guard = json.loads(Path(guard_path).read_text(encoding="utf-8"))
        if not isinstance(guard, dict):
            raise Refusal("Owned-Xvfb guard must contain a JSON object")
        pid = _decimal(guard.get("pid"), "pid")
        start_ticks = str(_decimal(guard.get("start_ticks"), "start_ticks"))
        socket_inode = _decimal(guard.get("socket_inode"), "socket_inode")
        boot_id = guard.get("boot_id")
        if not isinstance(boot_id, str) or not boot_id.strip():
            raise Refusal("Invalid boot_id in owned-Xvfb guard")

        proc = proc_root / str(pid)
        stat_fields = (proc / "stat").read_text(encoding="ascii").rsplit(")", 1)[1].split()
        command = (proc / "cmdline").read_bytes().split(b"\0")
        if command and command[-1] == b"":
            command.pop()
        executable = (proc / "exe").resolve(strict=True)
        executable_stat = executable.stat()
        boot_id_now = boot_id_path.read_text(encoding="ascii").strip()
        socket_stat = display_socket.stat()
        owned = (
            proc.stat().st_uid == uid
            and bool(stat_fields)
            and stat_fields[0] != "Z"
            and len(stat_fields) > 19
            and stat_fields[19] == start_ticks
            and boot_id_now == boot_id
            and len(command) >= 4
            and Path(os.fsdecode(command[0])).name == "Xvfb"
            and executable.name == "Xvfb"
            and stat.S_ISREG(executable_stat.st_mode)
            and executable_stat.st_mode & 0o111
            and command[1] == PRIVATE_DISPLAY.encode("ascii")
            and any(command[index:index + 2] == [b"-nolisten", b"tcp"]
                    for index in range(len(command) - 1))
            and stat.S_ISSOCK(socket_stat.st_mode)
            and socket_stat.st_uid == uid
            and socket_stat.st_ino == socket_inode
        )
        if not owned:
            raise Refusal("Owned private Xvfb identity changed; refusing game launch")
    except Refusal:
        raise
    except (OSError, ValueError, KeyError, IndexError, json.JSONDecodeError) as error:
        raise Refusal(f"Cannot verify owned private Xvfb guard: {error}") from error


def validate_client_args(client_args):
    """Allow only a small set of known test/UI switches and safe values."""
    index = 0
    seen = set()
    while index < len(client_args):
        option = client_args[index]
        if option == "--disable-video":
            if option in seen:
                raise Refusal("Duplicate client argument: --disable-video")
            seen.add(option)
            index += 1
            continue
        if option == "--savefrequency":
            if option in seen or index + 1 >= len(client_args) or client_args[index + 1] != "0":
                raise Refusal("Only the fixed safe pair '--savefrequency 0' is allowed")
            seen.add(option)
            index += 2
            continue
        if option == "--testmap":
            if option in seen or index + 1 >= len(client_args):
                raise Refusal("--testmap requires one safe Maps/*.h3m path")
            name = client_args[index + 1]
            parts = name.split("/")
            if ("\\" in name or "\0" in name or len(parts) < 2
                    or parts[0] != "Maps"
                    or any(part in ("", ".", "..") for part in parts)
                    or not name.lower().endswith(".h3m")):
                raise Refusal("--testmap must be a relative Maps/*.h3m path without traversal")
            seen.add(option)
            index += 2
            continue
        raise Refusal(f"Client argument is not allowlisted: {option!r}")


def _parser():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--guard", required=True, type=Path,
                        help="explicit JSON identity guard for the owned :191 Xvfb")
    parser.add_argument("--snapshot", type=Path,
                        help="frozen snapshot directory containing new-horizons (or historical vcmiclient) and resources")
    parser.add_argument("--profile", type=Path,
                        help="dedicated writable New Horizons private profile")
    parser.add_argument("--assets", type=Path,
                        help="purchaser-supplied Heroes III Complete asset directory")
    parser.add_argument("--verify-only", "--dry-run", dest="verify_only", action="store_true",
                        help="validate via the managed launcher without executing the game")
    parser.add_argument("--debugger-log", type=Path,
                        help="new exclusive gdb log inside the managed private profile")
    parser.add_argument("--wine-surface-test", type=Path,
                        help="only the numerical mask regression PE, never the game")
    parser.add_argument("--wine-test-sha256")
    parser.add_argument("--wine-sha256")
    parser.add_argument("--wine-binary", type=Path)
    parser.add_argument("--wine-fixtures", type=Path)
    parser.add_argument("--wine-fixture-manifest", type=Path)
    parser.add_argument("--wine-fixture-manifest-sha256")
    parser.add_argument("--wine-dll-manifest", type=Path)
    parser.add_argument("--wine-dll-manifest-sha256")
    parser.add_argument("--wine-run-root", type=Path)
    return parser


def _pinned_file(path, digest):
    if path is None or path.is_symlink() or not path.is_file():
        raise Refusal("Pinned input must be a regular non-symlink file")
    if (not isinstance(digest, str) or len(digest) != 64
            or any(c not in "0123456789abcdef" for c in digest)
            or hashlib.sha256(path.read_bytes()).hexdigest() != digest):
        raise Refusal("Pinned input SHA256 mismatch")
    return path.resolve(strict=True)


def wine_surface_plan(args, client_args):
    """A fixed surface-only PE contract, separate from the managed game route."""
    if client_args or any((args.snapshot, args.profile, args.assets, args.debugger_log)):
        raise Refusal("Wine surface mode cannot accept game options or arbitrary arguments")
    executable = _pinned_file(args.wine_surface_test, args.wine_test_sha256)
    if executable.name not in ("nhSdl3GrayscaleMaskRuntimeTest.exe",
                               "nhSdl3GrayscaleMaskNegativeWitness.exe"):
        raise Refusal("Wine mode only accepts the named mask regression witnesses")
    image = executable.read_bytes()
    if len(image) < 64 or image[:2] != b"MZ":
        raise Refusal("Mask witness is not a PE executable")
    offset = struct.unpack_from("<I", image, 60)[0]
    if (offset + 24 > len(image) or image[offset:offset + 4] != b"PE\0\0"
            or struct.unpack_from("<H", image, offset + 4)[0] != 0x8664
            or not struct.unpack_from("<H", image, offset + 22)[0] & 2):
        raise Refusal("Mask witness must be an executable AMD64 PE")
    wine = _pinned_file(args.wine_binary, args.wine_sha256)
    if (wine.name not in ("wine", "wine64") or not wine.is_relative_to(WINE_INSTALL_ROOT)
            or wine.read_bytes()[:4] != b"\x7fELF" or not os.access(wine, os.X_OK)):
        raise Refusal("Wine must be a pinned installed ELF wine/wine64 executable")
    server = wine.parent / "wineserver"
    if server.is_symlink() or not server.is_file() or not os.access(server, os.X_OK):
        raise Refusal("Matching installed Wine server is unavailable")
    manifest_path = _pinned_file(args.wine_fixture_manifest,
                                 args.wine_fixture_manifest_sha256)
    fixtures = args.wine_fixtures
    if fixtures is None or fixtures.is_symlink() or not fixtures.is_dir():
        raise Refusal("Wine fixtures require a regular directory")
    expected = {f"NH_academy_{name}_portrait_mask.png" for name in (
        "archMage", "genie", "giant", "gremlin", "ironGolem", "mage",
        "masterGenie", "masterGremlin", "nagaQueen", "naga", "stoneGolem", "titan")}
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or set(manifest) != expected:
        raise Refusal("Fixture receipt must bind exactly the twelve selected masks")
    if {p.name for p in fixtures.iterdir()} != expected:
        raise Refusal("Fixture directory must contain exactly twelve mask files")
    for name in sorted(expected):
        _pinned_file(fixtures / name, manifest[name])
    dll_receipt = _pinned_file(args.wine_dll_manifest, args.wine_dll_manifest_sha256)
    dlls = json.loads(dll_receipt.read_text(encoding="utf-8"))
    if not isinstance(dlls, dict) or set(dlls) != {
            "SDL3.dll", "SDL3_image.dll", "libpng16.dll", "zlib1.dll",
            "vcruntime140.dll", "vcruntime140_1.dll"}:
        raise Refusal("DLL receipt must pin the shipping SDL/image/PNG/zlib/VC runtime files")
    for name, digest in dlls.items():
        _pinned_file(executable.parent / name, digest)
    root = args.wine_run_root
    repository = Path(__file__).resolve().parents[2]
    if (root is None or root.is_symlink() or not root.is_dir()
            or root.stat().st_uid != os.getuid() or stat.S_IMODE(root.stat().st_mode) != 0o700):
        raise Refusal("Wine run root must be an existing owned mode-0700 directory")
    root = root.resolve(strict=True)
    if root == Path.home() or root == Path("/") or root.is_relative_to(repository):
        raise Refusal("Wine run root must be private and outside the checkout")
    if any(root.iterdir()):
        raise Refusal("Wine run root must be fresh and empty; no reused prefix")
    return [str(wine), str(executable), str(fixtures.resolve())], root


def run_wine_surface(args, client_args):
    """At most 5s stop + 90s child + 10s reap + 10s kill-server + 10s wait-server.

    The caller owns Xvfb and a separate 150-second whole-run deadline. This
    helper owns only its fresh Wine prefix and launched process group.
    """
    command, root = wine_surface_plan(args, client_args)
    if args.verify_only:
        return 0
    env = {"PATH": "/usr/bin:/bin", "DISPLAY": PRIVATE_DISPLAY,
           "WINEDEBUG": "-all,+loaddll",
           "WINEDLLOVERRIDES": "SDL3,SDL3_image=n;libpng16,zlib1,vcruntime140,vcruntime140_1=n,b;mscoree,mshtml=",
           "SDL_VIDEODRIVER": "dummy", "SDL_VIDEO_DRIVER": "dummy",
           "SDL_AUDIODRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy"}
    for key, name in (("HOME", "home"), ("WINEPREFIX", "prefix"),
                      ("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                      ("XDG_CACHE_HOME", "cache"), ("XDG_STATE_HOME", "state"),
                      ("XDG_RUNTIME_DIR", "runtime")):
        directory = root / name
        directory.mkdir(mode=0o700)
        env[key] = str(directory)
    # Wine filters Unix HOME; its explicit WINEHOME variant supplies Windows HOME.
    env["WINEHOME"] = env["HOME"]
    command.extend(["--wine-private-environment", str(root)])
    verify_owned_xvfb(args.guard)
    stopped_command = [sys.executable, "-c", WINE_STOP_EXEC, *command]
    child = subprocess.Popen(stopped_command, env=env, cwd=Path(command[1]).parent,
                             start_new_session=True)
    proof = {"pid": child.pid, "command": command, "verified_environment": False}
    try:
        # Wine can rewrite its initial Unix process memory. Inspect the same PID
        # while stopped before the fixed execv, then require the PE's own Win32
        # environment witness before any SDL call. No arbitrary trampoline code.
        deadline = time.monotonic() + 5
        while True:
            fields = (PROC_ROOT / str(child.pid) / "stat").read_text().rsplit(")", 1)[1].split()
            if fields[0] == "T":
                break
            if child.poll() is not None or time.monotonic() >= deadline:
                raise Refusal("Trusted pre-exec stop was not established within five seconds")
            time.sleep(.005)
        actual = dict(entry.split(b"=", 1) for entry in
                      (PROC_ROOT / str(child.pid) / "environ").read_bytes().split(b"\0")
                      if b"=" in entry)
        proof["actual_environment"] = {key: actual.get(key.encode(), b"<missing>").decode(errors="replace")
                                       for key in env}
        proof["environment_mismatches"] = [key for key, value in env.items()
                                            if actual.get(key.encode()) != value.encode()]
        if proof["environment_mismatches"] or b"WAYLAND_DISPLAY" in actual:
            raise Refusal("Actual Wine child environment does not match the private contract")
        proof["verified_environment"] = True
        proof["environment"] = {key: actual[key.encode()].decode() for key in env}
        if (PROC_ROOT / str(child.pid) / "exe").resolve() != Path(sys.executable).resolve():
            raise Refusal("Actual stopped child differs from the trusted Python executable")
        proof["start_ticks"] = fields[19]
        proof["pre_exec_executable_sha256"] = hashlib.sha256(Path(sys.executable).read_bytes()).hexdigest()
        os.kill(child.pid, signal.SIGCONT)
        proof["returncode"] = child.wait(timeout=90)
        marker = root / "pe-environment-proof.txt"
        if (marker.is_symlink() or not marker.is_file()
                or marker.read_bytes() != b"WINE_PRIVATE_ENV_VERIFIED\n"):
            raise Refusal("Actual Windows PE did not prove its private environment before SDL")
        marker.chmod(0o600)
        proof["verified_windows_environment"] = True
        proof["windows_environment_proof_sha256"] = hashlib.sha256(marker.read_bytes()).hexdigest()
        return proof["returncode"]
    except subprocess.TimeoutExpired as error:
        proof["original_error"] = {"class": type(error).__name__, "message": str(error)}
        raise Refusal("Wine surface regression exceeded its 90-second bound") from error
    except Exception as error:
        proof["original_error"] = {"class": type(error).__name__, "message": str(error)}
        raise
    finally:
        cleanup_errors = []
        if child.poll() is None:
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass  # Exit between poll and kill: still reap and clean the prefix.
            except OSError as error:
                cleanup_errors.append(f"kill-group: {error}")
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                cleanup_errors.append("owned child reap timed out")
        # Only this fresh prefix's server; never the user's Wine server.
        server = str(Path(command[0]).parent / "wineserver")
        proof["server_cleanup"] = {}
        for option in ("-k", "-w"):
            try:
                status = subprocess.run([server, option], env=env, timeout=10, check=False).returncode
                proof["server_cleanup"][option] = status
                if status != 0:
                    cleanup_errors.append(f"prefix wineserver {option} exited {status}")
            except (OSError, subprocess.TimeoutExpired) as error:
                proof["server_cleanup"][option] = str(error)
                cleanup_errors.append(f"prefix wineserver {option} failed")
        proof["reaped"] = child.poll() is not None
        if not proof["reaped"]:
            cleanup_errors.append("owned child not reaped")
        proof["cleanup_errors"] = cleanup_errors
        (root / "wine-proof.json").write_text(json.dumps(proof, indent=2) + "\n")
        (root / "wine-proof.json").chmod(0o600)
        if cleanup_errors:
            raise Refusal("Wine cleanup failed; retained wine-proof.json; not a successful run")


def _split_client_args(argv):
    values = list(sys.argv[1:] if argv is None else argv)
    if "--" not in values:
        return values, []
    boundary = values.index("--")
    return values[:boundary], values[boundary + 1:]


def main(argv=None):
    option_argv, client_args = _split_client_args(argv)
    parser = _parser()
    args = parser.parse_args(option_argv)
    try:
        # Refuse before creating any child process, even the managed wrapper.
        verify_owned_xvfb(args.guard)
        if args.wine_surface_test is not None:
            return run_wine_surface(args, client_args)
        if any((args.wine_test_sha256, args.wine_sha256, args.wine_binary, args.wine_fixtures,
                args.wine_fixture_manifest, args.wine_fixture_manifest_sha256,
                args.wine_dll_manifest, args.wine_dll_manifest_sha256,
                args.wine_run_root)):
            raise Refusal("Wine options require --wine-surface-test")
        if any(value is None for value in (args.snapshot, args.profile, args.assets)):
            raise Refusal("Game mode requires --snapshot, --profile and --assets")
        validate_client_args(client_args)

        snapshot_input = args.snapshot.expanduser()
        assets_input = args.assets.expanduser()
        profile_input = args.profile.expanduser()
        if snapshot_input.is_symlink() or not snapshot_input.is_dir():
            raise Refusal("Snapshot must be an existing, non-symlink directory")
        if assets_input.is_symlink() or not assets_input.is_dir():
            raise Refusal("Purchaser asset path must be an existing, non-symlink directory")
        if profile_input.is_symlink():
            raise Refusal("Private profile cannot be a symlink")

        snapshot = snapshot_input.resolve(strict=True)
        assets = assets_input.resolve(strict=True)
        profile = profile_input.resolve(strict=False)
        clients = [snapshot / name for name in ("new-horizons", "vcmiclient")
                   if (snapshot / name).exists() or (snapshot / name).is_symlink()]
        if len(clients) != 1:
            raise Refusal("Frozen snapshot must contain exactly one current or legacy client")
        client = clients[0]
        library = snapshot / "libvcmi.so"
        if client.is_symlink() or not client.is_file() or not os.access(client, os.X_OK):
            raise Refusal("Frozen snapshot must contain an executable regular New Horizons client")
        if library.is_symlink() or not library.is_file():
            raise Refusal("Frozen snapshot must contain its matching regular libvcmi.so")
        if not LAUNCHER.is_file() or not os.access(LAUNCHER, os.X_OK):
            raise Refusal(f"Managed New Horizons launcher is unavailable: {LAUNCHER}")

        command = [
            str(LAUNCHER),
            "--client", str(client),
            "--resources", str(snapshot),
            "--profile", str(profile),
            "--assets", str(assets),
        ]
        if args.verify_only:
            command.append("--verify-only")
        if args.debugger_log is not None:
            command.extend(["--debugger-log", str(args.debugger_log.expanduser())])
        if client_args:
            command.extend(["--", *client_args])

        child_env = os.environ.copy()
        child_env["DISPLAY"] = PRIVATE_DISPLAY
        child_env["SDL_VIDEODRIVER"] = "x11"
        child_env["SDL_AUDIODRIVER"] = "dummy"
        child_env["SDL_VIDEO_DRIVER"] = "x11"
        child_env["SDL_AUDIO_DRIVER"] = "dummy"
        child_env.pop("WAYLAND_DISPLAY", None)
        return subprocess.run(command, env=child_env, check=False).returncode
    except Refusal as error:
        parser.exit(2, f"nh-private-launch: {error}\n")
    except OSError as error:
        parser.exit(2, f"nh-private-launch: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
