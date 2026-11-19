"""Clean portable Windows layout and independent, system-only bootstrap audit."""
# SPDX-License-Identifier: GPL-2.0-or-later

from pathlib import Path
import re
import shutil
import zipfile


def audit_bootstrap(binary, run, system_dlls, tool):
    """Audit normal/delay imports and export forwarders without loading the PE."""
    headers = run(tool, '/nologo', '/headers', str(binary))
    if not re.search(r'\b8664 machine\b', headers, re.IGNORECASE):
        raise RuntimeError('Portable bootstrap must be an AMD64 PE')
    dependents = run(tool, '/nologo', '/dependents', str(binary))
    imports = set(re.findall(r'^\s+([\w.+-]+\.(?:dll|drv))\s*$', dependents,
                             re.MULTILINE | re.IGNORECASE))
    exports = run(tool, '/nologo', '/exports', str(binary))
    imports.update(name + '.dll' for name in re.findall(
        r'\(forwarded to ([\w-]+)\.', exports, re.IGNORECASE))
    # VC redistributables, SDL, VCMI and bundled DLLs may NEVER satisfy this gate.
    for name in imports:
        lower = name.lower()
        if lower not in system_dlls and not lower.startswith(('api-ms-win-', 'ext-ms-win-')):
            raise RuntimeError('Bootstrap imports non-system DLL: ' + name)
    return {name: 'Windows system/API contract' for name in sorted(imports)}


def assemble_portable_root(package, install, run, system_dlls, write_json):
    """Nest an already audited flat stage; do not change the installed engine."""
    package, install = Path(package), Path(install)
    bootstrap = install / 'bootstrap' / 'new-horizons.exe'
    if bootstrap.is_symlink() or not bootstrap.is_file():
        raise RuntimeError('Required installed portable bootstrap missing or linked')
    tool = shutil.which('dumpbin')
    if not tool:
        raise RuntimeError('dumpbin is required for the portable bootstrap audit')
    report = audit_bootstrap(bootstrap, run, system_dlls, tool)
    setup = install / 'bootstrap' / 'Setup.exe'
    setup_report = None
    if setup.exists() or setup.is_symlink():
        if setup.is_symlink() or not setup.is_file():
            raise RuntimeError('Installed Setup.exe must be an ordinary file')
        setup_report = audit_bootstrap(setup, run, system_dlls, tool)
    children = list(package.iterdir())
    if not children or any(path.name.lower() == 'runtime' for path in children):
        raise RuntimeError('Invalid flat runtime staging tree')
    if any(path.is_symlink() for path in package.rglob('*')):
        raise RuntimeError('Linked files are not permitted in portable packages')
    runtime = package / 'runtime'
    runtime.mkdir()
    for path in children:
        path.rename(runtime / path.name)
    shutil.copyfile(bootstrap, package / 'new-horizons.exe')
    write_json(runtime / 'BOOTSTRAP-IMPORTS.json', report)
    if setup_report is not None:
        shutil.copyfile(setup, package / 'Setup.exe')
        write_json(runtime / 'MANUAL-SETUP-IMPORTS.json', setup_report)
    assert_portable_layout(package)
    return runtime


def append_windows_path_budget(package, runtime):
    """Record classic Windows path budgets using the final payload inventory."""
    def units(path):
        return len(path.as_posix().encode('utf-16-le')) // 2

    # The checksum file is written after the README is finalized.
    members = [path.relative_to(package) for path in package.rglob('*') if path.is_file()]
    members.append((runtime / 'SHA256SUMS.txt').relative_to(package))
    longest = max(map(units, members))
    deepest = max(map(units, (path.parent for path in members)))
    file_limit, directory_limit = 259, 247
    initial = min(file_limit - 1 - longest, directory_limit - 1 - deepest)
    # Ready contents live beneath r/<24 hex>. This is five characters deeper
    # than .p-<16 hex>/p. Selection writes a 43-character temporary suffix.
    store = min(file_limit - max(28 + longest, 43), directory_limit - 28 - deepest)
    # The per-installation store adds NH/<16 hex> beneath LocalAppData.
    local_data = store - 20
    original_lookup = file_limit - 1 - units(Path('runtime/config/newHorizonsVersion.json'))
    report = f"""Windows path budget for this ZIP
Longest relative file path: {longest} UTF-16 units.
Maximum extracted package root for classic full ZIP extraction: {initial} UTF-16 units.
Existing installation lookup budget for the updater: {original_lookup} UTF-16 units.
Maximum managed update store path: {store} UTF-16 units.
Maximum LocalAppData path for this managed update layout: {local_data} UTF-16 units.
Lengths exclude any trailing separator. The existing-installation lookup
budget is separate from full ZIP extraction.
"""
    with (runtime / 'README-New-Horizons.txt').open('a', encoding='utf-8') as readme:
        readme.write('\n' + report)
    return report


