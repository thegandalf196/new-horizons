#!/usr/bin/env python3
"""Validate tracked JSONC with jstyleson, without scanning local build/private data.

Index membership and modes select inputs; current working-tree bytes are checked.
No rewrites. Comments and trailing commas retain the existing jstyleson policy,
not JSON5 or a new schema-validation contract.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

import jstyleson


def git(root, *args):
    return subprocess.check_output(
        ['/usr/bin/git', '-C', str(root), *args], stderr=subprocess.PIPE)


def repository_root(path):
    try:
        root = path.resolve(strict=True)
        actual = Path(git(root, 'rev-parse', '--show-toplevel').decode('utf-8').strip())
    except (OSError, UnicodeError, subprocess.CalledProcessError):
        raise ValueError('--repo must name an existing Git checkout') from None
    if actual.resolve() != root:
        raise ValueError('--repo must be the actual Git root, not a subdirectory')
    return root


def indexed_json_files(root):
    result = {}
    for record in git(root, 'ls-files', '--stage', '-z').split(b'\0'):
        if not record:
            continue
        metadata, raw_name = record.split(b'\t', 1)
        mode, _blob, stage = metadata.decode('ascii').split()
        name = raw_name.decode('utf-8')
        if stage != '0':
            raise ValueError('Unmerged index entry: ' + name)
        if Path(name).suffix.lower() == '.json':
            result[name] = mode
    return result


def validate_file(root, name, mode):
    if mode not in {'100644', '100755'}:
        return 'tracked JSON must be a regular file, not a symlink or submodule'
    path = root / name
    redirected = any(parent.is_symlink() for parent in path.parents
                     if parent.is_relative_to(root))
    if path.is_symlink() or redirected or not path.is_file():
        return 'missing, nonregular or symlink-redirected tracked JSON'
    try:
        text = path.read_bytes().decode('utf-8')
        jstyleson.loads(text)
    except UnicodeDecodeError as error:
        return f'invalid UTF-8 at byte {error.start}'
    except OSError as error:
        return 'file read error: ' + (error.strerror or type(error).__name__)
    except json.JSONDecodeError as error:
        return f'JSONC parse error: {error.msg} (line {error.lineno}, column {error.colno})'
    except Exception as error:
        # jstyleson's preprocessor can raise non-JSON exceptions; never disclose
        # arbitrary exception text or source contents in a public CI diagnostic.
        return 'JSONC parser error: ' + type(error).__name__
    return None


def check(root):
    root = repository_root(root)
    files = indexed_json_files(root)
    errors = []
    for name, mode in sorted(files.items(), key=lambda item: (item[0].casefold(), item[0])):
        error = validate_file(root, name, mode)
        if error is not None:
            errors.append({'path': name, 'error': error})
    return {'trackedJson': len(files), 'validJson': len(files) - len(errors),
            'failedJson': len(errors), 'errors': errors}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)
    try:
        result = check(args.repo)
    except (OSError, UnicodeError, ValueError, subprocess.CalledProcessError) as error:
        message = str(error) if isinstance(error, ValueError) else 'unable to read the Git index'
        print(json.dumps({'refused': message}), file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2))
    return bool(result['failedJson'])


if __name__ == '__main__':
    sys.exit(main())
