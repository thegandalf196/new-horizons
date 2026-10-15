#!/usr/bin/env python3
"""Check indexed, declared text without treating game packs as source text.

Git's cached text/eol attributes take precedence. Otherwise known source/document
extensions and conventional text filenames are checked as LF. Known binaries and
unknown formats are reported separately, not classified by a NUL-byte guess.
Untracked files, submodules and symlink targets are never scanned. This is a
scoped line-ending check, not a validator for every possible text encoding.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import sys

TEXT_EXTENSIONS = frozenset('''
.c .cc .cpp .cxx .h .hh .hpp .hxx .ipp .tpp .inl .m .mm .inc
.py .sh .bash .zsh .ps1 .bat .cmd .cmake .lua .js .mjs .ts .tsx .jsx
.json .jsonc .yaml .yml .toml .ini .cfg .xml .html .htm .css .scss
.md .rst .txt .csv .tsv .sql .patch .diff .in .ui .qrc .rc .desktop
.po .pot .pro .pri .qss .iss .isl .nsi .nsh .gradle .properties .java
.svg .plist .storyboard .strings .conf .nix .lock .spec .dockerfile
.recipe .template .brain .erm .verm .gv .scpt
'''.split())
BINARY_EXTENSIONS = frozenset('''
.png .jpg .jpeg .gif .bmp .ico .icns .webp .ttf .otf .woff .woff2
.wav .ogg .mp3 .flac .webm .mp4 .avi .zip .7z .gz .xz .bz2 .tar
.nhart .docx .pdf .enc .exe .dll .so .a .lib .o .obj .pdb .h3m .vmap
.jks .jar .def .pal .psd
'''.split())
TEXT_NAMES = frozenset({'CMakeLists.txt', 'Makefile', 'Dockerfile', 'AUTHORS',
                        'LICENSE', 'COPYING', 'NOTICE', '.gitignore',
                        '.gitattributes', '.gitmodules', '.editorconfig',
                        '.clang-format', '.clang-tidy', 'Brewfile', 'LINGUAS',
                        'changelog', 'control', 'copyright', 'rules', 'format'})
TEXT_PATHS = frozenset({'vcmibuilder', 'tools/ci/conan-mingw-x64',
    'docs/maintainers/scripts/builds/builds.vcmi.download',
    'docs/maintainers/scripts/builds/download.vcmi.eu',
    'docs/maintainers/scripts/discourse/forum.vcmi.eu',
    'docs/maintainers/scripts/lobby/api.vcmi.eu',
    'docs/maintainers/scripts/web/bugs.vcmi.eu',
    'docs/maintainers/scripts/web/slack.vcmi.eu',
    'docs/maintainers/scripts/web/wiki.vcmi.eu',
    'docs/maintainers/scripts/weblate/weblate.vcmi.eu'})


def git(root, *args, input_bytes=None):
    return subprocess.check_output(['/usr/bin/git', '-C', str(root), *args], input=input_bytes)


def indexed_files(root):
    rows = {}
    for record in git(root, 'ls-files', '--stage', '-z').split(b'\0'):
        if not record:
            continue
        metadata, path = record.split(b'\t', 1)
        mode, _blob, stage = metadata.decode('ascii').split()
        if stage != '0':
            raise ValueError('Unmerged index entry: ' + path.decode('utf-8'))
        rows[path.decode('utf-8')] = mode
    return rows


def attributes(root, names):
    if not names:
        return {}
    raw = git(root, 'check-attr', '--cached', '-z', '--stdin', 'text', 'eol',
              'working-tree-encoding', input_bytes=b'\0'.join(n.encode() for n in names) + b'\0')
    fields = raw.decode('utf-8').split('\0')[:-1]
    result = {}
    for index in range(0, len(fields), 3):
        name, attribute, value = fields[index:index + 3]
        result.setdefault(name, {})[attribute] = value
    return result


def text_policy(name, attrs):
    if attrs.get('text') == 'unset':
        return None, 'attribute--text'
    eol = attrs.get('eol', 'unspecified')
    if eol in {'lf', 'crlf'}:
        return eol, 'attribute-eol'
    if attrs.get('text') == 'set':
        return 'lf', 'attribute-text'
    path = Path(name)
    if path.suffix.lower() in TEXT_EXTENSIONS or path.name in TEXT_NAMES or name in TEXT_PATHS:
        return 'lf', 'known-text'
    if path.suffix.lower() in BINARY_EXTENSIONS:
        return None, 'known-binary'
    return None, 'unknown-format'


def violations(data, expected, encoding='unspecified'):
    if encoding not in {'unspecified', 'unset'}:
        text = data.decode(encoding)
        data = text.encode('utf-8')
    errors = []
    if b'\0' in data:
        errors.append('NUL in declared text (not treated as a binary exemption)')
    without_pairs = data.replace(b'\r\n', b'')
    if b'\r' in without_pairs:
        errors.append('bare CR')
    if expected == 'lf' and b'\r\n' in data:
        errors.append('CRLF in LF text')
    if expected == 'crlf' and b'\n' in without_pairs:
        errors.append('bare LF in explicit CRLF text')
    return errors


def check(root):
    root = root.resolve(strict=True)
    if Path(git(root, 'rev-parse', '--show-toplevel').decode().strip()).resolve() != root:
        raise ValueError('--repo must be the actual Git root')
    files = indexed_files(root)
    attrs = attributes(root, files)
    checked, skipped, errors = [], [], []
    for name, mode in sorted(files.items()):
        if mode not in {'100644', '100755'}:
            skipped.append({'path': name, 'reason': 'symlink-or-submodule'})
            continue
        expected, reason = text_policy(name, attrs[name])
        if expected is None:
            skipped.append({'path': name, 'reason': reason})
            continue
        path = root / name
        redirected_parent = any(p.is_symlink() for p in path.parents if p.is_relative_to(root))
        if path.is_symlink() or redirected_parent or not path.is_file():
            errors.append({'path': name, 'errors': ['missing or nonregular tracked text']})
            continue
        try:
            problems = violations(path.read_bytes(), expected, attrs[name].get('working-tree-encoding', 'unspecified'))
        except (LookupError, UnicodeError) as error:
            problems = ['declared encoding error: ' + str(error)]
        checked.append({'path': name, 'eol': expected, 'reason': reason})
        if problems:
            errors.append({'path': name, 'errors': problems})
    return {'trackedEntries': len(files), 'checkedText': len(checked),
            'checkedByPolicy': dict(Counter(r['reason'] for r in checked)),
            'skippedByScope': dict(Counter(r['reason'] for r in skipped)),
            'violations': errors, 'checked': checked, 'skipped': skipped}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--details', action='store_true')
    args = parser.parse_args()
    result = check(args.repo)
    if not args.details:
        result.pop('checked')
        result.pop('skipped')
    print(json.dumps(result, indent=2))
    return bool(result['violations'])


if __name__ == '__main__':
    sys.exit(main())
