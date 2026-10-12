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
  --testmap Maps/<relative-map>.h3m (or .vmap)
  --spectate
"""
import argparse
import hashlib
import importlib.util
import json
import ntpath
import os
from pathlib import Path
import stat
import signal
import struct
import subprocess
import sys
import time
import uuid


PRIVATE_DISPLAY = ":191"
PROC_ROOT = Path("/proc")
BOOT_ID_PATH = Path("/proc/sys/kernel/random/boot_id")
DISPLAY_SOCKET = Path("/tmp/.X11-unix/X191")
LAUNCHER = Path(__file__).resolve().parents[1] / "new-horizons-launch.sh"
WINE_INSTALL_ROOT = Path("/usr")
WINE_STOP_EXEC = "import os,signal,sys;os.kill(os.getpid(),signal.SIGSTOP);os.execv(sys.argv[1],sys.argv[1:])"


class Refusal(Exception):
    """A fail-closed validation error."""


def validate_wine_identity(row, *, owner_uid):
    """Mandatory identity for one explicitly tracked process, not name discovery."""
    if type(owner_uid) is not int or owner_uid < 0 or not isinstance(row, dict):
        raise Refusal("Invalid Wine ownership record")
    if any(type(row.get(k)) is not int or row[k] <= 0 for k in ("pid", "start_ticks")):
        raise Refusal("Wine PID/start identity missing")
    if type(row.get("uid")) is not int or row["uid"] != owner_uid:
        raise Refusal("Wine process is not owned")
    try:
        boot = uuid.UUID(row.get("boot_id", ""))
    except (ValueError, AttributeError, TypeError) as error:
        raise Refusal("Invalid Wine boot identity") from error
    if boot.int == 0 or str(boot) != row["boot_id"]:
        raise Refusal("Invalid Wine boot identity")


def validate_wine_exec_transition(stopped, actual, loader, *, owner_uid, expected_bootstrap=None):
    for row in (stopped, actual):
        validate_wine_identity(row, owner_uid=owner_uid)
    if (stopped.get("state") != "T" or actual.get("state") not in ("R", "S", "D", "I")
            or not isinstance(loader, dict) or not isinstance(loader.get("argv"), list)
            or not loader["argv"] or not Path(loader.get("exe", "")).is_absolute()
            or not _is_sha(loader.get("sha256"))):
        raise Refusal("Unproven Wine loader transition")
    if any(stopped[k] != actual[k] for k in ("pid", "start_ticks", "uid", "boot_id")):
        raise Refusal("Wine identity changed across exec")
    permitted_argv = [loader["argv"]]
    if expected_bootstrap is not None:
        # Wine may remove its own argv[0], but only for the admitted bootstrap.
        # Keep the observed snapshots intact and the physical loader pins exact.
        if (not isinstance(expected_bootstrap, str) or not Path(expected_bootstrap).is_absolute()
                or loader["argv"][:2] != [loader["exe"], expected_bootstrap]):
            raise Refusal("Bootstrap differs from pinned loader command")
        permitted_argv.append(loader["argv"][1:])
    if (any(actual.get(k) != loader[k] for k in ("exe", "sha256"))
            or actual.get("argv") not in permitted_argv):
        raise Refusal("Wine loader does not match exact pinned transition")


def _is_sha(value):
    return isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value)


def parse_wine_fullgame_witness(payload, *, nonce, executable, facade, localappdata):
    """Win32 before-engine-SDL proof, not DLL-initializer or input authority."""
    if not _is_sha(nonce) or not isinstance(payload, bytes) or len(payload) > 8192 or len(payload) % 2:
        raise Refusal("Invalid Win32 witness")
    try:
        lines = payload.decode("utf-16-le", errors="strict").split("\n")
        suffixes = (r"\HeroesIII-NewHorizons\content", r"\HeroesIII-NewHorizons\config",
                    r"\HeroesIII-NewHorizons\cache", r"\HeroesIII-NewHorizons\logs", r"\new-horizons\Saves")
        paths = [executable, facade, localappdata,
                 ntpath.join(ntpath.dirname(executable), "config", "dirs.json"),
                 localappdata + r"\.nh-private-wine-gate\proof.utf16le",
                 localappdata + r"\.nh-private-wine-gate\approved.utf16le",
                 localappdata + suffixes[2] + r"\extracted",
                 *[localappdata + s for s in suffixes]]
        if any(not isinstance(p, str) or not p or len(p.encode("utf-16-le")) // 2 >= 260
               or any(c in p for c in ("\n", "\r", "\0")) or ".." in p.split("\\") for p in paths):
            raise Refusal("Win32 paths exceed current resolver contract")
    except UnicodeError as error:
        raise Refusal("Invalid Win32 witness encoding") from error
    if (len(lines) != 16 or lines[-1] != "" or any("\r" in x or "\0" in x for x in lines)
            or lines[:2] != ["NH_WINE_PRE_SDL_V1", nonce]
            or not localappdata.startswith("Z:\\") or not localappdata.endswith(r"\localappdata")
            or lines[3:10] != [executable, facade, localappdata, "dummy", "dummy", "windows", "windows"]
            or lines[10:15] != [localappdata + s for s in suffixes]):
        raise Refusal("Win32 witness environment/challenge mismatch")
    pid = _decimal(lines[2], "Win32 PID")
    if pid > 0xffffffff:
        raise Refusal("Win32 PID exceeds uint32")
    return {"windows_pid": pid, "nonce": nonce,
            "verified_win32_before_engine_sdl_init_environment": True, "input_authorized": False}


def validate_wine_private_root(root, *, uid):
    if not root.is_absolute() or ".." in root.parts:
        raise Refusal("Wine root must be an absolute plain path")
    for path in (root, *root.parents):
        if path.is_symlink():
            raise Refusal("Wine root has symlink ancestor")
    info = root.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != uid or stat.S_IMODE(info.st_mode) != 0o700:
        raise Refusal("Wine root must be owned0700")
    repository = Path(__file__).resolve().parents[2]
    if root == Path.home() or root == Path("/") or root.is_relative_to(repository) or any(root.iterdir()):
        raise Refusal("Wine root must be fresh outside checkout")


def validate_wine_mapping_rows(rows, expected):
    """Same-open-file hash/stat proofs supplied by the owner, never basename matching."""
    required = {"new-horizons.exe", "VCMI_lib.dll", "SDL3.dll", "SDL3_image.dll", "SDL3_mixer.dll", "SDL3_ttf.dll"}
    if not isinstance(expected, list) or not required <= {Path(x.get("path", "")).name for x in expected}:
        raise Refusal("PE/facade/four-provider mappings required")
    for artifact in expected:
        if (not Path(artifact.get("path", "")).is_absolute() or not _is_sha(artifact.get("sha256"))
                or type(artifact.get("inode")) is not int or artifact["inode"] <= 0
                or not isinstance(artifact.get("device"), str) or not artifact["device"]):
            raise Refusal("Invalid selected mapped-file proof")
        matches = [r for r in rows if r.get("path") == artifact["path"]]
        if not matches or any(any(r.get(k) != artifact[k] for k in ("sha256", "inode", "device")) for r in matches):
            raise Refusal("Selected Wine mapping changed/missing")
    selected = {x["path"] for x in expected}
    if any(Path(r.get("path", "")).name.casefold() in {name.casefold() for name in required}
           and r.get("path") not in selected for r in rows):
        raise Refusal("Alternative selected PE/provider mapping")


def validate_wine_environment(actual, required, *, expected_loader=None):
    if expected_loader is not None and (not isinstance(expected_loader, str)
            or not Path(expected_loader).is_absolute() or str(Path(expected_loader)) != expected_loader
            or ".." in Path(expected_loader).parts or "\0" in expected_loader):
        raise Refusal("Invalid admitted Wine loader environment pin")
    fixed = {"DISPLAY": PRIVATE_DISPLAY, "SDL_VIDEODRIVER": "windows", "SDL_VIDEO_DRIVER": "windows",
             "SDL_AUDIODRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy"}
    for key in ("HOME", "WINEHOME", "WINEPREFIX", "LOCALAPPDATA", "XDG_DATA_HOME", "XDG_CONFIG_HOME",
                "XDG_CACHE_HOME", "XDG_STATE_HOME", "XDG_RUNTIME_DIR"):
        if not required.get(key) or actual.get(key) != required[key]:
            raise Refusal("Actual Wine private profile differs")
    if any(actual.get(k) != v for k,v in fixed.items()):
        raise Refusal("Actual Wine silent/display route differs")
    rejected = [k for k in ("WAYLAND_DISPLAY", "LD_PRELOAD", "LD_AUDIT", "LD_LIBRARY_PATH",
                            "PULSE_SERVER", "PIPEWIRE_REMOTE", "WINELOADER", "WINESERVER")
                if actual.get(k) and not (k == "WINELOADER" and expected_loader is not None
                                         and actual[k] == expected_loader)]
    if rejected:
        raise Refusal("Wine host/injection route refused: keys=" + ",".join(rejected))


def render_timing_contract(args):
    enabled = args.wine_render_load_timings
    text = args.wine_render_load_timing_limit
    if not enabled:
        if text is not None:
            raise Refusal("Render timing limit requires explicit timing opt-in")
        return {"enabled": False, "limit": None}
    if text is None:
        return {"enabled": True, "limit": 1024}
    try:
        limit = int(text)
    except (TypeError, ValueError) as error:
        raise Refusal("Invalid render timing limit") from error
    if str(limit) != text or not 1 <= limit <= 16384:
        raise Refusal("Render timing limit must be canonical decimal1..16384")
    return {"enabled": True, "limit": limit}


def validate_render_timing_environment(actual, contract):
    if (not isinstance(contract, dict) or set(contract) != {"enabled", "limit"}
            or type(contract["enabled"]) is not bool):
        raise Refusal("Invalid render timing contract")
    keys = ("NH_RENDER_LOAD_TIMINGS", "NH_RENDER_LOAD_TIMING_LIMIT")
    if not contract["enabled"]:
        if contract["limit"] is not None or any(key in actual for key in keys):
            raise Refusal("Unrequested render timing environment")
    elif (type(contract["limit"]) is not int or not 1 <= contract["limit"] <= 16384
            or actual.get(keys[0]) != "1" or actual.get(keys[1]) != str(contract["limit"])):
        raise Refusal("Actual render timing environment differs from admitted opt-in")


def verify_wine_runtime(context, *, plan):
    """Repeated owner-acquired proof; no input or acknowledgement is sent here."""
    uid = os.getuid()
    if (not _is_sha(plan.get("launch_nonce")) or context.get("nonce") != plan["launch_nonce"]
            or not isinstance(plan.get("stopped_identity"), dict) or context.get("stopped") != plan["stopped_identity"]
            or not isinstance(plan.get("loader_identity"), dict) or context.get("loader") != plan["loader_identity"]):
        raise Refusal("Runtime proof differs from retained launch challenge/identity")
    before, after = context["before"], context["after"]
    validate_wine_identity(before, owner_uid=uid)
    validate_wine_identity(after, owner_uid=uid)
    if any(before[k] != after[k] for k in ("pid", "start_ticks", "uid", "boot_id")):
        raise Refusal("Wine process changed around runtime proof")
    if plan.get("bootstrap"):
        bootstrap = context.get("bootstrap", {})
        for snapshot in ("before", "after"):
            validate_wine_exec_transition(context["stopped"], bootstrap.get(snapshot), context["loader"],
                                          owner_uid=uid, expected_bootstrap=plan["bootstrap"])
        if any(bootstrap["before"][k] != bootstrap["after"][k] for k in ("pid", "start_ticks", "uid", "boot_id")):
            raise Refusal("Bootstrap changed around runtime proof")
        game = plan.get("game_identity")
        if not isinstance(game, dict) or context.get("game_identity") != game:
            raise Refusal("Game child differs from retained owned identity")
        validate_wine_identity(game, owner_uid=uid)
        if any(after.get(k) != game.get(k) for k in ("pid", "start_ticks", "uid", "boot_id", "exe", "sha256", "argv")):
            raise Refusal("Owned game child changed")
        if after.get("state") not in ("R", "S", "D", "I") or after["pid"] == context["stopped"]["pid"] or after["exe"] != plan["wine"] or after["sha256"] != plan["wine_sha256"]:
            raise Refusal("Game is not a distinct pinned Wine child")
        rows = bootstrap.get("mappings", [])
        expected_bootstrap = plan.get("bootstrap_mapping")
        if not isinstance(expected_bootstrap, dict) or expected_bootstrap.get("path") != plan["bootstrap"] or expected_bootstrap.get("sha256") != plan["bootstrap_sha256"]:
            raise Refusal("Bootstrap mapping pin absent")
        if not rows or any(row != expected_bootstrap for row in rows):
            raise Refusal("Bootstrap mapped identity changed")
    else:
        validate_wine_exec_transition(context["stopped"], after, context["loader"], owner_uid=uid)
    root = Path(plan["root"])
    expected_environment = {key: str(root/name) for key,name in (
        ("HOME", "home"), ("WINEHOME", "home"), ("WINEPREFIX", "prefix"),
        ("XDG_DATA_HOME", "data"), ("XDG_CONFIG_HOME", "config"),
        ("XDG_CACHE_HOME", "cache"), ("XDG_STATE_HOME", "state"), ("XDG_RUNTIME_DIR", "runtime"))}
    win32_local = "Z:" + str(root/"localappdata").replace("/", "\\")
    expected_environment["LOCALAPPDATA"] = win32_local
    if context["required_environment"] != expected_environment:
        raise Refusal("Runtime profile pins differ from admitted private root")
    for directory in {root, *[root/name for name in ("home", "prefix", "data", "config", "cache", "state", "runtime", "localappdata")]}:
        info = directory.lstat()
        if (not stat.S_ISDIR(info.st_mode) or info.st_uid != uid or stat.S_IMODE(info.st_mode) & 0o022
                or (directory == root and stat.S_IMODE(info.st_mode) != 0o700)
                or any(parent.is_symlink() for parent in directory.parents)):
            raise Refusal("Runtime private profile directory changed")
    # Wine 10 init_paths sets WINELOADER from its physical executable. Only the
    # admitted bootstrap route, whose process/hash proofs passed above, may
    # retain that exact value. Direct/surface callers keep the strict default.
    validate_wine_environment(context["environment"], expected_environment,
                              expected_loader=plan["wine"] if plan.get("bootstrap") else None)
    validate_render_timing_environment(context["environment"],
        plan.get("render_timings", {"enabled": False, "limit": None}))
    required_dlls = {"VCMI_lib.dll", "SDL3.dll", "SDL3_image.dll", "SDL3_mixer.dll", "SDL3_ttf.dll"}
    pinned = {str(Path(plan["executable"]).parent/name): pin for name,pin in plan["dlls"].items() if name in required_dlls}
    pinned[plan["executable"]] = plan["executable_sha256"]
    if {r.get("path"):r.get("sha256") for r in context["expected_mappings"]} != pinned:
        raise Refusal("Runtime map pins differ from admitted package")
    validate_wine_mapping_rows(context["mappings"], context["expected_mappings"])
    if (context["win32_executable"] != "Z:"+plan["executable"].replace("/", "\\")
            or context["win32_facade"] != "Z:"+str(Path(plan["executable"]).parent/"VCMI_lib.dll").replace("/", "\\")
            or context["win32_localappdata"] != win32_local):
        raise Refusal("Win32 witness paths differ from admitted package/profile")
    proof = parse_wine_fullgame_witness(context["witness"], nonce=context["nonce"],
        executable=context["win32_executable"], facade=context["win32_facade"], localappdata=context["win32_localappdata"])
    if (context.get("supervisor_ready_before_resume") is not True or context.get("owned_service_graph_verified") is not True
            or type(context.get("age_after_io")) not in (int,float) or not 0 <= context["age_after_io"] < 600):
        raise Refusal("Wine cleanup/service/input deadline gate missing")
    return proof


def wine_fullgame_plan(args, client_args):
    """Real admission wiring only. No process creation, profile writes or ack."""
    if client_args or args.wine_surface_test or any((args.snapshot, args.profile, args.assets, args.debugger_log,
            args.wine_fixtures, args.wine_fixture_manifest, args.wine_fixture_manifest_sha256)):
        raise Refusal("Full-game Wine mode cannot mix other launch routes or arguments")
    timings = render_timing_contract(args)
    for ancestor in (args.wine_fullgame, *args.wine_fullgame.parents):
        if ancestor.is_symlink():
            raise Refusal("Player package has symlink ancestor")
    if not args.wine_fullgame.is_absolute():
        raise Refusal("Player path must be absolute")
    executable = _pinned_file(args.wine_fullgame, args.wine_test_sha256)
    if executable.name != "new-horizons.exe":
        raise Refusal("Full-game Wine requires selected player PE")
    image = executable.read_bytes()
    if len(image) < 64 or image[:2] != b"MZ":
        raise Refusal("Player is not a PE")
    offset = struct.unpack_from("<I", image, 60)[0]
    if (offset + 24 > len(image) or image[offset:offset+4] != b"PE\0\0"
            or struct.unpack_from("<H", image, offset+4)[0] != 0x8664
            or not struct.unpack_from("<H", image, offset+22)[0] & 2):
        raise Refusal("Player must be executable AMD64 PE")
    bootstrap = None
    if args.wine_bootstrap is not None or args.wine_bootstrap_sha256 is not None:
        candidate = args.wine_bootstrap
        if candidate is None or not candidate.is_absolute() or ".." in candidate.parts:
            raise Refusal("Bootstrap requires an absolute pinned path")
        if any(path.is_symlink() for path in (candidate, *candidate.parents)):
            raise Refusal("Bootstrap has symlink ancestor")
        bootstrap = _pinned_file(candidate, args.wine_bootstrap_sha256)
        data = bootstrap.read_bytes()
        if len(data) < 64 or data[:2] != b"MZ":
            raise Refusal("Bootstrap is not a PE")
        position = struct.unpack_from("<I", data, 60)[0]
        if (position + 24 > len(data) or data[position:position+4] != b"PE\0\0"
                or struct.unpack_from("<H", data, position+4)[0] != 0x8664
                or not struct.unpack_from("<H", data, position+22)[0] & 2
                or struct.unpack_from("<H", data, position+22)[0] & 0x2000
                or "NH_WINE_BOOTSTRAP_V1".encode("utf-16-le") not in data):
            raise Refusal("Bootstrap lacks the fixed AMD64 protocol")
        if bootstrap == executable or len(("Z:"+str(bootstrap).replace("/", "\\")).encode("utf-16-le")) // 2 >= 260:
            raise Refusal("Bootstrap identity/path is unsuitable")
    receipt = _pinned_file(args.wine_dll_manifest, args.wine_dll_manifest_sha256)
    dlls = json.loads(receipt.read_text())
    required = {"VCMI_lib.dll", "SDL3.dll", "SDL3_image.dll", "SDL3_mixer.dll", "SDL3_ttf.dll"}
    if not isinstance(dlls, dict) or not required <= set(dlls):
        raise Refusal("Full-game manifest lacks facade/four SDL providers")
    for name, pin in dlls.items():
        if not isinstance(name, str) or Path(name).name != name or not name.endswith(".dll"):
            raise Refusal("Unsafe full-game DLL receipt")
        _pinned_file(executable.parent/name, pin)
    wine = _pinned_file(args.wine_binary, args.wine_sha256)
    if not wine.is_relative_to(WINE_INSTALL_ROOT) or wine.name not in ("wine", "wine64") or wine.read_bytes()[:4] != b"\x7fELF" or not os.access(wine, os.X_OK):
        raise Refusal("Full-game Wine loader must be installed pinned ELF")
    server = _pinned_file(wine.parent/"wineserver", args.wine_server_sha256)
    if not os.access(server, os.X_OK):
        raise Refusal("Pinned prefix server is not executable")
    input_program = _pinned_file(Path(__file__).with_name("nh-private-input.py"), args.wine_input_sha256)
    if args.wine_run_root is None:
        raise Refusal("Fresh private full-game root required")
    validate_wine_private_root(args.wine_run_root, uid=os.getuid())
    content_module_path = Path(__file__).with_name("nh_wine_profile.py")
    if content_module_path.is_symlink() or not content_module_path.is_file():
        raise Refusal("Adjacent Wine content admission module unavailable")
    spec = importlib.util.spec_from_file_location("nh_wine_profile_guarded", content_module_path)
    content_module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(content_module)
    try:
        content = content_module.load_content_manifest(args.wine_content_manifest,
                                                     args.wine_content_manifest_sha256)
    except content_module.Refusal as error:
        raise Refusal(str(error)) from error
    fixture_map = args.wine_fixture_map
    if fixture_map is not None:
        # Share the owner's exact typed argv contract, rather than admitting a
        # second basename grammar or any general client-argument passthrough.
        owner_path = Path(__file__).with_name("nh_wine_fullgame_owner.py")
        if owner_path.is_symlink() or not owner_path.is_file():
            raise Refusal("Adjacent full-game Wine ownership dispatcher unavailable")
        spec = importlib.util.spec_from_file_location("nh_wine_fixture_admission", owner_path)
        owner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(owner)
        try:
            owner.fixture_arguments({"fixture_map": fixture_map, "content": content})
        except owner.Refusal as error:
            raise Refusal(str(error)) from error
        if bootstrap and "NH_WINE_BOOTSTRAP_FIXTURE_V1".encode("utf-16-le") not in bootstrap.read_bytes():
            raise Refusal("Pinned bootstrap lacks typed fixture capability")
    # Root remains untouched, and even the future expanded profile must fit MAX_PATH.
    local = "Z:" + str(args.wine_run_root).replace("/", "\\") + r"\localappdata"
    bounded = [local+r"\HeroesIII-NewHorizons\cache\extracted",
               "Z:"+str(executable.parent/"config/dirs.json").replace("/", "\\")]
    if fixture_map is not None:
        bounded.append(local+"\\HeroesIII-NewHorizons\\content\\Maps\\"+fixture_map)
    if any(len(path.encode("utf-16-le"))//2 >= 260 for path in bounded):
        raise Refusal("Private profile exceeds existing Win32 path bound")
    return {"executable": str(executable), "wine": str(wine), "server": str(server),
            "root": str(args.wine_run_root), "guard": str(args.guard), "dlls": dlls,
            "executable_sha256": args.wine_test_sha256, "wine_sha256": args.wine_sha256,
            "server_sha256": args.wine_server_sha256, "input_deadline": 600,
            "input_program": str(input_program), "input_sha256": args.wine_input_sha256,
            "content": content, "bootstrap": str(bootstrap) if bootstrap else None,
            "bootstrap_sha256": args.wine_bootstrap_sha256, "render_timings": timings,
            "fixture_map": fixture_map}


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
        if option in ("--disable-video", "--spectate"):
            if option in seen:
                raise Refusal(f"Duplicate client argument: {option}")
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
                raise Refusal("--testmap requires one safe Maps/*.h3m or Maps/*.vmap path")
            name = client_args[index + 1]
            parts = name.split("/")
            if ("\\" in name or "\0" in name or len(parts) < 2
                    or parts[0] != "Maps"
                    or any(part in ("", ".", "..") for part in parts)
                    or not name.lower().endswith((".h3m", ".vmap"))):
                raise Refusal("--testmap must be a relative Maps/*.h3m or Maps/*.vmap path without traversal")
            seen.add(option)
            index += 2
            continue
        raise Refusal(f"Client argument is not allowlisted: {option!r}")


def _parser():
    class SingleFixtureMap(argparse.Action):
        def __call__(self, parser, namespace, value, option_string=None):
            if getattr(namespace, self.dest, None) is not None:
                parser.error("--wine-fixture-map may appear only once")
            setattr(namespace, self.dest, value)

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
    parser.add_argument("--wine-fullgame", type=Path,
                        help="guarded full-game route requiring a witness-capable PE and private ownership checks; not gameplay-certified")
    parser.add_argument("--wine-server-sha256")
    parser.add_argument("--wine-input-sha256", help="required full-game-only pin of adjacent maintained input helper")
    parser.add_argument("--wine-bootstrap", type=Path, help="optional pinned SDL-free Win32 child-environment bootstrap")
    parser.add_argument("--wine-bootstrap-sha256")
    parser.add_argument("--wine-fixture-map", metavar="NAME.vmap", action=SingleFixtureMap,
                        help="full-game-only exact manifest-bound map; fixed testmap/spectate/no-save arguments")
    parser.add_argument("--wine-render-load-timings", action="store_true",
                        help="full-game-only bounded existing render-load diagnostics")
    parser.add_argument("--wine-render-load-timing-limit", metavar="1..16384")
    parser.add_argument("--wine-content-manifest", type=Path,
                        help="required full-game-only explicit purchaser-content and fixture manifest")
    parser.add_argument("--wine-content-manifest-sha256")
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
    if client_args or args.wine_fixture_map is not None or any((args.snapshot, args.profile, args.assets, args.debugger_log)):
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
        if args.wine_fullgame is not None:
            plan = wine_fullgame_plan(args, client_args)
            if args.verify_only:
                return 0
            # Kept separate from native/surface modes; never fall back if owner absent.
            owner_path = Path(__file__).with_name("nh_wine_fullgame_owner.py")
            if owner_path.is_symlink() or not owner_path.is_file():
                raise Refusal("Adjacent full-game Wine ownership dispatcher unavailable")
            spec = importlib.util.spec_from_file_location("nh_wine_fullgame_owner_guarded", owner_path)
            owner = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(owner)
            try:
                return owner.run_fullgame(plan, verify_xvfb=verify_owned_xvfb,
                    verify_runtime=lambda context: verify_wine_runtime(context, plan=plan))
            except owner.Refusal as error:
                raise Refusal(str(error)) from error
        if args.wine_surface_test is not None:
            if args.wine_fixture_map is not None or args.wine_input_sha256 or args.wine_server_sha256 or args.wine_content_manifest or args.wine_content_manifest_sha256 or args.wine_bootstrap or args.wine_bootstrap_sha256 or args.wine_render_load_timings or args.wine_render_load_timing_limit is not None:
                raise Refusal("Full-game-only options misplaced in surface mode")
            return run_wine_surface(args, client_args)
        if any((args.wine_test_sha256, args.wine_sha256, args.wine_binary, args.wine_fixtures,
                args.wine_fixture_manifest, args.wine_fixture_manifest_sha256,
                args.wine_dll_manifest, args.wine_dll_manifest_sha256,
                args.wine_run_root, args.wine_server_sha256, args.wine_input_sha256,
                args.wine_content_manifest, args.wine_content_manifest_sha256,
                args.wine_bootstrap, args.wine_bootstrap_sha256,
                args.wine_render_load_timings, args.wine_render_load_timing_limit is not None,
                args.wine_fixture_map is not None)):
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
