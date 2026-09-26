# Supplied Mage Guild assets

These three archives were supplied by the project owner for integration on
2026-09-25 and are retained byte-for-byte. Their embedded READMEs describe art
derived from original Heroes III buildings. No new license or CC0 dedication
is asserted for these supplied assets; original game asset rights remain with
their respective owners. They are not the independently generated draft art.

Run `python3 tools/import_new_horizons_mage_guilds.py` from the repository to
reproduce runtime files in `Mods/new-horizons/Content/{sprites,data}`.
PNG files remain unchanged. JSON `basepath` values receive a trailing slash
because VCMI concatenates paths literally.

`universalMageGuilds.json` selectively adopts the supplied structure placement,
animations, masks and hall icons. It preserves New Horizons costs, prerequisite
chains, hall slots and five complete spell rows. The standalone packages' spell
row append operations must not be applied again.

- Castle V: all 11 frames, new area/border masks, hall frame 4.
- Fortress IV/V: all 21 frames per level, original III masks/campaign icon,
  hall frames 3/4.
- Stronghold I–V: guild moved to the former Valhalla ridge, Valhalla moved to
  the former guild ridge; corresponding masks and hall frames 0–4/23.

Original campaign icons are intentionally reused as specified by the packages.
The remaining original hall frames come from the user's installed game data.

Validation: `python3 -m unittest tools.tests.test_user_mage_guild_assets`;
native `NewHorizonsUniqueBuildingTrainingTest.MissingMageGuildLevelsBuildSequentiallyWithLoadedArtBindings`
checks construction prerequisites, resource debits and loaded bindings.
These checks do not substitute for in-game visual acceptance.
