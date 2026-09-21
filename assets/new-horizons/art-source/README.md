# New Horizons generated UI art

These masters were generated for the New Horizons prototype with the host image
generator and are retained as editable provenance references. They are original
artwork, not extracted or traced from Heroes III assets. The runtime exports are
the small RGBA PNGs under `Mods/new-horizons/Images/`:

- `orders-gauntlet-v4/master.png` supplies the current four 48x36 Orders button states.
- `NH_perk_bone_collector_master.png` supplies the Bone Collector 44x44 icon.
- `NH_perk_neutral_master.png` supplies the neutral unknown-perk fallback icon.
- `active-perks-v2/` contains the three newly active provisional perk paintings
  (Inspirational Leader, Wild Chance, and Perfect Moment), their exact prompts,
  44x44/32x32 reductions, comparisons, and reproducible runtime export.

Prompts:

- Orders: “Original fantasy game UI asset ... heavily armored medieval steel gauntlet ...
  clean horizontal four-panel sprite sheet ... normal, pressed, disabled, highlighted ...
  no text, no letters, no book, no spell symbols ... original artwork.”
- Bone Collector: “Original fantasy game UI icon ... ornate iron basket overflowing with
  clean ivory bones and one tiny weathered skull ... modeled still-life ... no text ...”
- Neutral fallback: “Original fantasy game UI icon ... neutral pending-perk emblem, an
  unadorned bronze compass medallion ... no skill-specific symbols ... neutral fallback.”

The generated masters are intentionally kept separate from the installed module
exports so they can be replaced without changing code-level identity mappings.
