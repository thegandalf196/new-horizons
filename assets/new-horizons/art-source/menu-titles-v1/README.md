# Main-menu title patches — provisional

Eight bounded subtitle replacements were generated using the HoMM3 art skill
and the host's built-in image tool: four Complete and four Armageddon's Blade
variants (`GAMSELBK`, `GAMSELB0`, `GAMSELB1`, `LOADBAR`). The user approved using
the available installed variants instead of waiting for a separate Shadow of
Death reference. Unknown/localized variants are not silently covered.

This is a reference-based text edit, not newly commissioned full splash art.
Original game artwork, names and trademarks retain their original rights; no
CC0 or rights-clearance claim is made for these reference-based patches.
Purchaser archives are unchanged. Extracted full reference images and full
generated edits are **not** committed or packaged. Full generated masters and
native-resolution compositions are retained locally under ignored
`output/homm3/menu-titles-v1/`; archive references remain under ignored
`build/menu-title-reference/`.

The manifest records reference payload hashes, generated master hashes and
sizes, exact export rectangles, output hashes and provisional status. Only the
new small subtitle/repair rectangles are runtime assets. The remaining source
illustration is loaded from the player's installation. Runtime CRC matching
selects the right replacement; it does not assume all `H3bitmap.lod` copies
contain identical edition art.

`tools/prepare-new-horizons-menu-art.py` mechanically normalizes the generated
4:3 masters to 800×600 and exports bounded RGBA rectangles with a three-pixel
edge join. It does not invent lettering or repaint scenes. The image tool did
the creative lettering and local backdrop repair. Native compositions were
inspected for text, clipping, style and obvious joins. In-game rendering and
user-final approval remain separate acceptance gates.

Exact prompt text and variant naming are retained in `prompts.md`. The first
generated output was accepted for each of the eight variants; no discarded
candidate is being presented as a delivered asset.
