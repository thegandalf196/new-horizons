#!/usr/bin/env python3
"""Canonical LOCAL PRIVATE preview: reviewed engine + explicitly pinned school art.

Never use this recipe for public releases. No client, loader or GUI is executed.
Engine-only stages are diagnostic inputs, not latest player previews.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import tarfile
import zipfile

SCHOOLS = ('sorcery', 'light', 'nature', 'havoc', 'shadow', 'chaos')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    if any(p.is_symlink() for p in root.rglob('*')):
        raise ValueError('Linked input is not permitted')
    return {p.relative_to(root).as_posix(): digest(p)
            for p in sorted(root.rglob('*')) if p.is_file()}


def write_json(path, value):
    path.write_text(json.dumps(value, sort_keys=True, indent=2) + '\n')


def validate_art(images, manifest, approved_digest):
    if digest(manifest) != approved_digest:
        raise ValueError('Approved art manifest digest mismatch')
    files = json.loads(manifest.read_text())['files']
    descriptors = {f'NH_{s}_{kind}.json' for s in SCHOOLS
                   for kind in ('bookmark', 'button', 'spellBorders')}
    pngs = {n for n in files if n.endswith('.png')}
    if len(pngs) != 144 or set(files) != pngs | descriptors:
        raise ValueError('Expected exactly 144 school PNGs and 18 descriptors')
    for school in SCHOOLS:
        if sum(n.startswith('NH_' + school) for n in pngs) != 24:
            raise ValueError('Incomplete school: ' + school)
    for name, expected in files.items():
        if Path(name).name != name or '\\' in name:
            raise ValueError('Unsafe art filename')
        p = images / name
        if p.is_symlink() or not p.is_file() or digest(p) != expected:
            raise ValueError('Missing or changed approved art: ' + name)
        if name in pngs and not p.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'):
            raise ValueError('Invalid PNG: ' + name)
    for name in descriptors:
        frames = json.loads((images / name).read_text())['images']
        if not frames or any(f['file'] not in pngs for f in frames):
            raise ValueError('Unbound descriptor: ' + name)
    return files


def package(engine, images, manifest, approved_digest, source, platform, output):
    destination = output.resolve()
    for protected in (engine, images, manifest):
        resolved = protected.resolve()
        if destination == resolved or destination.is_relative_to(resolved) or resolved.is_relative_to(destination):
            raise ValueError('Output overlaps protected input')
    launchers = ('Play-New-Horizons.sh', 'new-horizons-launch.sh') if platform == 'linux' else (
        'Play-New-Horizons.cmd', 'Start-New-Horizons.ps1', 'config/dirs.json')
    for name in launchers:
        p = engine / name
        if not p.is_file() or p.is_symlink() or not p.stat().st_size:
            raise ValueError('Missing required launcher: ' + name)
        if platform == 'linux' and not p.stat().st_mode & 0o111:
            raise ValueError('Launcher is not executable: ' + name)
    files = validate_art(images, manifest, approved_digest)
    before = inventory(engine)
    identity = json.loads((engine / 'BUILD-IDENTITY.json').read_text())
    if identity.get('source', identity.get('source_commit')) != source:
        raise ValueError('Engine source identity mismatch')
    sums = 'SHA256SUMS' if platform == 'linux' else 'SHA256SUMS.txt'
    expected = {}
    for line in (engine / sums).read_text().splitlines():
        h, name = line.split(maxsplit=1)
        if name in expected:
            raise ValueError('Duplicate engine checksum')
        expected[name] = h
    if expected != {n: h for n, h in before.items() if n != sums}:
        raise ValueError('Engine inventory/checksum mismatch')
    binaries = {n: h for n, h in before.items()
                if n in ('vcmiclient', 'libvcmi.so') or n.lower().endswith(('.dll', '.exe'))}
    if not binaries or (platform == 'linux' and not {'vcmiclient', 'libvcmi.so'} <= binaries.keys()):
        raise ValueError('Missing engine binaries')
    if platform == 'windows' and 'VCMI_client.exe' not in binaries:
        raise ValueError('Missing Windows client')
    output.mkdir(parents=True, exist_ok=False)
    stage = output / 'New-Horizons-Private-Preview'
    shutil.copytree(engine, stage)
    for name in files:
        shutil.copyfile(images / name, stage / 'Mods/new-horizons/Images' / name)
    mod_path = stage / 'Mods/new-horizons/mod.json'
    mod = json.loads(mod_path.read_text())
    for school in SCHOOLS:
        mod['spellSchools'][school]['schoolBorders'] = f'NH_{school}_spellBorders.json'
    write_json(mod_path, mod)
    shutil.copyfile(manifest, stage / 'SIX-SCHOOL-ASSETS.json')
    profile = 'new-horizons-private-' + source[:12] + '-' + approved_digest[:8]
    if platform == 'linux':
        (stage / 'Play-Preview.sh').write_text(
            '#!/usr/bin/env bash\nset -euo pipefail\n'
            'root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)\n'
            'exec "$root/Play-New-Horizons.sh" --profile "$HOME/.local/share/'
            + profile + '" "$@"\n')
        (stage / 'Play-Preview.sh').chmod(0o755)
    else:
        for name in ('Start-New-Horizons.ps1', 'config/dirs.json'):
            p = stage / name
            text = p.read_text()
            if 'HeroesIII-NewHorizons' not in text:
                raise ValueError('Unrecognized Windows profile helper')
            p.write_text(text.replace('HeroesIII-NewHorizons', profile))
    (stage / 'PRIVATE-PREVIEW.txt').write_text(
        'CANONICAL PRIVATE PLAYER PREVIEW — NOT FOR PUBLIC REDISTRIBUTION\n'
        'Reviewed engine plus pinned 144 school PNGs/18 descriptors and six border bindings.\n'
        'School artwork includes template/game-derived material; CC0 mod wording does not\n'
        'license it. Preserve private provenance and original engine/source notices.\n'
        '114 PNG consumer mappings;30 provisioned PNGs remain unbound.\n'
        'Orders placeholder remains: no new gauntlet artwork commissioned.\n'
        'No GUI, input, save, native test or full-design acceptance implied.\n'
        'Use Play-Preview.sh --assets DIR on Linux or Play-New-Horizons.cmd on Windows.\n'
        'Fresh profile: ' + profile + '\n')
    (stage / 'README-New-Horizons.txt').write_text((stage / 'PRIVATE-PREVIEW.txt').read_text())
    identity.pop('proprietary_assets_included', None)
    identity.update(private_preview_recipe=1, recipe_sha256=digest(Path(__file__)),
                    art_manifest_sha256=approved_digest,
                    engine_identity_sha256=before['BUILD-IDENTITY.json'],
                    preview_platform=platform, preview_profile=profile,
                    binary_sha256=binaries, engine_rebuilt=False,
                    private_only=True, approved_art_files=files)
    write_json(stage / 'BUILD-IDENTITY.json', identity)
    after = inventory(stage)
    if any(after['Mods/new-horizons/Images/' + n] != h for n, h in files.items()):
        raise ValueError('Approved overlay changed during packaging')
    if any(after[n] != h for n, h in binaries.items()):
        raise ValueError('Engine binary changed during packaging')
    if inventory(engine) != before:
        raise ValueError('Frozen engine changed during packaging')
    (stage / sums).write_text(''.join(h + '  ' + n + '\n'
                                     for n, h in after.items() if n != sums))
    after = inventory(stage)
    archive = output / ('New-Horizons-Private-' + platform + '.tar.gz' if platform == 'linux'
                        else 'New-Horizons-Private-windows.zip')
    if platform == 'linux':
        with archive.open('wb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', filename='', mtime=0) as gz:
            with tarfile.open(fileobj=gz, mode='w') as tar:
                for name in after:
                    p = stage / name
                    info = tar.gettarinfo(str(p), arcname=stage.name + '/' + name)
                    info.uid = info.gid = info.mtime = 0
                    info.uname = info.gname = ''
                    with p.open('rb') as stream:
                        tar.addfile(info, stream)
    else:
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as z:
            for name in after:
                info = zipfile.ZipInfo(stage.name + '/' + name, (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                z.writestr(info, (stage / name).read_bytes())
    result = dict(source=source, platform=platform, art_manifest_sha256=approved_digest,
                  archive=archive.name, archive_sha256=digest(archive),
                  engine_binaries=binaries, private_only=True)
    write_json(output / 'result.json', result)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('engine', 'images', 'manifest', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--approved-digest', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--platform', choices=('linux', 'windows'), required=True)
    print(json.dumps(package(**vars(parser.parse_args())), indent=2))
