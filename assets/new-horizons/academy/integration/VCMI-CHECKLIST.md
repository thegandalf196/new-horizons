# Remaining VCMI implementation


### 1. Mod packaging and faction identity

- [ ] Create the New Horizons VCMI mod manifest, dependencies, supported engine version and resource directories. Integrate these files through the mod resource system; do not repack or overwrite the player’s LOD archives. Follow [VCMI’s mod format](https://vcmi.eu/modders/Mod_File_Format/).
- [ ] Implement Academy as the intended **replacement for Tower**, preserving the existing faction identity and numeric compatibility unless New Horizons deliberately requires a separate selectable town. Use Academy as the displayed/localized name. A separate faction would additionally require reassignment of heroes, creatures, dwellings, map objects and selection rules.
- [ ] Update faction description and relevant faction-facing text/localization. Audit map selection, town tooltips, editor labels and faction-specific messages. Do not globally replace the word “tower,” which also describes unrelated structures.
- [ ] Keep original gameplay costs, creature lineup, building requirements, alignment, spell probabilities and hero classes unless a separate design decision changes them.

The local [Tower configuration snapshot](vcmi-tower.json) is the reference used for positions and resource mappings. It came from VCMI’s `develop` branch; pin and verify the actual New Horizons release version before integration.

### 2. Native terrain and map placement

- [ ] Change the faction’s `nativeTerrain` from `["snow"]` to `["sand"]`. **The VCMI identifier is `sand`, not `desert`.** Confirm creature faction references and intended native-terrain effects. See the [official terrain definitions](https://github.com/vcmi/vcmi/blob/develop/config/terrains.json).
- [ ] Update random-map templates/zone rules to generate appropriate sandy home regions where intended. Native terrain does not repaint existing maps. Check faction restrictions and starting-zone terrain behavior against the [random-map template format](https://vcmi.eu/modders/Random_Map_Template/).
- [ ] Edit terrain around Academy towns in authored New Horizons maps when required. A sand-based town sprite placed on an old snowy map remains surrounded by snow tiles until the map itself changes.
- [ ] Verify movement/native-terrain behavior with Academy armies and mixed armies; changing faction terrain is a gameplay change as well as a visual one.

### 3. Adventure-map town object

- [ ] Register the three PNG sprites as VCMI animations and connect village, fort/citadel/castle, and capitol templates. The original Tower uses the fortified sprite for all three defensive building levels.
- [ ] Preserve the map anchor, visitable entrance, blocked tiles, drawing order and terrain eligibility. Use the extracted `.MSK` and original templates as references; PNG transparency is not a movement mask. Review [VCMI’s map object format](https://vcmi.eu/modders/Map_Object_Format/).
- [ ] Create/verify player-color handling for flags and neutral ownership. The current gold flags are ordinary RGBA artwork, not an encoded VCMI player-color mask. Test every player color.
- [ ] Check the capitol pennant, shadows, map edges, fog of war and hero approach/entry alignment at native scale.

### 4. Town selection and other interfaces

- [ ] Register all eight icon PNGs in `town.icons`: `village`/`fort`, each with `normal`/`built`, each with `small`/`large`. Do not replace the entire shared ITPT/ITPA sheets, which contain other factions. The state called `built` means a building was constructed today, not mouse selection. See [VCMI’s faction schema](https://github.com/vcmi/vcmi/blob/develop/config/schemas/faction.json).
- [ ] Point the creature backgrounds and guild window to the new files.
- [ ] Register the 44 construction-menu frames without changing their order. Repeated placeholder frames are intentionally retained. Map the 37 `campaignBonus` resources to their desert counterparts.
- [ ] Register the 48 puzzle pieces under a distinct prefix, retaining the original positions and reveal order in [puzzle-layout.json](puzzle-layout.json).
- [ ] Check scenario setup, town lists, town details, build dialogs, recruitment, guild, campaign bonuses, puzzle reveal, and any New Horizons custom launcher/encyclopedia screens for stale Tower images. Core graphics fields are described in the [faction format](https://vcmi.eu/modders/Entities_Format/Faction_Format/).

### 5. Desert siege

- [ ] Register a dedicated Academy siege prefix and all corresponding suffixes, including `BACK`. Start from Tower’s existing coordinates and suffix mapping; do not substitute the entire castle with a single flattened image.
- [ ] Include `ARCH`, `TPWL`, `MAN1/2/C`, `TW11/12/1C`, `TW21/22/2C`, `WA11/12/13`, `WA2`, `WA31/32/33`, `WA41/42/43`, `WA5`, `WA61/62/63`, and the retained gate pieces. Case/extension/resource resolution must match the target VCMI version.
- [ ] Connect the sandy mine sprite to Tower’s siege-mine visual, preserving the original trap mechanics and explosion animation. Do not invent a water moat to replace the faction’s minefield.
- [ ] Test gate closed, open, blocked and destroyed; walls intact, damaged and destroyed; keep and both shooting towers; all shooter cover layers; catapult and earthquake transitions. Review the [siege renderer’s suffix/state mapping](https://github.com/vcmi/vcmi/blob/develop/client/battle/BattleSiegeController.cpp).
- [ ] Verify seams, bottom contact, projectile origins, shooter occlusion and unit visibility in the actual engine. The preview omits units and firing cover overlays, so it cannot validate them.
- [ ] Keep non-siege terrain battles separate. The existing sand battlefield is available for ordinary desert combat; do not globally replace the snow battlefield just to change Academy sieges. See [VCMI battlefield definitions](https://github.com/vcmi/vcmi/blob/develop/config/battlefields.json).

### 6. Town-screen production work

- [ ] Register the desert scenery and individual building layers, with correct coordinates, z-order, build conditions and upgrade replacement rules. Preserve independent clicking and construction states.
- [ ] Split/package the repaired Mage Guild/wall composite for the relevant layers and stage visibility. The approved full-town preview uses a joint correction, not independently finished corrected assets for all five Mage Guild stages.
- [ ] Bake or register the repaired little scenery-house roof with the correct visibility and stacking. The exact native roof patch and placement are supplied in `town-layout.json`.
- [ ] Verify the supplied basic/upgraded Gremlin Workshop pair in-game; check the repaired spire against its lower stage.
- [ ] Regenerate/verify building click-area and highlight masks where silhouettes changed. Reusing the original invisible masks can leave mismatched interaction areas.
- [ ] Register static PNG animations for an initial static build, or produce matching animated frames before an animated release. Existing new building masters are **single frames**; original animated DEFs have not been fully redrawn. Do not mix new first frames with snowy original later frames. See [VCMI’s animation format](https://vcmi.eu/modders/Animation_Format/).

These are real remaining production tasks, including animation and layer preparation, not completed work hidden behind the previews. The art pass supplies the new visual assets; it does not certify a playable town conversion.

## Release checks

- [ ] Load the mod with no missing resources or schema errors.
- [ ] Compare screenshots at native scale against the supplied reviews; check textures, edges, shadows and palette on light and dark backgrounds.
- [ ] Visit village, fort, citadel, castle and capitol versions with all ownership colors; save and reload.
- [ ] Build every building and upgrade, including all Mage Guild levels, special buildings and Grail; verify animation loops, hover outlines and click targets.
- [ ] Exercise every siege damage/gate state and mine trigger, on surface and underground maps if supported.
- [ ] Confirm only Academy changed, including shared UI resources and battles belonging to other factions.
- [ ] Test authored maps and random maps separately; verify native sand behavior and town selection.

Optional later expansion of the cultural redesign: hero clothing/portraits, creature equipment, outdoor dwellings, town-name lists, music and narrative material. Those are separate art/design decisions; the current conversion preserves the existing roster and approved town layout.

