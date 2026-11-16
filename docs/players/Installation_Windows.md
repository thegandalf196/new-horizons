# Installing New Horizons on Windows

New Horizons 0.91.2 provides a portable Windows x64 ZIP for Windows 10 and 11.
Download it from the [New Horizons release page](https://github.com/thegandalf196/new-horizons/releases/tag/v0.91.2).
Original Heroes III Complete game assets are required and are not included.

## Before you start

- Install **Heroes III Complete** separately. If you own the GOG offline
  installer, install it first; New Horizons does not extract GOG `.exe`/`.bin`
  installers. The separate Heroes III HD Edition is unsupported.
- Use an ordinary local folder with a short path. First-run setup needs room
  for an additional copy of the original `Data`, `Maps` and `Mp3` folders,
  plus artwork cache and saves. Allow several GiB of free space.
- Use your normal Windows account. Administrator access, build tools and the
  VCMI Qt launcher are not required. The setup helper uses Windows PowerShell 5.1.

## Install and start the portable ZIP

1. Download the **Windows x64 ZIP**, rather than GitHub's source-code ZIP.
2. Extract the **entire ZIP into a new folder**. Keep `new-horizons.exe`,
   `Setup.exe` and `runtime/` together. Do not run inside the ZIP or merge it
   into an existing Heroes III, VCMI or older New Horizons installation.
3. Double-click `new-horizons.exe` at the extracted folder's root.
4. On first launch, select the installed **Heroes III Complete folder containing
   `Data`, `Maps` and `Mp3`**, or its **`Data` subfolder**. The `Maps` and `Mp3`
   folders must still exist beside `Data`. Do not select an installer or a
   compressed archive.
5. Wait for the private copy and verification to finish. The progress window
   remains visible; the game starts when preparation succeeds.

Later launches use the same root `new-horizons.exe` and reuse the completed
import. The original installation is read without being changed. Its executable,
mods, settings and saves are not imported. The imported copy remains usable if
the original installation subsequently moves.

The root executable starts setup and the game. `runtime/new-horizons.exe` is the
inner engine; use the root executable for ordinary play. Keep the package's
`runtime/config/dirs.json` unchanged.

## Choose the original assets again

Close the game and double-click `Setup.exe` beside `new-horizons.exe`. Select
the Complete installation or its `Data` subfolder as described above. Setup
copies and verifies the selected assets, then exits without starting the game.
Use `new-horizons.exe` for ordinary play afterward.

## Your files and updates

New Horizons stores imported assets, settings, cache and logs under:

```text
%LOCALAPPDATA%\HeroesIII-NewHorizons\
```

New saves are stored separately:

```text
%LOCALAPPDATA%\new-horizons\Saves
```

Paste these paths into File Explorer's address bar to open them. Existing
`%LOCALAPPDATA%\HeroesIII-NewHorizons\Saves` files are not moved or deleted.
Separate extractions share this New Horizons profile. Moving or deleting the
portable program folder does not move or remove these files or the original
Heroes III installation. Back up saves before upgrading; see the
[changelog](../NEW_HORIZONS_CHANGELOG.md#0912-early-access) for save compatibility.

To update, close New Horizons and extract the new release into another new
folder. Use its root executable. Do not mix executables, DLLs or support files
from different releases.

## Setup problems

- **Cancel:** no game starts; existing content and saves remain. Launch again
  when you are ready to select the original installation.
- **Wrong folder:** choose the Complete installation containing `Data`, `Maps`
  and `Mp3`, or its `Data` subfolder, with maps and music intact beside it.
- **Copy or access error:** close other New Horizons sessions, check free space
  and ordinary access to your source/profile folders, then retry. Do not run as
  administrator to bypass the error.
- **Missing program file:** extract the entire matching ZIP again. Do not mix
  releases or download individual DLLs from unrelated sites.
- **Blocked startup:** unsigned downloads may show Windows reputation prompts.
  Device policy can also block PowerShell. Do not disable security controls;
  consult your device administrator where needed.

The shipped `runtime/README-New-Horizons.txt` contains the asset-reselection
command, manual-copy fallback and more detailed recovery instructions. Logs are
under `%LOCALAPPDATA%\HeroesIII-NewHorizons\logs`.

Windows compilation, portable-entry and manual Setup smoke tests, and static
package/source checks passed for 0.91.2. Native Windows GUI and gameplay remain unverified;
those checks do not establish successful audio, save/reload or every language/path combination.
