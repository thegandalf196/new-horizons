# Player-facing New Horizons branding

The desktop player executable is `new-horizons` (`new-horizons.exe` on Windows).
The internal CMake target remains `vcmiclient`; shared-library names, engine
architecture, data/profile paths and save formats remain unchanged. Android's
Qt ABI library name and iOS entry-point naming remain unchanged.

SDL2 and SDL3 windows use the title **New Horizons**. The Linux desktop entry
uses the same name and executable and installs as `new-horizons.desktop`.
Windows client metadata identifies New Horizons while retaining VCMI copyright
and company attribution. Client help explicitly says it is powered by VCMI.
The in-game credits thank the VCMI team and contributors, identify GPL v2.0 or
later, and retain upstream AUTHORS contributors, website and original Heroes III
credits. No license or source notice is removed.

## Application icon boundary

The supplied New Horizons product icon is embedded in `clientapp/icons/new-horizons.ico` and nine Linux desktop derivatives. Exact authorization/conversion/hash provenance is in `assets/new-horizons/platform-icon-provenance.json`; authoring masters and reviews remain private. This is a narrow pre-NHART platform-bootstrap exception, not permission for loose runtime artwork or redistribution of purchaser originals.

## Integration and acceptance

Launchers and package/managed-launch tools must resolve the new desktop binary
name, including `IVCMIDirs::clientPath()` and UNIX development-directory detection.
Keep old mobile ABI names intact. Build targets may still be named `vcmiclient`;
do not confuse target identity with output filename. Packaging must retain the
existing upstream attribution and license files.

Run the focused source/Windows metadata contract gate:

```sh
"$NH_PYTHON" -m unittest tools.tests.test_new_horizons_branding
```

These checks are not a linked-client build or rendered delivery claim. Final
acceptance still requires serialized desktop builds, matching launch/package
paths, Windows resource inspection and an isolated graphical check of the title,
desktop launch and credits. OS-shell appearance remains a separate platform check.
