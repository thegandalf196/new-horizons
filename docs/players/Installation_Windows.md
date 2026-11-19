# Installing New Horizons on Windows

New Horizons provides a portable Windows x64 ZIP for Windows 10 and 11.
Download it from the [latest New Horizons release page](https://github.com/thegandalf196/new-horizons/releases/latest).
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
3. Double-click `new-horizons.exe` at the extracted folder's root. The launcher
   shows the installed version, **Play**, **Check for updates** and
   **Original game files**.
4. For the first game, choose **Play** and select the installed **Heroes III Complete folder containing
   `Data`, `Maps` and `Mp3`**, or its **`Data` subfolder**. The `Maps` and `Mp3`
   folders must still exist beside `Data`. Do not select an installer or a
   compressed archive.
5. Wait for the private copy and verification to finish. The launcher then
   shows **Preparing game artwork and starting New Horizons** while the game
   checks its artwork cache and opens its window.

Later launches use the same root `new-horizons.exe`, reuse the completed import
and show artwork preparation while the game checks its existing cache. The
original installation is read without being changed. Its executable,
mods, settings and saves are not imported. The imported copy remains usable if
the original installation subsequently moves.

The root executable opens the launcher for ordinary play. Keep the package's
`runtime/config/dirs.json` unchanged.

## Choose the original assets again

Close the game and choose **Original game files** in the launcher. `Setup.exe`
also opens the folder selection. Select
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

For a manual update, close New Horizons and extract the new release into a new
folder. Launch its root executable and keep each release's files together.

In the launcher, choose **Install update** when an update is available. The
launcher downloads and verifies the official Windows ZIP and checks the new
client. When the installed version changes, choose **Play**. Saves, settings
and the selected Heroes III assets stay in their existing locations. Extract
the first launcher-capable release into a writable folder.

Managed updates use a separate store for each installation under
`%LOCALAPPDATA%\NH\`. The launcher uses the same store if Windows refers to
the installation by its 8.3 short name. Moving the program folder creates a
new installation identity for updates.

The ZIP's `runtime/README-New-Horizons.txt` reports the measured path budgets
for that package. The full ZIP extraction limit applies to the extracted
package root, including the top-level folder supplied by the ZIP. An existing
installation can update from a longer folder because staging and activation
use the managed store. The update budget lists the maximum `LocalAppData`
path separately.

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
