# Supplied Mage Guild assets

The original three archives were supplied by the project owner for integration
on 2026-09-25 and are retained byte-for-byte. On 2026-10-04 the owner supplied
`fortress-mage-guild-v9.zip` to replace all five Fortress levels. The v8 archive
remains historical source, not the active runtime import. The v9 archive's SHA256
is `7ea9610ae3d0964a8f649779a1d098a60b455df691e838e811399f24ced3ff99`.
Their embedded READMEs describe art
derived from original Heroes III buildings. No new license or CC0 dedication
is asserted for these supplied assets; original game asset rights remain with
their respective owners. They are not the independently generated draft art.

Run `python3 tools/import_new_horizons_mage_guilds.py` from the repository to
reproduce runtime files in `Mods/new-horizons/Content/{sprites,data}`.
PNG, DEF, PCX and BMP bytes remain unchanged. JSON `basepath` values receive a trailing slash
because VCMI concatenates paths literally.

`universalMageGuilds.json` selectively adopts the supplied structure placement,
animations, masks and hall icons. It preserves New Horizons costs, prerequisite
chains, hall slots and five complete spell rows. The standalone packages' spell
row append operations must not be applied again.

- Castle V: all 11 frames, new area/border masks, hall frame 4.
- Fortress I–V v9: native DEF artwork for all five levels, independent masks,
  campaign icons and hall frames0–4. Levels3–5 provide21 torch-animation frames;
  levels1–2 are static. Supplied structure origins preserve the common ground
  line as each successive building grows taller. Native binding/visual evidence
  is tracked separately in UP213; the archive's own README does not claim live
  engine acceptance.
- Stronghold I–V: guild moved to the former Valhalla ridge, Valhalla moved to
  the former guild ridge; corresponding masks and hall frames 0–4/23.

Original campaign icons are intentionally reused as specified by the packages.
The remaining original hall frames come from the user's installed game data.

Validation: `python3 -m unittest tools.tests.test_user_mage_guild_assets`;
native `NewHorizonsUniqueBuildingTrainingTest.MissingMageGuildLevelsBuildSequentiallyWithLoadedArtBindings`
checks construction prerequisites, resource debits and loaded bindings.
These checks do not substitute for in-game visual acceptance.
