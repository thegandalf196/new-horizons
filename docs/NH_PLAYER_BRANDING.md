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

No reviewed New Horizons application/executable icon was found in the selected
runtime-art inventory. Skill, creature and town icons are not implicitly approved
product logos. An unchanged original Heroes III installation icon must not be
committed or redistributed as the executable/desktop icon.

Until an authorized New Horizons product icon is supplied or reviewed, the
existing GPL-covered upstream VCMI icon remains an explicit temporary fallback.
The requested final application icon is therefore **not delivered**. No icon art
was generated or imported by this branding change. A private icon from a user's
original installation would need a separately specified local-only loading or
shortcut path; no such supported path is invented here.

## Integration and acceptance

Launchers and package/managed-launch tools must resolve the new desktop binary
name, including `IVCMIDirs::clientPath()` and UNIX development-directory detection.
Keep old mobile ABI names intact. Build targets may still be named `vcmiclient`;
do not confuse target identity with output filename. Packaging must retain the
existing upstream attribution and license files.

Run the focused source/Windows metadata contract gate:

```sh
python3 -m unittest tools.tests.test_new_horizons_branding
```

These checks are not a linked-client build or rendered delivery claim. Final
acceptance still requires serialized desktop builds, matching launch/package
paths, Windows resource inspection and an isolated graphical check of the title,
desktop launch and credits. The final product-icon blocker remains explicit.
