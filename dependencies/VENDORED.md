# Preserved dependency recipes

This directory preserves the exact tracked source tree of
https://github.com/vcmi/vcmi-dependencies.git at commit
`f61de7b5e1cbfc3d916a600b7468b5d9c2492995`. Original files, notices,
comments and upstream build instructions are unchanged. `UPSTREAM.json` records
their byte sizes and SHA-256 hashes; `tools/ci/verify_vendored_dependencies.py`
checks the snapshot without contacting that host or requiring Conan.

The pinned tree has no repository-wide LICENSE/COPYING file. This preservation
does not invent or replace a license: retain its existing per-file notices and
upstream attribution. Licenses for resolved third-party implementations remain
in the existing dependency notice/corresponding-source packaging pipeline.

The same `dependencies/conanfile.py`, profiles and patches are consumed from
this repository instead of a remotely acquired Git submodule. Nested upstream
workflow files are preserved as source, not installed as this repository's
workflows. Changes require a deliberate reviewed update of the upstream
revision and complete hash inventory; do not silently modify the preserved tree.

This is **not an offline dependency distribution**. The approximately 5 MB
snapshot contains recipes, profiles and patches, not the upstream Windows
binary cache or all dependency implementations. CI still downloads its pinned,
checksum-verified cache, Conan recipes/sources and required compiler tools.
Preserve source companions and verified caches in independent offline copies
for disaster recovery; a source pin alone cannot guarantee host availability.
