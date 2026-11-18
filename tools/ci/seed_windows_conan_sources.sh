#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Populate the Windows job’s Conan source cache from verified release archives.
set -euo pipefail

seed_dav1d() {
  source_cache="$RUNNER_TEMP/conan-source-cache"
  archive="$RUNNER_TEMP/dav1d-1.5.4.tar.xz"
  downloaded=
  # The Conan recipe's canonical downloads.videolan.org endpoint has
  # timed out on the hosted runner. These are public VideoLAN mirrors;
  # only bytes matching the pinned upstream SHA-256 are accepted.
  for url in \
    https://videolan.c3sl.ufpr.br/dav1d/1.5.4/dav1d-1.5.4.tar.xz \
    https://videolan.mirror.garr.it/videolan/dav1d/1.5.4/dav1d-1.5.4.tar.xz; do
    echo "Fetching dav1d source mirror: $url"
    if curl --fail --location --retry 3 --retry-all-errors --connect-timeout 20 --max-time 120 \
        --output "$archive" "$url"; then
      if python - "$archive" "$DAV1D_SOURCE_SHA256" <<'PY'
import hashlib
from pathlib import Path
import sys

path, expected = Path(sys.argv[1]), sys.argv[2]
actual = hashlib.sha256(path.read_bytes()).hexdigest()
if actual != expected:
    raise SystemExit(f'dav1d mirror SHA-256 mismatch: {actual}')
PY
      then
        downloaded=1
        break
      fi
    fi
    rm -f "$archive"
  done
  test "$downloaded" = 1
  python - "$source_cache" "$archive" "$DAV1D_SOURCE_SHA256" <<'PY'
from pathlib import Path
import sys

sys.path.insert(0, 'tools/ci')
from preflight_dependency_sources import seed_source_cache

print(seed_source_cache(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]))
PY
  echo "NH_CONAN_SOURCE_CACHE=$source_cache" >> "$GITHUB_ENV"
}

seed_libiconv() {
  source_cache="$RUNNER_TEMP/conan-source-cache"
  archive="$RUNNER_TEMP/libiconv-1.17.tar.gz"
  downloaded=
  # These are GNU-listed mirrors; accept only the checksum pinned by
  # the exact Conan Center Index 1.17 recipe, never the mirror bytes' own hash.
  for url in \
    https://mirrors.ocf.berkeley.edu/gnu/libiconv/libiconv-1.17.tar.gz \
    https://mirror.csclub.uwaterloo.ca/gnu/libiconv/libiconv-1.17.tar.gz; do
    echo "Fetching libiconv source mirror: $url"
    if curl --fail --location --retry 3 --retry-all-errors --connect-timeout 20 --max-time 120 \
        --output "$archive" "$url"; then
      if python - "$archive" "$LIBICONV_SOURCE_SHA256" <<'PY'
import hashlib
from pathlib import Path
import sys

path, expected = Path(sys.argv[1]), sys.argv[2]
actual = hashlib.sha256(path.read_bytes()).hexdigest()
if actual != expected:
    raise SystemExit(f'libiconv mirror SHA-256 mismatch: {actual}')
PY
      then
        downloaded=1
        break
      fi
    fi
    rm -f "$archive"
  done
  test "$downloaded" = 1
  python - "$source_cache" "$archive" "$LIBICONV_SOURCE_SHA256" <<'PY'
from pathlib import Path
import sys

sys.path.insert(0, 'tools/ci')
from preflight_dependency_sources import seed_source_cache

print(seed_source_cache(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]))
PY
  echo "NH_CONAN_SOURCE_CACHE=$source_cache" >> "$GITHUB_ENV"
}

seed_sdl_image() {
  source_cache="$RUNNER_TEMP/conan-source-cache"
  archive="$RUNNER_TEMP/SDL3_image-3.4.4.tar.gz"
  # Canonical official GitHub release; one bounded fetch, no retry loop.
  # Never substitute a new checksum or alter the canonical recipe URL.
  curl --fail --location --max-redirs 3 --proto '=https' --proto-redir '=https' \
    --connect-timeout 20 --max-time 120 --max-filesize 15728640 --retry 0 \
    --output "$archive" https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.4/SDL3_image-3.4.4.tar.gz
  python - "$source_cache" "$archive" "$SDL_IMAGE_SOURCE_SHA256" "$SDL_IMAGE_SOURCE_BYTES" <<'PY'
from pathlib import Path
import sys

sys.path.insert(0, 'tools/ci')
from preflight_dependency_sources import seed_source_cache

archive = Path(sys.argv[2])
if archive.stat().st_size != int(sys.argv[4]):
    raise SystemExit('SDL3_image official source size mismatch')
print(seed_source_cache(Path(sys.argv[1]), archive, sys.argv[3]))
PY
  echo "NH_CONAN_SOURCE_CACHE=$source_cache" >> "$GITHUB_ENV"
}

case "${1:-}" in
  dav1d) seed_dav1d ;;
  libiconv) seed_libiconv ;;
  sdl_image) seed_sdl_image ;;
  *) echo "expected dav1d, libiconv or sdl_image" >&2; exit 2 ;;
esac
