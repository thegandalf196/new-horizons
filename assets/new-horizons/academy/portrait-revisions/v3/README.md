# Academy Gargoyle geometry-mask drafts

These are geometry-only white-on-black masks generated as corrective drafts for
the Stone and Obsidian Gargoyle portraits. They contain no original-color
portrait pixels and are not registered for runtime use. The original colored
references remain external or in ignored `build/` diagnostics.

The high-resolution masters are reduced mechanically to 58x64 grayscale using
LANCZOS and threshold `>=128`; the native mattes exist only to produce the
private original-frame composition previews. Each prompt is retained adjacent to
its corresponding master.

## Review disposition

- `*-mask.png` / native `*.png`: first generated drafts; **not approved**. Their
  compositions replace dark facial/stone pixels with Academy background and
  create substantial false cutouts in creature regions.
- `*-mask-r2.png` / native `*-r2.png`: focused facial-feature correction; **not
  approved**. Native composites improve face selection but still replace large
  wing/body regions with background. Do not register either draft for runtime.

Native and 8x-nearest comparisons are private ignored outputs under
`build/nh-up252-validation/`. They show the raw external TWCRPORT portrait at
left and the current matte composited over the authored Academy backdrop at
right. The output is evidence for mask review, not a completed portrait or
playable delivery.
