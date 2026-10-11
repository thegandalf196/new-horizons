HEROES III: NEW HORIZONS — Linux x86-64

This build targets Ubuntu 26.04. It is system-library-dependent, not a universal
Linux/AppImage bundle. Other distributions/releases are not verified. Keep the
new-horizons executable, libvcmi.so, config, scripts, Mods and launcher files
together. Extract the whole package into a directory you can read and execute;
do not mix binaries or resources from different releases.

Original Heroes III Complete assets are REQUIRED and are NOT included.
Your installation must contain Data, Maps and Mp3 directories. Asset directories
are mounted read-only by convention; saves/settings go in a separate managed
profile. Do not select an existing ordinary VCMI profile.

From this extracted directory:
  ./Play-New-Horizons.sh --assets "/path/to/Heroes III Complete" --profile "$HOME/.local/share/new-horizons-play"

Add --verify-only to check paths without creating a profile or executing the game.
This preflight does not verify library compatibility, asset completeness or gameplay.
Use Play-New-Horizons.sh: new-horizons-launch.sh is also a developer tool and its
implicit client path is not the packaged default.

Start a single-player scenario from the game's menu. The curated New Horizons
rules and selected artwork are included; no separate mod download is required.
Keep Mods/new-horizons/NewHorizons.nhart intact. Original game archives remain
external; do not extract artwork into the package as a fallback.

Before gameplay, the engine verifies the NHART package and prepares its exact
resource bytes in a new-horizons-art directory within the managed cache tree.
The current pack needs about 27 MB plus filesystem overhead. Later launches
verify and reuse the prepared files; no extraction takes place during animation.
Keep the NHART file. A preparation error means you should check cache space and
permissions, not delete saves or modify your original installation.

The first launch creates a private managed profile for settings and saves. Reuse
that profile for later launches; an existing unmanaged profile is refused. Keep
profiles outside both the extracted package and the original installation.
Saves are stored in <profile>/data/new-horizons/Saves. Earlier releases used
<profile>/data/vcmi/Saves; those files are not moved or deleted. To continue an
old game, close the game and copy its save files (including same-name companion
files) into the new directory. Do not overwrite conflicting names. Retain the
originals until the copied game loads successfully.
Saved gameplay contexts retain their captured rules rather than silently adopting
new defaults. Consult the release notes for version-specific compatibility,
acceptance evidence and known limitations. Numerical balance remains provisional;
Linux checks do not establish native Windows gameplay acceptance.

See the build identity, dependency evidence, licenses and corresponding source
companions accompanying the release. New Horizons-authored geometry/icons are
CC0 where identified by the included notices; original-based artwork retains its
underlying rights and purchaser-supplied Heroes III artwork is not relicensed.
Engine/tooling licensing is retained in license.txt and source notices. Do not redistribute your
purchased game assets, personal profiles or saves with this package.