def archive_portable_package(package, runtime, destination, sha256):
    """Finalize the checksummed payload and archive under the portable root name."""
    print(append_windows_path_budget(package, runtime), end='')
    manifest = runtime / 'SHA256SUMS.txt'
    members = sorted(path for path in package.rglob('*') if path != manifest and path.is_file())
    checksums = [f'{sha256(path)}  {path.relative_to(package).as_posix()}' for path in members]
    manifest.write_text('\n'.join(checksums) + '\n', encoding='utf-8')
    with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in sorted([*members, manifest]):
            archive.write(path, f'{destination.stem}/{path.relative_to(package).as_posix()}')


def assert_portable_layout(package, *, require_setup=False):
    package = Path(package)
    names = {path.name for path in package.iterdir()}
    expected = {'new-horizons.exe', 'runtime'}
    if require_setup or 'Setup.exe' in names:
        expected.add('Setup.exe')
    if names != expected:
        raise RuntimeError('Portable root must contain only new-horizons.exe, optional Setup.exe and runtime/')
    if 'Setup.exe' in names and ((package / 'Setup.exe').is_symlink() or not (package / 'Setup.exe').is_file()):
        raise RuntimeError('Manual setup entry must be a regular file')
    if (package / 'new-horizons.exe').is_symlink() or not (package / 'new-horizons.exe').is_file():
        raise RuntimeError('Portable entry point must be a regular file')
    runtime = package / 'runtime'
    if runtime.is_symlink() or not runtime.is_dir():
        raise RuntimeError('Portable runtime must be an ordinary directory')
    if any(path.is_symlink() for path in runtime.rglob('*')):
        raise RuntimeError('Linked runtime payload is unsupported')
    native = [path for path in runtime.rglob('*') if path.suffix.lower() in {'.exe', '.dll'}]
    if any(path.parent != runtime or not path.is_file() for path in native):
        raise RuntimeError('Native runtime files must stay in the audited flat PE directory')
    executables = [path.name for path in native if path.suffix.lower() == '.exe']
    if executables != ['new-horizons.exe']:
        raise RuntimeError('Unexpected executable in portable runtime')
    for name in ('new-horizons.exe', 'VCMI_lib.dll', 'Start-New-Horizons.ps1', 'Launch-Process.ps1', 'Setup-Progress.ps1'):
        if not (runtime / name).is_file():
            raise RuntimeError('Incomplete portable runtime: ' + name)


def stage_frontend(root, helpers, package):
    """Stage the launcher, setup helpers and project license notices."""
    for filename in ('Play-New-Horizons.cmd', 'Start-New-Horizons.ps1', 'Launch-Process.ps1',
                     'Setup-Progress.ps1', 'Complete-Assets.ps1', 'Player-Launcher.ps1',
                     'Player-Launcher-Operation.ps1',
                     'Portable-Release-Launcher.ps1', 'Prepare-NHReleaseUpdate.ps1',
                     'README-New-Horizons.txt', 'dirs.json'):
        source = helpers / filename
        destination = package / (Path('config/dirs.json') if filename == 'dirs.json' else filename)
        if destination.exists() and filename != 'dirs.json':
            raise RuntimeError('Frontend helper collides with package file: ' + destination.name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    for filename in ('license.txt', 'AUTHORS.h'):
        shutil.copyfile(root / filename, package / filename)
    embedded = package / 'licenses' / 'xBRZ'
    embedded.mkdir(parents=True)
    shutil.copyfile(root / 'client/xBRZ/License.txt', embedded / 'License.txt')
    xbrz = (root / 'client/xBRZ/xbrz.cpp').read_text(encoding='utf-8')
    (embedded / 'ATTRIBUTION.txt').write_text(xbrz.split('#include "xbrz.h"', 1)[0], encoding='utf-8')
