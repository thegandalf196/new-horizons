"""Failure-only vendor evidence; never grants package admission or runs a build.

Only sanitized comparisons and recipient-encrypted originals leave this process.
Installed v142 inventory is not proof of CMake selection or final package bytes.
"""
import argparse
import difflib
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

from windows_crt_provenance import file_version


BASELINE_URL = ('https://github.com/thegandalf196/new-horizons/releases/download/'
                'v0.92.3/New-Horizons-Windows-x64-3d95ee2022b0.zip')
BASELINE_SHA = '72f8e1c912485238c9279df73fb64b01e05c5acf3060d415081a19cf29647de2'
PREFIX = 'New-Horizons-Windows-x64-3d95ee2022b0/runtime/'
CERTIFICATE_SHA = 'ea0e74f613248796f99ee86dd23d98ce9e17860e442575996033bc7542792cb1'
RECIPIENT_CERTIFICATE = '''-----BEGIN CERTIFICATE-----
MIIERTCCAq2gAwIBAgIUB4ba2y9flqsRq6TpbZTTNggLNEswDQYJKoZIhvcNAQEL
BQAwMjEwMC4GA1UEAwwnTmV3IEhvcml6b25zIFdpbmRvd3MgZXZpZGVuY2UgcmVj
aXBpZW50MB4XDTI2MTExOTE1NDEyNloXDTI2MTEyMjE1NDEyNlowMjEwMC4GA1UE
AwwnTmV3IEhvcml6b25zIFdpbmRvd3MgZXZpZGVuY2UgcmVjaXBpZW50MIIBojAN
BgkqhkiG9w0BAQEFAAOCAY8AMIIBigKCAYEAwsu5ckWgRNVNpszlv4kUlD0eSy7u
V2RX3IvnwP8FptS9Q2vYsy1LOzalpCLu1xdaf3oFEmen8qks66fEDW7YmXuNByCj
ZEb7+KhpKKCycHbwd9Q11e3W3RnL9G/x9dOgElmnFZ8O6Ew/B5kv0+hsVOeI8Ynz
YKXcGIFCyw8fkvRbiwrjzNyanOgzkjRdwPd8bu+WkZRC84uztNQVSIZ/bPEW/l+6
kgDna6nWlUDS+5AXrriosj5KVe0ShlRrHFzLcdjwt5K1eSE+0wVEJFqSEJssrmmJ
LNckJcocOOekyj+PRQFwz0jMra+tZEOUFfu18k7pD0DR0IGHpBfDxP5O3eSUiEiw
gBGjj8SJW8JpBOyTKiyNCwAlM7mOXUboMNDhqU0hkJ68rN6m+lpiSGViCvjh7hOM
j1dhwDHHtdwaZRWlj8cuBseikOhhvY9L7fDVq4AKp3aycZjXpQ+PmBKLEAmlgmd2
xKmngHGnUqlmhfwiP092NMIx6TSeeYkDi+KrAgMBAAGjUzBRMB0GA1UdDgQWBBSV
oIVubo4K/7m9QC8HzG+jgWieNTAfBgNVHSMEGDAWgBSVoIVubo4K/7m9QC8HzG+j
gWieNTAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4IBgQAoLvyO7sjz
aN3uZYEiYKoIgajajIVlLcV0QiQpCqeKw/6yjZhYmyS/Ua3W4WDuRWH30J4Sniz7
Cb7uhvb+xMvaN3famF0x+7m8IObTmAZz5ucI9+VzI8Wkp11hcVfmk2CveqMu7VN8
zt3VxD3taFKaupqkyg2MjsWCa3pl3Xwj/nI4EJUPHMtjODuorT00l5QPmwhZehv1
qlOAN99eMppjuyHtZ7kT56WKCH2cIZntqAMxkiqjjdAdsV7dqym4MbE0XeKPzPHm
DnfZUkDELQsZe2QFDgCVxUQ0V5CRgkPZkGujv8TKV17hnAUe0cYWwlhm+MzFULQ1
KBCJjJoXarWUuBCn6ihKmFO5SLQiesuyNK5fUdk1I7Q9b/Af7VSmW0/zFAyesgPI
agt2QMDer0g+5yM2hXYLlGQjJ12nS+emTj1ew/+hWH679yxxc9dgM4yo6JlmXpvW
friQaGwryBqCgUad03jKpNK3/dO58BTgGZiSVAYrbfsu2JfJhcQL5ZA=
-----END CERTIFICATE-----
'''
TERM_NAMES = (
    'Visual-C-V14-License-Redistributable_and_Runtime_ENU.docx',
    'Visual_Studio_2026-License-Enterprise_Professional_ENU.docx',
)
RUNTIME_NAMES = ('msvcp140.dll', 'msvcp140_atomic_wait.dll',
                 'vcruntime140.dll', 'vcruntime140_1.dll')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def download(url, limit, referer=None):
    headers = {'User-Agent': 'Mozilla/5.0'}
    if referer:
        headers['Referer'] = referer
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read(limit + 1)
        if not response.url.startswith('https://'):
            raise RuntimeError('Non-HTTPS vendor evidence redirect')
    if len(data) > limit:
        raise RuntimeError('Vendor evidence exceeds size limit')
    return data


