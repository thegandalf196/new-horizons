HEROES III: NEW HORIZONS - WINDOWS PREVIEW
=======================================

PLAY (no installer, administrator access or build tools)
------------------------------------------------------
1. Download the New Horizons Windows ZIP from this project's GitHub release
   or Actions artifact. Choose the Windows package, not GitHub's source ZIP.
   Extract ALL files into an ordinary local folder you can access. Do not run
   inside the ZIP or merge this package into an existing Heroes III/VCMI install.
2. Double-click Play-New-Horizons.cmd in the extracted folder.
3. On first use, select your legitimate, installed Heroes III Complete folder:
   the parent containing Data, Maps and Mp3. Setup copies only those three
   folders into a private New Horizons profile, then starts the game.
4. Next time, use the same Play-New-Horizons.cmd. A completed import is checked
   and reused; the original installation need not remain at its previous path.

This is a Windows 10/11 preview. Match the package architecture in its release
notes. Compilation/package checks do NOT establish successful native Windows
play, audio, save/reload or every language/path combination. Consult the release
acceptance notes for what was actually tested. No Windows 7 support is promised.

You supply the original game assets. They are not included in this download and
setup does not download them, run the original executable, modify the original
installation, or import its root Mods, settings or saves. Use an intact,
unmodified Complete installation; archive checks cannot certify authenticity
or ownership. Heroes III HD Edition (the separate HD re-release) is unsupported.
GOG .exe/.bin installers must already be installed; this helper does not extract
installers. Do not select Data itself, an installer, or a compressed archive.
No Qt launcher, recommended mods, server/host/address setup or separate server
process is needed for the normal New Horizons single-player route.

DISK SPACE, SAFETY AND PRIVATE FILES
----------------------------------
A first import needs free space for a full additional copy of Data, Maps and
Mp3, plus room for saves/cache. Reselection needs another full copy while the
existing content remains available. Large installations can require several
GiB. Copying and SHA-256 verification can take several minutes.

All runtime data is isolated below:
  %LOCALAPPDATA%\HeroesIII-NewHorizons\

Paste that line into File Explorer's address bar to find:
  content\Data, content\Maps, content\Mp3   private copies of your assets
  config\                                  New Horizons settings
  cache\                                   generated/extracted resources
  logs\                                    diagnostics
  Saves\                                   New Horizons saves

The package's config\dirs.json supplies ALL five path overrides. Do not remove
it or point userDataPath at your original installation: the engine can write
random maps into its data root. No shared Documents\My Games\vcmi profile is
used by this configured package. No symlinks/junctions or related privileges
are required; the helper rejects linked source/profile paths for safety.

Imports use a separate .import-... staging folder. All files must copy and
verify before a completion record is written and the new content is activated.
Cancel, invalid selection or copy failure does not replace live content or
Saves. Subsequent launches check required archives, the completion record and
all recorded file sizes, not merely whether a directory exists. They do not
rehash the full installation every time or certify archives are uncorrupted.
If game startup reports archive corruption, reimport from a known-good source.

Reselection retains the old content as content.previous-... in the profile.
Saves/config are not moved or reset. After successful play you may manually
remove obsolete content.previous-... copies to reclaim space. Do not delete
Saves. A crash/power loss can leave staging/previous folders; while the game is
closed, preserve your previous content and restore it to content if activation
was interrupted. Never merge a partial staging folder into active content.

The helper holds a profile lock during import and play. Do not start a second
session, or use the direct EXE while another helper/game is running. Separate
extractions of this release intentionally share this NH profile, not upstream
VCMI's profile. Moving the extracted program folder does not move your saves.

RESELECT ASSETS / SETUP WITHOUT PLAY
----------------------------------
Close the game. Open Windows PowerShell in the extracted package folder
(File Explorer: type powershell in its address bar). Run:
  powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File ".\Start-New-Horizons.ps1" -SelectAssets

For setup only, add -SetupOnly to that command. For a non-dialog import the
script also accepts -AssetsPath with one literal folder argument; it never
executes that selected path as a shell command. The normal CMD route needs no
command typing and does not accept/forward shell arguments.

