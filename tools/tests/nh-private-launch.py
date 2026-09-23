#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Launch New Horizons only through the managed profile on the guarded :191 Xvfb.

This is the GUI launch entry point for a future authorized tester. Always pass
an explicit owned-Xvfb guard, frozen snapshot, private profile, and purchaser
asset directory. It invokes tools/new-horizons-launch.sh with argv (never a
shell command), pins the child display/video/audio drivers, and removes the
host Wayland display. Future GUI sessions must enter through this helper; do
not start raw vcmiclient or manually bypass the managed launcher. This helper
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
import json
import os
from pathlib import Path
import stat
import subprocess
import sys


PRIVATE_DISPLAY = ":191"
PROC_ROOT = Path("/proc")
BOOT_ID_PATH = Path("/proc/sys/kernel/random/boot_id")
DISPLAY_SOCKET = Path("/tmp/.X11-unix/X191")
LAUNCHER = Path(__file__).resolve().parents[1] / "new-horizons-launch.sh"


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
    parser.add_argument("--snapshot", required=True, type=Path,
                        help="frozen snapshot directory containing vcmiclient and resources")
    parser.add_argument("--profile", required=True, type=Path,
                        help="dedicated writable New Horizons private profile")
    parser.add_argument("--assets", required=True, type=Path,
                        help="purchaser-supplied Heroes III Complete asset directory")
    parser.add_argument("--verify-only", "--dry-run", dest="verify_only", action="store_true",
                        help="validate via the managed launcher without executing the game")
    return parser


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
        client = snapshot / "vcmiclient"
        library = snapshot / "libvcmi.so"
        if client.is_symlink() or not client.is_file() or not os.access(client, os.X_OK):
            raise Refusal("Frozen snapshot must contain an executable regular vcmiclient")
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
