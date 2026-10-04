#!/usr/bin/env python3
"""Reproduce runtime assets from the retained user-supplied Mage Guild archives.

Raw image, mask and animation bytes are preserved. JSON animation descriptors
with a basepath receive a trailing slash because VCMI concatenates the basepath
and frame filenames literally. Faction rules are deliberately not imported
from these standalone mod packages.
"""
import json
from pathlib import Path, PurePosixPath
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "assets/new-horizons/Mage Guilds"
CONTENT = ROOT / "Mods/new-horizons/Content"
PACKAGES = (
	"castle-mage-guild-level5",
	"fortress-mage-guild-v9",
	"stronghold-mage-guild-ridge-swap",
)
FORTRESS_V8 = "fortress-mage-guild-v8"


def _member_path(name):
	if not name or "\\" in name or "\0" in name:
		raise ValueError(f"Unsafe archive path: {name}")

	parts = name.rstrip("/").split("/")
	if (not parts or any(part in ("", ".", "..") for part in parts)
			or ":" in parts[0]):
		raise ValueError(f"Unsafe archive path: {name}")

	path = PurePosixPath(name)
	if path.is_absolute() or ".." in path.parts:
		raise ValueError(f"Unsafe archive path: {name}")
	return path


def _content_root(archive):
	roots = set()
	for name in archive.namelist():
		path = _member_path(name)
		for index, part in enumerate(path.parts[:-1]):
			if (part.casefold() == "content"
					and path.parts[index + 1].casefold() in {"config", "data", "sprites"}):
				roots.add(path.parts[:index + 1])

	if len(roots) != 1:
		raise ValueError(f"Expected one package Content root, found {sorted(roots)}")
	return next(iter(roots))


def _runtime_assets(archive):
	content_root = _content_root(archive)
	for name in archive.namelist():
		path = _member_path(name)
		if name.endswith("/") or path.parts[:len(content_root)] != content_root:
			continue

		parts = path.parts[len(content_root):]
		if len(parts) < 2 or parts[0].casefold() not in {"data", "sprites"}:
			continue

		directory = parts[0].casefold()
		relative = Path(directory, *parts[1:])
		data = archive.read(name)
		if relative.suffix.casefold() == ".json":
			descriptor = json.loads(data)
			basepath = descriptor.get("basepath") if isinstance(descriptor, dict) else None
			if isinstance(basepath, str) and basepath:
				descriptor["basepath"] = basepath.replace("\\", "/").rstrip("/") + "/"
			data = (json.dumps(descriptor, indent=2) + "\n").encode()
		yield relative, data


def assets():
	seen = {}
	for package in PACKAGES:
		with ZipFile(SOURCES / (package + ".zip")) as archive:
			for relative, data in _runtime_assets(archive):
				key = relative.as_posix().casefold()
				if key in seen:
					if seen[key] != data:
						raise ValueError(f"Conflicting Mage Guild source asset: {relative}")
					continue
				seen[key] = data
				yield relative, data


def _expected_obsolete_fortress_paths():
	paths = {
		Path("sprites/TBFRMAG4.json"),
		Path("sprites/TBFRMAG5.json"),
		Path("sprites/HALLFORT/mage-guild-4.png"),
		Path("sprites/HALLFORT/mage-guild-5.png"),
	}
	for level in (4, 5):
		paths.update(Path(f"sprites/TBFRMAG{level}/{frame:02}.png") for frame in range(21))
	return paths


def _obsolete_fortress_assets():
	with ZipFile(SOURCES / (FORTRESS_V8 + ".zip")) as old_archive:
		old_assets = dict(_runtime_assets(old_archive))
	with ZipFile(SOURCES / "fortress-mage-guild-v9.zip") as new_archive:
		new_paths = {relative for relative, _ in _runtime_assets(new_archive)}

	obsolete = {relative: data for relative, data in old_assets.items() if relative not in new_paths}
	if set(obsolete) != _expected_obsolete_fortress_paths():
		raise ValueError("The Fortress v8-to-v9 asset delta changed; review cleanup targets before importing")
	return obsolete


def remove_obsolete_fortress_assets():
	obsolete = _obsolete_fortress_assets()
	to_remove = []

	for relative, expected in obsolete.items():
		target = CONTENT / relative
		if target.is_symlink():
			raise ValueError(f"Refusing to remove changed obsolete Fortress asset: {target}")
		if not target.exists():
			continue
		if not target.is_file() or target.read_bytes() != expected:
			raise ValueError(f"Refusing to remove changed obsolete Fortress asset: {target}")
		to_remove.append(target)

	for level in (4, 5):
		directory = CONTENT / "sprites" / f"TBFRMAG{level}"
		if not directory.exists():
			continue
		if directory.is_symlink() or not directory.is_dir():
			raise ValueError(f"Refusing to clean unexpected Fortress frame path: {directory}")
		allowed = {f"{frame:02}.png" for frame in range(21)}
		if any(path.name not in allowed or path.is_symlink() or not path.is_file()
				for path in directory.iterdir()):
			raise ValueError(f"Unexpected contents in obsolete Fortress frame directory: {directory}")

	for target in to_remove:
		target.unlink()

	for level in (4, 5):
		directory = CONTENT / "sprites" / f"TBFRMAG{level}"
		if directory.exists():
			directory.rmdir()

	return len(to_remove)


def import_assets():
	# Validate every source package and destination before removing obsolete files.
	prepared = list(assets())
	removed = remove_obsolete_fortress_assets()
	for relative, data in prepared:
		destination = CONTENT / relative
		destination.parent.mkdir(parents=True, exist_ok=True)
		destination.write_bytes(data)
	return len(prepared), removed


if __name__ == "__main__":
	count, removed = import_assets()
	print(f"Imported {count} runtime assets; removed {removed} obsolete Fortress v8 assets; faction gameplay rules unchanged.")