def text(data, name):
    if name.endswith('.docx'):
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            document = ET.fromstring(archive.read('word/document.xml'))
        ns = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'}
        return '\n'.join(''.join(p.itertext()) for p in document.findall('.//w:p', ns))
    if data.startswith((b'\xff\xfe', b'\xfe\xff')):
        return data.decode('utf-16')
    try:
        return data.decode('utf-8-sig')
    except UnicodeDecodeError:
        return data.decode('cp1252')


def safe_version(value):
    return value if re.fullmatch(r'\d+(?:\.\d+){1,3}', value) else 'unrecognized'


def installation_metadata(vs):
    locator = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    result = subprocess.run([str(locator), '-latest', '-products', '*', '-format', 'json'],
                            check=True, capture_output=True, timeout=30)
    entries = json.loads(result.stdout.decode('utf-8-sig'))
    if len(entries) != 1 or Path(entries[0]['installationPath']).resolve() != vs:
        raise RuntimeError('Visual Studio evidence installation mismatch')
    entry = entries[0]
    channel = entry.get('channelId', '')
    stable = (entry.get('isPrerelease') is False and channel == 'VisualStudio.18.Release'
              and entry.get('isComplete') is True)
    return {'installation_version': safe_version(entry.get('installationVersion', '')),
            'channel': channel if channel == 'VisualStudio.18.Release' else 'not-approved-stable',
            'stable_installation_verified': stable,
            'is_prerelease': entry.get('isPrerelease') is not False,
            'toolset_version': safe_version(os.environ.get('VCToolsVersion', '').strip()),
            'environment_redist_version': safe_version(Path(os.environ['VCToolsRedistDir']).name)}


def compare_notice(name, current, baseline, sealed, origin):
    if current is not None and len(current) > 256 * 1024:
        raise RuntimeError('Notice exceeds bounded allowlist size')
    old_text = text(baseline, name) if baseline is not None else ''
    new_text = text(current, name) if current is not None else ''
    if current is not None:
        sealed.writestr('current/' + name, current)
    if baseline is not None:
        sealed.writestr('baseline/' + name, baseline)
    sealed.writestr('text/' + name + '.txt', new_text)
    difference = ''.join(difflib.unified_diff(old_text.splitlines(True), new_text.splitlines(True),
                                             fromfile='baseline/' + name, tofile='current/' + name))
    sealed.writestr('diff/' + name + '.diff', difference)
    return {'file': name, 'origin': origin, 'sha256': digest(current) if current is not None else None,
            'baseline_sha256': digest(baseline) if baseline is not None else None,
            'bytes_equal': baseline == current, 'text_equal': old_text == new_text}


def bounded_read(path, root):
    resolved = path.resolve(strict=True)
    if not resolved.is_relative_to(root) or not resolved.is_file():
        raise RuntimeError('Vendor evidence source escapes allowlisted root')
    with resolved.open('rb') as stream:
        data = stream.read(256 * 1024 + 1)
    if len(data) > 256 * 1024:
        raise RuntimeError('Installed notice too large')
    return data


def collect_notices(baseline, vs, sdk, sealed):
    records = []
    landings = ('vs2026-ga-visualcpp-v14-redist-runtime', 'vs2026-ga-pro-enterprise')
    for name, landing in zip(TERM_NAMES, landings):
        current = download('https://visualstudio.microsoft.com/wp-content/uploads/2025/10/'
                           + name + '?download=1', 256 * 1024,
                           'https://visualstudio.microsoft.com/license-terms/' + landing + '/')
        records.append(compare_notice(name, current, baseline.read(PREFIX + 'licenses/Microsoft/' + name),
                                      sealed, 'official-license-document-download'))
    current = bounded_read(vs / 'Licenses/1033/Redist.txt', vs)
    records.append(compare_notice('VS2026-Redist.txt', current,
                                  baseline.read(PREFIX + 'licenses/Microsoft/VS2026-Redist.txt'),
                                  sealed, 'installed-vs-notice'))
    current_sdk = {}
    for source in sorted((sdk / 'Licenses').glob('*/sdk_license.rtf')):
        if safe_version(source.parent.name) == 'unrecognized':
            raise RuntimeError('Unknown installed SDK notice version')
        current_sdk['SDK-' + source.parent.name + '-license.rtf'] = source
    prefix = PREFIX + 'licenses/Microsoft/'
    previous = {member[len(prefix):] for member in baseline.namelist()
                if member.startswith(prefix) and re.fullmatch(r'SDK-\d+(?:\.\d+){1,3}-license\.rtf', member[len(prefix):])}
    for name in sorted(current_sdk.keys() | previous):
        current = bounded_read(current_sdk[name], sdk) if name in current_sdk else None
        old = baseline.read(prefix + name) if name in previous else None
        records.append(compare_notice(name, current, old, sealed, 'installed-sdk-notice-set'))
    return records


