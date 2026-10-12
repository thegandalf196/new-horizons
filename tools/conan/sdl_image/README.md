# Local SDL_image interlace recipe

Derived from Conan Center Index `sdl_image/3.4.4` recipe revision
`072329d83163c19393337dc3820e3a5c` (upstream recipe SHA-256
`e7ae89c80dd7d42149a3e9c44cee1dc167cdf07eed5fb557d146ba2a6d10a8f6`).
The recipe's MIT notice is retained in `LICENSE.recipe`; SDL_image's own
implementation license remains its upstream `LICENSE.txt` and is packaged
unchanged. Recipe licensing is not a replacement for implementation licensing.

Local changes export the maintained `tools/ci/prepare_sdl3_image_interlace.py`
and `tools/ci/patches/sdl3-image-3.4.4-interlace.patch`, check exact upstream
3.4.4 bytes, apply precisely two interlace-handling calls before libpng's
read-update calls, and verify the patched hash. Options, codec selection,
dependency requirements, build, package and CMake identities remain upstream.
Exported copies are build inputs, not additional tracked patch/checker copies.

Export into a writable Conan cache (never the preserved dependency archive):

```sh
conan export tools/conan/sdl_image --version=3.4.4 --user=newhorizons --channel=patched
```

Fresh consumer installs require this local export first. Historical notice-only
repacking uses a separate upstream consumer/workflow; do not use the patched
graph to relabel an already-compiled upstream artifact.

Use the existing consumer profile/options with only
`--build='sdl_image/3.4.4@newhorizons/patched'` in place of `--build=never`;
these build modes cannot be combined. Do not use `--build=missing` or cascade.
Record the new complete recipe/package reference and DLL hash. Unchanged
settings/options can retain a package ID while its recipe/package revisions
change; this is not the old upstream binary or source receipt.

Corresponding-source collection must retain this exact recipe, exported
checker/patch and patched implementation. Its standalone local `conan source`
layout includes exported files in the recipe directory; the maintained collector
already reconstructs that layout. All original sources/notices stay pinned.
Source checks or a successful build alone do not certify Windows rendering.
