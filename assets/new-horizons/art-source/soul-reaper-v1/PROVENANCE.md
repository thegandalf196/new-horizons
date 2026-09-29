# Soul Reaper icon provenance

Status: Provisional. The original icon and exports are source-bound, but user-final art approval and native in-game visual review are not recorded.

## Source

- Subject: the New Horizons Shadow spell Soul Reaper, represented by an iron reaper hook drawing one cold luminous soul from a crumbling charcoal remnant.
- Generated with the host's built-in ImageGen tool on 2026-09-29. No reference image was supplied; no classic-game pixels or existing icon were reused.
- The untouched generated output was copied byte-for-byte to master.png. It is a 1254×1254 RGB PNG; SHA-256: 9edd6e3863e7c04831d2058c91e69d2deb3a0bad1ba1ab6e6ddf957bca280726.
- Exact generation prompt: prompt.txt. The host generation job was 01a0ede2-abf3-7262-90c3-017492e7971f / exec-4fd7d6a5-8730-420f-863e-e8b7f78e557c.png. The host left its original generated file in place.
- The image-generation model/version was not exposed by the tool.

## Raster exports and runtime names

The HoMM3 art skill's export_art.py helper copied the master unchanged into exports/master.png and made square LANCZOS reductions. The exact outputs, dimensions, method, and hashes are recorded in exports/soul-reaper-manifest.json; exports/soul-reaper-comparison.png shows the 44 and 32 px native samples and pixel-repeated enlargements. The 30 px file was separately inspected at native resolution.

| Role | Export | Runtime asset | SHA-256 |
| --- | --- | --- | --- |
| Spell book | exports/soul-reaper-44.png (44×44) | Mods/new-horizons/Images/NH_spell_soul_reaper_44.png | fcfca3429d1b5fdbb43e4dd73d17e30c63f7333030c0b9ab7431f89bb9a86bb4 |
| Scroll and scenario bonus | exports/soul-reaper-32.png (32×32) | Mods/new-horizons/Images/NH_spell_soul_reaper_32.png | e9618a6a90ff5220cf34261bbd9ad12440f3c914b1fceb629ab86ac188d64ef8 |
| Battle effect and immunity | exports/soul-reaper-30.png (30×30) | Mods/new-horizons/Images/NH_spell_soul_reaper_30.png | efe1ae278e1cd153df883cc358ec541f6284a9a961fd398f0bc52d11a4031fb8 |

The three runtime files compare byte-for-byte equal to their corresponding exports. The current spell JSON points these role fields at the approved runtime names; JSON/config edits are owned separately from this art task.

## Deferred consumer check

The art asset specification describes the scenario-bonus icon role as 58×64, while the current Soul Reaper scenario-bonus binding uses the approved 32×32 icon. This does not block the current binding. Confirm the actual scenario UI consumer's dimensions before calling that role fully validated; the size-contract reconciliation is deferred.

## Visual review

At 44×44, 32×32, and 30×30 the crescent hook and cold soul ember remain distinct, with the cracked remnant reading as a secondary fading form. The intentionally dark, textured background is still visible at native size. No native game rendering or user acceptance is claimed.
