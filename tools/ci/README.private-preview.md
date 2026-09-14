# Canonical private player previews

Use `package_private_preview.py` for **both Linux and Windows**. Engine-only
builds are internal diagnostics, not the latest player preview. Public releases
remain a separate recipe: do not add private artwork to Git or CI/public assets.

Inputs must already have independent engine/static acceptance. Supply the private
approved school manifest and its independently approved SHA256 explicitly; never
substitute a newly calculated digest merely to accept changed art. All 144 PNGs
and 18 descriptors are mandatory. Missing or mismatched content is an error.

```
python3 tools/ci/package_private_preview.py \
  --engine ENGINE_STAGE --images PRIVATE_APPROVED_IMAGES \
  --manifest PRIVATE_APPROVED_SIX_SCHOOL_MANIFEST \
  --approved-digest APPROVED_MANIFEST_SHA256 \
  --source EXACT_ENGINE_COMMIT --platform linux --output NEW_OUTPUT_DIRECTORY
```

Use `--platform windows` for the same recipe's Windows ZIP. Neither path executes
the client or loader. Output directories must be new. Binaries remain identical,
six school border references are restored, and launchers use a separate profile
bound to engine/art identity. No source rebuild is needed for resource changes.
Linux requires its engine's installed system libraries; it is not a portable DSO
bundle. Windows retains the accepted engine's DLLs and setup resources.

The package binds engine/source/platform, recipe digest, approved art manifest,
all art bytes and binary hashes. Archives normalize timestamps/ownership. Retain
and deliver the engine's corresponding source/dependency companions alongside
these private archives and retain the private art provenance separately; this
recipe does not manufacture rights or substitute for source-correspondence checks.

Independent changed-package acceptance is still required before player handoff.
Rendered appearance, gameplay, saves, public rights and final art are separate
gates. The Orders placeholder is not replaced without a new approved commission.

Tests: `python3 -B tools/tests/test_private_preview.py` (synthetic fixtures only).
