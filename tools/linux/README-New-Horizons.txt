NEW HORIZONS — Linux x86-64 Artillery mastery and convenience preview

This build targets Ubuntu 26.04. It is system-library-dependent, not a universal
Linux/AppImage bundle. Other distributions/releases are not verified. Keep the
client, libvcmi.so, config, scripts, Mods and launcher files together.

Original Heroes III Complete assets are REQUIRED and are NOT included.
Your installation must contain Data, Maps and Mp3 directories. Asset directories
are mounted read-only by convention; saves/settings go in a separate managed
profile. Do not select an existing ordinary VCMI profile.

From this extracted directory:
  ./Play-New-Horizons.sh --assets "/path/to/Heroes III Complete" --profile "$HOME/.local/share/new-horizons-capabilities"

Add --verify-only to check paths without creating a profile or executing the game.
This preflight does not verify library compatibility, asset completeness or gameplay.
Use Play-New-Horizons.sh: new-horizons-launch.sh is also a developer tool and its
implicit client path is not the packaged default.

The preview includes Orders/Doctrines, six schools for existing spells, saved hero
primary growth, scaled spell-power/mana rules, soft leadership capacity and trained
ballista siege capability. Read-only development views reflect saved identities;
absent primary/capability rules are not adopted from the installed module. Capacity
alone never rejects or deletes creatures; normal army constraints remain. Army
changes update the daily movement limit without instantly refunding current points.
Expanded ratings do not blindly increase creature statistics.

This increment adds Artillery's post-Expert Volley, Precision and Field Repair
choices. Newly Expert Artillery becomes eligible at a later level-up; ordinary
secondary-skill choices come first. Saved mastery identities are authoritative;
old saves without those rules do not silently adopt current defaults.
Landscape adventure screens also have visible quick-save/load buttons and
original ability/status artwork. F8/F9 remain supported; not every ability has
an icon. Six-school artwork is provisional, not final commissioned illustration.
Externally supplied art studies and proprietary reference images are not included.

Other skill mastery families, new spell effects and creature categories are NOT
completed features of this checkpoint. Values are provisional.
Visible new-battle NONE and the broader GUI journeys remain unverified. Linux
results are not native Windows gameplay acceptance.

See the build identity, dependency evidence, licenses and corresponding source
companions accompanying the release. New Horizons-authored geometry/icons are
CC0; purchaser-supplied Heroes III artwork is not relicensed. Engine/tooling
licensing is retained in license.txt and source notices. Do not redistribute your
purchased game assets, personal profiles or saves with this package.
