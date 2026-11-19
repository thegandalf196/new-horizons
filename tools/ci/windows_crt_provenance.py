"""Bind shipped CRT bytes to CMake's selected installed redistributables."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


REVIEWED_ENVIRONMENT = '14.52.37025'
REVIEWED_PROVIDER = 'VC/Redist/MSVC/14.29.30133'
REVIEWED_RUNTIME_VERSION = '14.29.30157.0'
# These exact files match the previously shipped v0.92.3 package and the stable
# VS 18.12 runner inspected before widening notice-environment admission.
REVIEWED_NOTICE_SHA256 = {
    'Visual-C-V14-License-Redistributable_and_Runtime_ENU.docx':
        '08651651a7602fc7c0e2763de0fde1ff9f868df2780597cd1775ee9d6441c783',
    'Visual_Studio_2026-License-Enterprise_Professional_ENU.docx':
        'dd2ab92a7c2bb90dd509e16572707a34b13bc0bc09b9199d115f25594ac39053',
    'VS2026-Redist.txt': '2d69e3b0ac782d5513b503bafdf269a93c4fec0f1f8efac82bfbfa4b6296aef5',
    'SDK-10.0.26100.0-license.rtf': '0f4a26ac9dc50066f8a1bfeaaf3f092d1b9e4791df5487b9f3c31e7c3dc4d7f5',
}
REVIEWED_RUNTIME_SHA256 = {
    'msvcp140.dll': '75872c426555ac8af55b5a7f6d4aea8831af74353b22612850a2bfc40e3b67e1',
    'msvcp140_atomic_wait.dll': 'ae430f3b65c5fc88643b30a344289bfda29811279379c1537cec19983c0938ae',
    'vcruntime140.dll': '270ba17bf89cc2eb6e6ce536eb78e4614ab5499f653ab7e07fe9e3d33bf3b6e3',
    'vcruntime140_1.dll': 'f3754fc6ec87bcf84e05f3287b5de7bd305a30a0e1db7d478cb6353600682784',
}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def file_version(path):
    import pefile
    with pefile.PE(str(path)) as image:
        info = getattr(image, 'VS_FIXEDFILEINFO', [])
        if len(info) != 1:
            raise RuntimeError('CRT file version is missing or ambiguous')
        if image.FILE_HEADER.Machine != 0x8664:
            raise RuntimeError('CRT must be AMD64')
        item = info[0]
        return '.'.join(str(x) for x in (item.FileVersionMS >> 16,
                       item.FileVersionMS & 65535, item.FileVersionLS >> 16,
                       item.FileVersionLS & 65535))


def review_notice_environment(visual_studio, redist):
    """Keep historical admission and admit the single reviewed stable successor."""
    if visual_studio.parent.name != '18' or visual_studio.name not in {'Enterprise', 'Professional'}:
        raise RuntimeError('Microsoft notice applicability requires VS 2026 Enterprise/Professional')
    if re.fullmatch(r'14\.51\.\d+', redist.name):
        return False
    if redist.name != REVIEWED_ENVIRONMENT:
        raise RuntimeError('Review Microsoft runtime version and terms before publishing this toolchain')
    installed = visual_studio.resolve(strict=True)
    if redist.resolve(strict=True) != installed / 'VC' / 'Redist' / 'MSVC' / REVIEWED_ENVIRONMENT:
        raise RuntimeError('Reviewed notice provider is outside the selected Visual Studio installation')
    locator = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    entries = json.loads(subprocess.check_output(
        [str(locator), '-all', '-products', '*', '-format', 'json', '-utf8'],
        encoding='utf-8-sig', timeout=30))
    matching = [entry for entry in entries if Path(entry['installationPath']).resolve() == installed]
    if len(matching) != 1 or matching[0].get('isPrerelease') is not False or not (
            matching[0].get('isComplete') is True and matching[0].get('channelId') == 'VisualStudio.18.Release'):
        raise RuntimeError('Reviewed Microsoft notice provider requires the stable VS 18 release channel')
    if os.environ.get('VCToolsVersion', '').strip('\\/') != '14.29.30133':
        raise RuntimeError('Reviewed Microsoft notice provider requires the pinned v142 toolset')
    return True


def verify_notice_pins(directory):
    """Verify the complete reviewed retained notice set before package admission."""
    sdk = {path.name for path in directory.glob('SDK-*-license.rtf')}
    if sdk != {'SDK-10.0.26100.0-license.rtf'}:
        raise RuntimeError('Installed SDK notice set differs from reviewed notices')
    for name, expected in REVIEWED_NOTICE_SHA256.items():
        path = directory / name
        if path.is_symlink() or not path.is_file() or sha256(path) != expected:
            raise RuntimeError('Retained Microsoft notice differs from reviewed bytes: ' + name)


def retain_installed_notices(visual_studio, sdk, destination, reviewed):
    """Copy installed redistribution notices after checking the reviewed source set."""
    sources = {'VS2026-Redist.txt': visual_studio / 'Licenses/1033/Redist.txt'}
    sdk_terms = sorted((sdk / 'Licenses').glob('*/sdk_license.rtf'))
    if not sdk_terms:
        raise RuntimeError('Installed Windows SDK license missing')
    sources.update({'SDK-' + source.parent.name + '-license.rtf': source for source in sdk_terms})
    if reviewed and sources.keys() != {'VS2026-Redist.txt', 'SDK-10.0.26100.0-license.rtf'}:
        raise RuntimeError('Installed SDK notice set differs from reviewed notices')
    for name, source in sources.items():
        if not source.is_file():
            raise RuntimeError('Installed Visual Studio redistribution-list notice missing')
        if reviewed and (source.is_symlink() or sha256(source) != REVIEWED_NOTICE_SHA256[name]):
            raise RuntimeError('Installed Microsoft notice differs from reviewed bytes: ' + name)
        shutil.copyfile(source, destination / name)
    if reviewed:
        verify_notice_pins(destination)
    return [{'file': name, 'sha256': sha256(destination / name)} for name in sorted(sources)]


def verify_reviewed_runtime_sources(visual_studio, selected):
    if selected.relative_to(visual_studio).as_posix() != REVIEWED_PROVIDER:
        raise RuntimeError('Reviewed runtime requires the pinned CMake-selected v142 provider')
    for name, expected in REVIEWED_RUNTIME_SHA256.items():
        source = selected / 'x64/Microsoft.VC142.CRT' / name
        if source.is_symlink() or not source.is_file() or sha256(source) != expected:
            raise RuntimeError('Selected runtime differs from reviewed bytes: ' + name)
        if file_version(source) != REVIEWED_RUNTIME_VERSION:
            raise RuntimeError('Selected runtime version differs from reviewed v142 version')


def review_selected_runtime(visual_studio, selected, recorded_environment=None):
    redist = Path(os.environ.get('VCToolsRedistDir', '__missing__'))
    if REVIEWED_ENVIRONMENT not in {redist.name, recorded_environment}:
        return False
    if recorded_environment is not None and recorded_environment != REVIEWED_ENVIRONMENT:
        raise RuntimeError('Retained notice environment differs from the current toolchain')
    if redist.name != REVIEWED_ENVIRONMENT or not review_notice_environment(visual_studio, redist):
        raise RuntimeError('Retained notice environment differs from the current toolchain')
    verify_reviewed_runtime_sources(visual_studio, selected)
    return True


def selected_redist(build_directory, visual_studio):
    cache = (build_directory / 'CMakeCache.txt').read_text(encoding='utf-8')
    roots = re.findall(r'^MSVC_REDIST_DIR:[^=]+=(.+)$', cache, re.MULTILINE)
    if len(roots) != 1:
        raise RuntimeError('CMake selected CRT directory is missing or ambiguous')
    vs = visual_studio.resolve(strict=True)
    selected = Path(roots[0].strip()).resolve(strict=True)
    allowed = (vs / 'VC/Redist/MSVC').resolve(strict=True)
    if selected == allowed or not selected.is_relative_to(allowed):
        raise RuntimeError('CMake selected CRT directory is outside the VS redist tree')
    return vs, selected


def preflight_runtime(build_directory, visual_studio):
    """Inspect selected redistributables before compilation; return metadata only."""
    vs, selected = selected_redist(build_directory, visual_studio)
    reviewed = review_selected_runtime(vs, selected)
    records = []
    for source in sorted((selected / 'x64').glob('Microsoft.VC*.CRT/*.dll')):
        if not source.name.lower().startswith(('msvcp', 'vcruntime', 'concrt')):
            continue
        resolved = source.resolve(strict=True)
        if not resolved.is_relative_to(selected):
            raise RuntimeError('CRT source escapes selected redistributable directory')
        version = file_version(resolved)
        if not version.startswith('14.'):
            raise RuntimeError('Unreviewed CRT family requires notice review')
        records.append({'file': source.name, 'sha256': sha256(resolved),
                        'file_version': version, 'vs_relative_source': resolved.relative_to(vs).as_posix()})
    if not records:
        raise RuntimeError('No selected CRT sources found before compilation')
    return {'scope': 'Selected-source preflight, not final-package binding or historical origin proof',
            'cmake_selected_redist_directory': selected.relative_to(vs).as_posix(), 'runtimes': records,
            'reviewed_v142_runtime_pins_verified': reviewed}


def bind_runtime(package, build_directory, visual_studio):
    """Fail closed unless shipped CRT bytes match CMake-selected sources and terms.

    This is a final-package gate, not a notice-only preflight. No absolute source
    paths enter the distributed provenance; origins are relative to VSINSTALLDIR.
    """
    vs, selected = selected_redist(build_directory, visual_studio)
    path = package / 'licenses/Microsoft/PROVENANCE.json'
    provenance = json.loads(path.read_text(encoding='utf-8'))
    reviewed = review_selected_runtime(vs, selected, provenance.get('notice_environment_redist_directory_version', ''))
    binaries = sorted(p for p in package.glob('*.dll')
                      if p.name.lower().startswith(('msvcp', 'vcruntime', 'concrt')))
    if not binaries:
        raise RuntimeError('No shipped MSVC runtime found for provenance binding')
    if reviewed:
        if {binary.name for binary in binaries} != REVIEWED_RUNTIME_SHA256.keys():
            raise RuntimeError('Shipped runtime set differs from reviewed v142 runtime files')
        verify_notice_pins(path.parent)
    records = []
    for binary in binaries:
        digest = sha256(binary)
        sources = []
        for folder in sorted((selected / 'x64').glob('Microsoft.VC*.CRT')):
            source = folder / binary.name
            if source.is_file():
                resolved = source.resolve(strict=True)
                if not resolved.is_relative_to(selected):
                    raise RuntimeError('CRT source escapes selected redistributable directory')
                if sha256(resolved) == digest:
                    sources.append(resolved.relative_to(vs).as_posix())
        if not sources:
            raise RuntimeError('Shipped CRT does not match selected redistributable bytes')
        version = file_version(binary)
        if reviewed and (digest != REVIEWED_RUNTIME_SHA256[binary.name] or version != REVIEWED_RUNTIME_VERSION):
            raise RuntimeError('Shipped runtime differs from reviewed v142 bytes or version')
        if not version.startswith('14.'):
            raise RuntimeError('Unreviewed CRT family requires notice review')
        records.append({'file': binary.name, 'sha256': digest,
                        'file_version': version, 'matching_vs_relative_sources': sources})
    if not provenance.get('terms'):
        raise RuntimeError('CRT binding requires retained Microsoft terms')
    for term in provenance['terms']:
        name = term.get('file', '')
        if not name or Path(name).name != name:
            raise RuntimeError('Invalid retained Microsoft terms filename')
        retained = path.parent / name
        if not retained.is_file() or retained.is_symlink() or sha256(retained) != term.get('sha256'):
            raise RuntimeError('Retained Microsoft terms do not match provenance')
    provenance['cmake_selected_redist_directory'] = selected.relative_to(vs).as_posix()
    provenance['runtime_bindings'] = records
    provenance['runtime_binding_status'] = 'verified-byte-identical-selected-redist'
    path.write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    return records
