#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check exact SDL3_image source; optionally publish ONE patched source file.

No downloads, builds, subprocesses or in-place edits. Default is check-only.
An explicit new output in an owned0700 directory is required to write anything.
The preserved upstream source/license bytes are unchanged except the two calls.
Provider selection, rebuilding and corresponding-source delivery are separate.
"""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import tempfile


VERSION = '3.4.4'
SOURCE_SHA256 = 'e1084e3c4b81bf6d1712de74e563d37a03f4b5b0e0a68ffb91b5452e34db6610'
CMAKE_SHA256 = 'a4c20a8a5ac2841d86169522d89d08d9f46dc768ba592711d659ca9e0e060840'
PATCH = Path(__file__).with_name('patches') / 'sdl3-image-3.4.4-interlace.patch'
LIMIT = 1024 * 1024


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def plain(path, directory=False):
    path = Path(path)
    require(path.is_absolute() and '..' not in path.parts, 'Explicit absolute plain path required')
    require(not any(item.is_symlink() for item in (path, *path.parents)), 'Linked input/output path refused')
    info = path.stat()
    require(stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode), 'Wrong input/output type')
    return info


def read(path):
    info = plain(path)
    require(info.st_size <= LIMIT, 'Oversized source or patch')
    with path.open('rb') as stream:
        data = stream.read(LIMIT + 1)
    require(len(data) <= LIMIT, 'Source or patch grew beyond bound')
    return data


def patched_source(source, patch):
    require(digest(source) == SOURCE_SHA256, 'Unsupported or modified IMG_libpng.c')
    require(source.count(b'(*png_set_interlace_handling)') == 1
            and source.count(b'FUNCTION_LOADER_LIBPNG(png_set_interlace_handling,') == 1,
            'Existing interlace API declaration/loader missing or ambiguous')
    require(b'lib.png_set_interlace_handling(' not in source, 'Source already has interlace calls')
    result = source
    for receiver in (b'vars', b'context'):
        before = b'    lib.png_read_update_info(' + receiver + b'->png_ptr, ' + receiver + b'->info_ptr);'
        require(result.count(before) == 1, 'Read-update marker missing or ambiguous')
        result = result.replace(before, b'    lib.png_set_interlace_handling(' + receiver + b'->png_ptr);\n' + before)
    expected_patch = ''.join(difflib.unified_diff(
        source.decode('utf-8').splitlines(keepends=True), result.decode('utf-8').splitlines(keepends=True),
        fromfile='a/src/IMG_libpng.c', tofile='b/src/IMG_libpng.c')).encode('utf-8')
    require(patch == expected_patch, 'Maintained patch differs from exact two-call change')
    return result


def prepare(source_root, output=None):
    source_root = Path(source_root)
    plain(source_root, directory=True)
    source_path = source_root / 'src/IMG_libpng.c'
    cmake_path = source_root / 'CMakeLists.txt'
    source, cmake, patch = read(source_path), read(cmake_path), read(PATCH)
    require(digest(cmake) == CMAKE_SHA256, 'Unsupported or modified SDL3_image CMake source')
    versions = re.findall(rb'^set\((MAJOR|MINOR|MICRO)_VERSION ([0-9]+)\)$', cmake, re.MULTILINE)
    require(versions == [(b'MAJOR', b'3'), (b'MINOR', b'4'), (b'MICRO', b'4')], 'Wrong SDL3_image source version')
    result = patched_source(source, patch)
    temporary = None
    if output is not None:
        output = Path(output)
        require(output.name == 'IMG_libpng.c', 'Output must be a new IMG_libpng.c artifact')
        require(hasattr(os, 'getuid'), 'Publication requires POSIX owned0700 storage; use check-only on Windows')
        parent = plain(output.parent, directory=True)
        require(parent.st_uid == os.getuid() and stat.S_IMODE(parent.st_mode) == 0o700,
                'Output parent must be owned0700 private storage')
        require(not output.is_relative_to(source_root), 'In-place/source-tree output refused')
        require(not output.exists() and not output.is_symlink(), 'Existing output refused')
        fd, temporary = tempfile.mkstemp(prefix='.interlace-source-', dir=output.parent)
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(result)
                stream.flush()
                os.fsync(stream.fileno())
            require(read(source_path) == source and read(cmake_path) == cmake and read(PATCH) == patch,
                    'Inputs changed during preparation')
            current_parent = plain(output.parent, directory=True)
            require((current_parent.st_dev, current_parent.st_ino) == (parent.st_dev, parent.st_ino),
                    'Output parent changed')
            # Atomic no-replace publication, including a racing existing file/link.
            # This artifact is not a full source tree or an installed provider.
            os.link(temporary, output, follow_symlinks=False)
        finally:
            os.unlink(temporary)  # Only our exclusively-created temporary file.
    return {'sourceVersion': VERSION, 'sourceSha256': digest(source), 'cmakeSha256': digest(cmake),
            'patchSha256': digest(patch), 'patchedSourceSha256': digest(result), 'callsAdded': 2,
            'published': output is not None,
            'scope': 'Source artifact only; current providers remain unchanged until separately rebuilt/selected'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--output', type=Path, help='NEW IMG_libpng.c in owned0700 storage; absent means check-only')
    args = parser.parse_args()
    print(json.dumps(prepare(args.source_root, args.output), indent=2))
