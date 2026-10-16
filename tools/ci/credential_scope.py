# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit credential policy and exact pinned original-path applicability."""
from pathlib import PurePosixPath
import hashlib
import re
from scanner_runtime import ScannerError
import credential_budget as budget
import credential_disposition as disposition

PIN = '83d9cd684c87d95d656c1458ef04895a7f1cbd8e'
# Review against pinned defaults, not arbitrary runtime/project configuration.
PREDICATES = {
    'freemius-secret-key': r'(?i)\.php$',
    'hashicorp-tf-password': r'(?i)\.(?:tf|hcl)$',
    'kubernetes-secret-yaml': r'(?i)\.ya?ml$',
    'nuget-config-password': r'(?i)nuget\.config$',
    'pkcs12-file': r'(?i)(?:^|\/)[^\/]+\.p(?:12|fx)$',
}
SUFFIX = {(): '.txt', ('freemius-secret-key',): '.php',
          ('hashicorp-tf-password',): '.tf', ('kubernetes-secret-yaml',): '.yaml',
          ('nuget-config-password',): '.nuget.config'}
EXCLUDED_SUFFIXES = frozenset('png jpg jpeg gif bmp tiff ico icns psd webp svg nhart lod pac snd vid mp3 ogg wav flac mp4 avi webm zip jar tar gz bz2 xz zst 7z rar exe dll so dylib a o obj pdb pdf doc docx xls xlsx ppt pptx ttf otf woff woff2 h3m h3c vmap vcmp'.split())
DENIED_SUFFIXES = frozenset('der p7b p7c p7m p8 jks keystore kdb kdbx gpg pgp enc p12 pfx'.split())
KNOWN_BINARY_BINDINGS = {
    'Mods/vcmi/Content/Sprites/QuickRecruitmentWindow/QuickRecruitmentAllButton.def':
        '884e909ccffa68aab840d8e15f4ae2f6473e57bc',
    'Mods/vcmi/Content/Sprites/QuickRecruitmentWindow/QuickRecruitmentNoneButton.def':
        '2efa0d4d968146ddfa80e73455a05be36725c6c7',
    'osx/dmg_DS_Store': 'f871faa2b9e0d4543fcd0cac871c913bb385be68',
    'config/NEUTRAL.PAL': 'fec2dab03ddf43798aeebbb665c51240390441c1',
}


def known_binary(path, mode, kind, oid):
    expected = KNOWN_BINARY_BINDINGS.get(path)
    if expected is None and oid.decode() not in KNOWN_BINARY_BINDINGS.values():
        return False
    if expected != oid.decode() or mode != b'100644' or kind != b'blob':
        raise ScannerError('known-binary-binding-mismatch')
    return True


def applicability(path):
    # Go RE2 simple folding for these ASCII patterns excludes Python's Turkish
    # I/i extras. No predicate here contains K/S (Unicode Kelvin/long-s folds).
    # RE2 '$' means end of text; Python '$' also accepts before a final newline.
    return tuple(sorted(rule for rule, pattern in PREDICATES.items()
                        if re.search(pattern.removeprefix('(?i)').removesuffix('$') + r'\Z',
                                     path, re.I | re.ASCII)))


def classify(path):
    matched = applicability(path)
    if 'pkcs12-file' in matched:
        return 'presence', matched
    suffix = PurePosixPath(path).suffix.lower().lstrip('.')
    if suffix in DENIED_SUFFIXES:
        return 'denied', ()
    if suffix in EXCLUDED_SUFFIXES:
        return 'excluded', ()
    if matched not in SUFFIX:
        raise ScannerError('unknown-path-applicability')
    return 'text', matched


def representative(oid, rules):
    if rules not in SUFFIX:
        raise ScannerError('unknown-path-applicability')
    name = oid + SUFFIX[rules]
    if applicability(name) != rules:
        raise ScannerError('unknown-path-applicability')
    return name


def validate_manifest(manifest):
    if (not isinstance(manifest, dict) or manifest.get('source_commit') != PIN or manifest.get('version') != '8.30.1'
            or manifest.get('source_sha256') != 'e163e53b9e7e8a8511e77271e2b323ed057759542a6d988258afe3a1fa329caf'
            or manifest.get('source_bytes') != 97731
            or manifest.get('path_predicates') != PREDICATES):
        raise ScannerError('unknown-path-applicability')


def contract():
    return {'policy': 'credential-all-regular-blobs-except-explicit-suffix-exclusions-v2',
            'text': 'all other regular Git blobs; complete strict UTF8, optional leading BOM removed',
            'excluded_suffixes': sorted(EXCLUDED_SUFFIXES),
            'known_binary_bindings': [{'path_sha256': hashlib.sha256(path.encode()).hexdigest(),
                                      'oid': oid, 'mode': '100644', 'kind': 'blob', 'inspected': False}
                                     for path, oid in sorted(KNOWN_BINARY_BINDINGS.items())],
            'known_binary_limits': 'exact identity only; renamed, replaced, duplicate or changed-mode aliases refuse; absent objects receive no exemption; no binary safety claim',
            'denied_credential_binary_suffixes': sorted(DENIED_SUFFIXES),
            'reviewed_nonsecret_findings': disposition.contract(),
            'presence_only_rules': ['pkcs12-file'], 'path_predicates': PREDICATES,
            'source_commit': PIN, 'version': '8.30.1',
            'budgets': dict(entries=budget.ENTRIES, metadata_bytes=budget.METADATA_BYTES,
                            original_unique_bytes=budget.UNIQUE_BYTES, normalized_physical_bytes=budget.PHYSICAL_BYTES,
                            single_original_bytes=budget.SINGLE_BYTES, mapped_findings=budget.FINDINGS,
                            whole_seconds=240, selected_detector_seconds=240, selected_transport_seconds=240,
                            witness_detector_seconds=10, witness_transport_seconds=12,
                            report_bytes=1048576, stderr_bytes=65536),
            'global_path_allowlists': 'original path exclusions intentionally not inherited; fixed representatives evaluated',
            'limits': 'default content allowlists/entropy/stopwords and finite fragments still apply; no exhaustive credential/history/external/binary clearance'}