def runtime_inventory(baseline, vs):
    provenance = json.loads(baseline.read(PREFIX + 'licenses/Microsoft/PROVENANCE.json'))
    provider = provenance['cmake_selected_redist_directory']
    if provider != 'VC/Redist/MSVC/14.29.30133':
        raise RuntimeError('Baseline v142 provider differs from reviewed receipt')
    records = []
    for name in RUNTIME_NAMES:
        source = (vs / provider / 'x64/Microsoft.VC142.CRT' / name).resolve(strict=True)
        if not source.is_relative_to(vs):
            raise RuntimeError('Runtime inventory escapes installed provider')
        current = source.read_bytes()
        old = baseline.read(PREFIX + name)
        records.append({'file': name, 'sha256': digest(current), 'baseline_sha256': digest(old),
                        'bytes_equal': current == old, 'file_version': file_version(source),
                        'provider_relative_source': PurePosixPath(provider, 'x64/Microsoft.VC142.CRT', name).as_posix()})
    return {'scope': 'Installed prior v142 provider inventory only; no CMake configure or final-package proof',
            'provider': provider, 'runtimes': records}


def encrypt_archive(archive, output, temporary):
    openssl = shutil.which('openssl')
    if not openssl:
        raise RuntimeError('OpenSSL required for sealed vendor evidence')
    certificate = temporary / 'recipient.pem'
    certificate.write_text(RECIPIENT_CERTIFICATE, encoding='ascii')
    der = subprocess.run([openssl, 'x509', '-in', str(certificate), '-outform', 'DER'],
                         check=True, capture_output=True, timeout=30).stdout
    if digest(der) != CERTIFICATE_SHA:
        raise RuntimeError('Evidence recipient certificate mismatch')
    subprocess.run([openssl, 'cms', '-encrypt', '-aes-256-cbc', '-binary', '-in', str(archive),
                    '-outform', 'DER', '-out', str(output), str(certificate)],
                   check=True, capture_output=True, timeout=60)


def collect(output):
    output.mkdir(parents=True, exist_ok=False)
    receipt = {'scope': 'Failure-only evidence; unchanged Microsoft admission failed; no build permitted',
               'baseline_url': BASELINE_URL, 'baseline_sha256': BASELINE_SHA,
               'recipient_certificate_sha256': CERTIFICATE_SHA}
    with tempfile.TemporaryDirectory(prefix='nh-vendor-evidence-') as directory:
        temporary = Path(directory)
        data = download(BASELINE_URL, 80 * 1024 * 1024)
        if digest(data) != BASELINE_SHA:
            raise RuntimeError('Baseline public release digest mismatch')
        vs = Path(os.environ['VSINSTALLDIR']).resolve(strict=True)
        sdk = Path(os.environ['WindowsSdkDir']).resolve(strict=True)
        receipt['installation'] = installation_metadata(vs)
        archive = temporary / 'vendor-evidence.zip'
        with zipfile.ZipFile(io.BytesIO(data)) as baseline, zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as sealed:
            receipt['notices'] = collect_notices(baseline, vs, sdk, sealed)
            receipt['runtime_inventory'] = runtime_inventory(baseline, vs)
        encrypted = output / 'vendor-evidence.cms'
        encrypt_archive(archive, encrypted, temporary)
        receipt['encrypted_archive_sha256'] = digest(encrypted.read_bytes())
    receipt['owner_review_required'] = (not receipt['installation']['stable_installation_verified']
        or any(not item['bytes_equal'] for item in receipt['notices'])
        or any(not item['bytes_equal'] for item in receipt['runtime_inventory']['runtimes']))
    (output / 'comparison.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        collect(args.output)
    except Exception as error:
        # Exception messages may contain proprietary notice text or personal paths.
        print('Vendor evidence collection failed: ' + type(error).__name__)
        return 1
    print('Sealed vendor evidence collected; Microsoft admission remains failed.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
