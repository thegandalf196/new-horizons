# Storm of Daggers art prompts

All three masters were generated in this session with the built-in image
generation tool, following the HoMM3 art skill. No purchaser-supplied or
extracted Heroes III assets were used.

## Spellbook icon

Style reference: none. This was a new original generation.

```text
Use case: stylized-concept. Generate an original square spellbook icon for a fantasy strategy game, painted as a late-1990s Western PC fantasy miniature still-life. One compact, dense fan of seven distinct small steel throwing daggers converges on a single target point: every blade is a separate short physical dagger with its own visible grip and crossguard, angled inward from different directions, with narrow bright glints along the steel edges. Keep the fan as the dominant centered emblem, filling most of the square with a safe inset. Worn steel, dark leather grips, a few tiny warm-white impact glints at the convergence point; theatrical chiaroscuro, warm light across the upper-left blades and deep oxblood/umber shadow behind them, richly modeled pre-rendered volume with painterly fantasy-book character. Keep the silhouette and individual daggers readable after reduction to 44x44 and 32x32. No lettering, runes, border, interface, target creature, gore, single large arrow or blade, ice or crystal shards, lightning arcs, branching rays, neon bloom, modern flat/vector treatment, cartoon outlines, intentional pixel art or dithering.
```

## Per-target impact animation

Style reference: none. This was a new original generation, separate from the
spellbook icon.

```text
Use case: stylized-concept. Create an original transparent sprite-sheet master for a compact four-cel battle spell impact animation, arranged as a clean 2-by-2 grid of four equal square animation cells on a fully transparent canvas. Every cell has the same camera, scale, center point and lighting, with generous transparent padding inside its own cell; no visible cell borders, dividers, frame numbers, labels, scenery or opaque background. The effect is seen head-on over an invisible enemy stack: seven distinct small steel throwing daggers, each with a visibly separate short blade, straight crossguard and dark grip, fan in from different directions and converge on one central impact point. The four cels progress left-to-right across the top row, then left-to-right across the bottom row: (1) the seven daggers first appear as separated inward-pointing steel glints around the target point, (2) the same daggers sweep closer into a tight fan, (3) all seven tips strike together around the center with a compact warm-white spark, (4) the impact glint fades while the distinct dagger silhouettes briefly remain. Keep all seven blades identifiable in every cel; movement comes from modest angle/position changes, not from turning them into one streak. Late-1990s Western PC fantasy spell illustration, pre-rendered dimensional metal with painterly modeling, small bright silver edges and restrained warm glints, deep shadows only within the daggers. Transparent everywhere outside the daggers and a few tiny impact glints. It must read as many physical daggers, never a single Magic Arrow, ice/crystal shards, lightning/branching rays, a projectile trail, a creature, blood or gore. This master will be deterministically cropped into four 192x192 transparent frames for a fast one-target impact overlay.
```

The built-in generator returned a 1774×887 image. Its four cells were
deterministically exported at the equal crop coordinates recorded in
`storm-of-daggers-art-manifest.json`; each runtime cel is 224×112. The prompt's
192×192 intention was superseded by the actual returned 2:1 cell geometry to
preserve the complete composition without cropping or stretching.

## One-time cast cue

Style/object reference: the local per-target impact master above. It was supplied
only as a dagger-material/style reference; the cast cue was independently
generated and does not reuse its poses.

```text
Use case: stylized-concept. Generate an original transparent 4-cel spell-cast cue sheet, with four equal cells in a 2-by-2 grid on a fully transparent canvas, no visible dividers, border, numbers, labels or scenery. Use the dagger materials and painterly steel treatment from the supplied impact sheet as a STYLE AND OBJECT reference only; do not copy its exact frame poses. This is a single cue shown once at the caster before impacts: seven small separate steel throwing daggers materialize close to a compact central ivory glint and settle into a tight, balanced inward-pointing fan/orbit. Across the four cells (top-left, top-right, bottom-left, bottom-right), show: (1) tiny steel glints gather, (2) all seven distinct daggers begin to appear around the glint, (3) the daggers pivot inward and their edges catch light, (4) the settled seven-dagger fan holds for a beat with one small warm-white core glint. Each cell has the same scale and center, and the complete effect stays inside that cell with generous transparent padding. Keep it compact enough to read over one battlefield stack. Late-1990s Western PC fantasy spell illustration, modeled worn silver steel and dark grips, rich material shading, restrained warm glints. No projectile, outgoing trail, target creature, blood, gore, lightning arcs, branching rays, ice/crystal shards, single arrow, broad glow, background or text.
```

The cast cue is optional runtime art. Bind it once in `animation.cast` if a
separate cue is desired; the impact art belongs in `animation.affect`.
