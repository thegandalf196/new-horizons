# Doom icon provenance

Status: Provisional. The original icon and exports are source-created and bound to Doom's spell definition; native in-game rendering and user-final art approval are not yet recorded.

## Source

- Subject: New Horizons Shadow's Doom, an ultimate single-target compound curse; depicted as a battered iron war-mask compressed by binding chains, distinct from the hooked blade and pale soul of Soul Reaper.
- Generated with the host's built-in ImageGen tool on 2026-09-29. No image was supplied to the generator; no classic-game pixels or existing icon were reused. Soul Reaper was inspected only to ensure visual distinction.
- The untouched generated output was copied byte-for-byte to `master.png`. It is a 1254×1254 RGB PNG; SHA-256: `6db75ae0abc8c394176ab9d937af0e9261cb1b503e2cd91435a8c23513c0459f`.
- Exact generation prompt: `prompt.txt`. The host generation job was `01a0ed8b-64dc-7402-9436-2e737369bca8` / `exec-74ab9dd3-fd58-4ea9-a420-a5d9c0845e0a.png`; the host left its original generated file in place. The image-generation model/version was not exposed by the tool.

## Raster exports and runtime names

The HoMM3 art skill's `export_art.py` helper copied the master unchanged and made square LANCZOS reductions. Exact output dimensions, methods, and hashes are recorded in `doom-manifest.json`; `doom-comparison.png` shows the 44 and 32 px native samples and pixel-repeated enlargements. The 30 px file was also inspected at native resolution.

| Role | Export | Runtime asset | SHA-256 |
| --- | --- | --- | --- |
| Spell book | `doom-44.png` (44×44) | `Mods/new-horizons/Images/NH_spell_doom_44.png` | `0a434ecf3c16d1e6aa9f6980ddf810f38bc6bd5b8c4aa81d32182bf2fa0f9236` |
| Scroll and scenario bonus | `doom-32.png` (32×32) | `Mods/new-horizons/Images/NH_spell_doom_32.png` | `413ef058c776ce13492f49ae0fabad758578237e0a1be6cfbf4a03f0cc86bf92` |
| Battle effect and immunity | `doom-30.png` (30×30) | `Mods/new-horizons/Images/NH_spell_doom_30.png` | `a7afb6adaab9baf29e2f4f0e71043af9bc4142f33affff01e508506d614064f2` |

The runtime files are exact copies of the corresponding exporter outputs. The spell definition binds all five graphic roles to these three sizes; visual presentation in a running client is not yet verified.

## Visual review

At 44×44, 32×32, and 30×30 the compressed iron mask and chain silhouette remain legible. The garnet eye is a small restrained accent; the icon reads as restraint/crippling rather than Soul Reaper's spectral extraction. No native game rendering or user approval is claimed.
