#!/usr/bin/env python3
"""Select tracked Markdown for the existing linter; never run or relax lint.

Compare an explicit ancestor commit with indexed/working-tree changes. Added and
modified Markdown is selected; deletion is omitted and renames are delete+add.
A lint configuration change selects all surviving tracked Markdown. Paths unsafe
for markdownlint's newline/glob input are refused, not silently omitted.
"""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import stat
import subprocess
import sys
import uuid

LINT_CONFIG = 'CI/example.markdownlint-cli2.jsonc'
GLOB_META = frozenset('*?[]{}!()\\')


def git(root, *args):
    return subprocess.check_output(['/usr/bin/git', '-C', str(root), *args])


def paths(raw):
    return [part.decode('utf-8') for part in raw.split(b'\0') if part]


def safe_path(name):
    path = PurePosixPath(name)
    if path.is_absolute() or any(p in {'.', '..'} for p in name.split('/')):
        raise ValueError('Nonrelative or traversing path: ' + repr(name))
    if not name or any(ord(c) < 32 or ord(c) == 127 or c in GLOB_META for c in name):
        raise ValueError('Path unsupported by newline/glob linter input: ' + repr(name))
    return './' + name


def lint_config(name):
    base = PurePosixPath(name).name
    return name == LINT_CONFIG or base == '.markdownlintignore' or base.startswith(
        ('.markdownlint.', '.markdownlint-cli2.'))


def ancestor(root, base):
    if not base or not base.strip():
        raise ValueError('--base must explicitly identify an ancestor commit')
    revision = git(root, 'rev-parse', '--verify', '--end-of-options', base + '^{commit}').decode().strip()
    result = subprocess.run(['/usr/bin/git', '-C', str(root), 'merge-base', '--is-ancestor', revision, 'HEAD'],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        raise ValueError('--base is not an ancestor of HEAD (or Git could not verify it)')
    return revision


def tracked_modes(root):
    result = {}
    for row in git(root, 'ls-files', '--stage', '-z').split(b'\0'):
        if not row:
            continue
        metadata, name = row.split(b'\t', 1)
        mode, _sha, stage = metadata.decode().split()
        if stage != '0':
            raise ValueError('Unmerged tracked path: ' + repr(name))
        result[name.decode('utf-8')] = mode
    return result


def redirected_path(root, path):
    return path.is_symlink() or any(p.is_symlink() for p in path.parents if p.is_relative_to(root))


def regular_path(root, name, mode):
    safe_path(name)
    path = root / name
    if mode not in {'100644', '100755'} or redirected_path(root, path) or not path.is_file():
        raise ValueError('Selected path is missing, symlink redirected or nonregular: ' + repr(name))


def select(root, base):
    root = root.resolve(strict=True)
    actual = Path(git(root, 'rev-parse', '--show-toplevel').decode().strip()).resolve()
    if actual != root:
        raise ValueError('--repo must be the actual Git root; no parent fallback')
    revision = ancestor(root, base)
    changed = set(paths(git(root, 'diff', '--no-renames', '--name-only', '-z', revision, '--')))
    deleted = set(paths(git(root, 'diff', '--no-renames', '--name-only', '--diff-filter=D', '-z', revision, '--')))
    modes = tracked_modes(root)
    for name in modes:
        if name.lower().endswith('.md') and redirected_path(root, root / name):
            raise ValueError('Tracked Markdown path is symlink redirected: ' + repr(name))
    config_changed = any(lint_config(n) for n in changed)
    for name in changed - deleted:
        if lint_config(name):
            regular_path(root, name, modes.get(name))
    candidates = set(modes) if config_changed else changed
    selected = sorted(n for n in candidates - deleted if n.lower().endswith('.md') and n in modes)
    for name in selected:
        regular_path(root, name, modes[name])
    return {'base': revision, 'configChanged': config_changed, 'count': len(selected),
            'files': [safe_path(n) for n in selected]}


def github_output(path, result):
    """Append one safe step-output block to the explicitly supplied file only."""
    if '..' in path.parts:
        raise ValueError('GitHub output path must not traverse parents')
    path = path.absolute()
    if path.is_symlink() or any(p.is_symlink() for p in path.parents):
        raise ValueError('GitHub output path must not be symlink redirected')
    lines = result['files']
    if result['count'] != len(lines) or any(not line.startswith('./') or safe_path(line[2:]) != line for line in lines):
        raise ValueError('Invalid selected-file output payload')
    delimiter = 'NH_MARKDOWN_' + uuid.uuid4().hex
    while delimiter in lines:
        delimiter = 'NH_MARKDOWN_' + uuid.uuid4().hex
    block = 'count=' + str(result['count']) + '\nfiles<<' + delimiter + '\n'
    block += ''.join(line + '\n' for line in lines) + delimiter + '\n'
    if path.exists() and not stat.S_ISREG(path.stat().st_mode):
        raise ValueError('GitHub output must be regular')
    descriptor = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600)
    with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError('GitHub output must be a regular single-link file')
        stream.write(block)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--base', required=True)
    parser.add_argument('--github-output', type=Path)
    args = parser.parse_args()
    result = select(args.repo, args.base)
    if args.github_output:
        github_output(args.github_output, result)
    else:
        print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