ERRORS / RESTRICTIONS
--------------------
- Cancel: no game starts and the previous content/saves remain. Run again later.
- Wrong folder/missing Complete archives: select the Complete parent folder.
  Data must contain nonempty H3bitmap.lod, H3sprite.lod, H3ab_bmp.lod and
  H3ab_spr.lod. Maps must contain .h3m maps and Mp3 must contain .mp3 music.
  These preliminary checks are not a complete engine/content validation.
- Copy/access/disk error: read the error in the console, free disk space, close
  other sessions and check ordinary user access to your source/profile. Retry.
  Do not solve this by running as administrator. An incomplete staging copy
  that could not be cleaned is identified in the error; it is not active.
- Missing EXE/DLL/config: extract the entire matching package again. Do not
  download individual DLLs from unrelated sites. Report a still-missing runtime
  dependency to the release publisher; users should not install build tools.
- Windows PowerShell 5.1 and its Windows Forms folder picker are required for
  the helper. The CMD uses a process-only ExecutionPolicy setting; it does not
  change machine/user policy. Enterprise policy, antivirus or application
  control may still block it. Do not disable security controls; use the manual
  route below when permitted, or consult the device administrator. Unsigned
  preview downloads may show Windows reputation/security prompts.
- Use ordinary local paths shorter than 260 characters, including filenames
  and the temporary staging-folder suffix. Spaces and non-ASCII characters
  are passed as paths, not evaluated commands; their actual Windows execution
  coverage is listed separately in acceptance notes. Very long paths, network
  profiles and redirected/junction paths are not a supported helper contract.

MANUAL FALLBACK (NO POWERSHELL)
------------------------------
This uses File Explorer and the same shipped config\dirs.json. No script,
symlink, elevation, development environment or optional mod setup is needed.

1. Close every New Horizons session. Extract the whole Windows package. Keep
   its config\dirs.json unchanged at that exact location beside the EXE's
   directory. Confirm it specifies the five private paths described above.
2. In File Explorer, open %LOCALAPPDATA% and create HeroesIII-NewHorizons if it
   does not exist. Inside it create a NEW empty folder named content.new.
   If that name already exists from an interrupted copy, do not reuse/merge it:
   preserve or remove the incomplete copy and start with a new empty folder.
3. COPY (do not move) Data, Maps and Mp3 from your legitimate Complete folder
   into content.new. The resulting directories must be exactly:
     %LOCALAPPDATA%\HeroesIII-NewHorizons\content.new\Data
     %LOCALAPPDATA%\HeroesIII-NewHorizons\content.new\Maps
     %LOCALAPPDATA%\HeroesIII-NewHorizons\content.new\Mp3
   Do not copy the original executable, root Mods, config or Saves directories.
4. Wait for all three copies to finish without errors. Check the four required
   Data archives listed above, maps and music. Compare source/copy folder file
   counts and sizes using Explorer Properties. Never activate a failed copy.
5. With the game closed, rename any existing content folder to an unused
   content.previous-... name (keep it as backup), then rename content.new to
   content. If that rename fails, restore the previous content name. Leave
   config, cache, logs and especially Saves alone.
6. Double-click VCMI_client.exe in the EXTRACTED NEW HORIZONS PACKAGE, not the
   original game EXE. The client sets its own working directory to that folder.
   It finds the private assets via config\dirs.json and creates private writable
   directories as needed. Use that direct EXE for subsequent manual-route play.

The manual route does not create the helper's verified import record. Using
Play-New-Horizons.cmd afterwards will request a fresh verified import; this is
intentional, not evidence that a manually copied directory was hash-verified.

LICENSE / SOURCE / ORIGINAL ASSETS
---------------------------------
New Horizons is a VCMI fork. Preserve the included VCMI/GPL license and copyright
notices and dependency licenses. These setup scripts are GPL-2.0-or-later.
Corresponding source for the exact published revision and dependency provenance
must accompany or be linked by the release; see the release's source/notice
files and GitHub source download. Do not redistribute a binary-only package
without satisfying those license obligations. The original Heroes III assets
remain separately copyrighted and are not covered by the engine's GPL. Do not
upload your imported content, saves or personal profile into release artifacts.
