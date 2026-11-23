HEROES III: NEW HORIZONS — Linux x86-64

This build targets Ubuntu 26.04. It is system-library-dependent, not a universal
Linux/AppImage bundle. Other distributions/releases are not verified. Keep the
new-horizons executable, libvcmi.so, the four declared SDL3 runtime libraries,
config, Mods and launcher files
together. Extract the whole package into a directory you can read and execute;
do not mix binaries or resources from different releases.

In the launcher, Check for updates looks for a newer Linux release. Install
update downloads and verifies the official archive and checks the new client.
Once the installed version changes, choose Play. Keep the extracted directory
writable for updates. The selected Heroes III data path, settings and saves
stay in the same managed profile.

Version track chooses between Stable and Beta. Beta installs experimental
releases (GitHub prereleases) beside the stable version; Play then starts the
beta. Beta keeps its own profile, new-horizons-play-beta, so its saves and
settings never mix with stable ones. It uses your stable Heroes III folder
until you choose another one for beta with Original game files. Its first
start copies your stable saves into the new beta profile (betas read older
saves); the stable saves stay where they are. Switch back to Stable at any
time; the stable version and its saves are unchanged. Beta offers only betas
and release candidates (0.94.0-beta.3 shows as 0.94.0 Beta 3, 0.94.0-rc.1 as
0.94.0 Release Candidate 1): a stable release, even one promoted from a beta,
appears on the Stable track only, and a beta player waits for the next beta.
Launchers of 0.93.x and older cannot see suffixed versions; download
0.94.0-beta.1 once by hand to move to them.

A beta download (a GitHub prerelease) opens on the Beta track, plays the beta
profile and checks for newer betas. Its Stable track starts empty and offers
the latest stable release, even when that is older than the beta; installing
it leaves the beta unchanged.

What's new shows the release notes of the installed version on the selected
track and of the update on offer, as plain text. It reuses the update check;
offline it shows the notes in CHANGELOG-New-Horizons.md beside the game.

The launcher writes a diagnostic session log to
~/.local/state/new-horizons/launcher.log; failed checks name it in the status.

The declared SDL3/image/mixer/ttf providers are included beside the executable
under their incoming SONAME filenames. BUILD-IDENTITY.json records their exact
bytes and source origins. Other system libraries and optional drivers remain
external; see ELF-DEPENDENCIES.json and the accompanying dependency evidence.
Do not add a private SDK or replacement libraries to make a broken package run.

Original Heroes III Complete assets are REQUIRED and are NOT included.
Your installation must contain Data, Maps and Mp3 directories. Asset directories
are mounted read-only by convention; saves/settings go in a separate managed
profile. Do not select an existing ordinary VCMI profile.

Double-click new-horizons-setup to open the launcher. Use Play to start the game
or Original game files to select your Heroes III Complete folder. On first play
you will be prompted to select the folder. It must contain Data, Maps, Mp3 and the four
nonempty Complete LOD archives (h3bitmap, h3sprite, h3ab_bmp, h3ab_spr).
Your desktop may require marking the extracted executable as executable or
allowing its execution; a shell script may instead open in a text editor.
The native entry calls the packaged launcher without initializing the game.

The desktop launcher uses the system Python 3, python3-gi and GTK 4 bindings
(gir1.2-gtk-4.0 on Ubuntu). Folder selection requires a working desktop dialog
provider. You can give the original data location explicitly using:
  ./Play-New-Horizons.sh --assets "/path/to/Heroes III Complete"

The selected location is remembered privately beside the managed profile in
<profile>.setup/original-assets.json. Original files are referenced, never
copied. The extracted installation may be read-only. --select-assets opens the
chooser again; a missing or moved original installation also prompts again.
Cancel exits without starting the game. --setup-only saves a valid selection
without starting. --assets overrides the location for one launch; an invalid
explicit path fails without opening a dialog.

Add --verify-only to check paths without creating settings, a profile or a game
process. --noninteractive (and --headless/--disable-video game arguments after
--) never prompts: supply --assets or configure the location beforehand.
Preflight checks paths and required archives, not library compatibility or
complete archive contents. Use the public entry; new-horizons-launch.sh alone
is a lower-level developer interface.

Start a single-player scenario from the game's menu. The curated New Horizons
rules and selected artwork are included; no separate mod download is required.
Keep Mods/new-horizons/NewHorizons.nhart intact. Original game archives remain
external; do not extract artwork into the package as a fallback.

Play displays the artwork preparation status until the game is ready. The
engine verifies the NHART package and prepares its exact resource bytes in a
new-horizons-art directory within the managed cache tree.
The current pack needs about 27 MB plus filesystem overhead. Later launches
verify and reuse the prepared files; no extraction takes place during animation.
Keep the NHART file. A preparation error means you should check cache space and
permissions, not delete saves or modify your original installation.

The first launch creates a private managed profile for settings and saves. Reuse
that profile for later launches; an existing unmanaged profile is refused. Keep
profiles outside both the extracted package and the original installation.
Saves are stored in <profile>/data/new-horizons/Saves. Earlier releases used
<profile>/data/vcmi/Saves; those files are not moved or deleted.

SAVE COMPATIBILITY
The current and minimum accepted save format is 1164. Earlier saves are rejected before payload
loading, not converted, rewritten or deleted. Keep your old save files and backups;
use the matching older version to continue those games. Copying an old save into
the new directory does not make it compatible.

Consult the release notes for acceptance evidence and known limitations.
Numerical balance remains provisional; Linux checks do not establish native
Windows gameplay acceptance.

See the build identity, dependency evidence, licenses and corresponding source
companions accompanying the release. New Horizons-authored geometry/icons are
CC0 where identified by the included notices; original-based artwork retains its
underlying rights and purchaser-supplied Heroes III artwork is not relicensed.
Engine/tooling licensing is retained in license.txt and source notices. Do not redistribute your
purchased game assets, personal profiles or saves with this package.
