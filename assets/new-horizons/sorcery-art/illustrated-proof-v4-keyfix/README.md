# V4 illustrative proof — isolated geometry repair, NOT APPROVED

User says V3 is closer but too photorealistic. V4 attempts illustration through
broad authored pigment fields, darker grouped lighting, selective silver accents,
richer blue/carmine and more emphatic key proportions. See ../illustrated-proof-v4/
README.md for actual shader/material/geometry changes: these are procedural3D
methods, NOT claimed manual painting, an image model or a generic painterly filter.
No motif, final quality, style guide or full-family approval has been supplied.

## Preserved failure and exact repair

First V4 render returned0 in19s, but actual image inspection found its enlarged
tilted key bow intersected the corrected left page and appeared broken-U. That
source/raw/clean image and visual RED remain untouched in ../illustrated-proof-v4/.

This scene differs in exactly ONE keyroot-transform line: z.84->.875 and local
rotationXY(7deg,-5deg)->(0,0); scale/Zrotation/materials/light/camera unchanged.
Source SHA256 da808e2d52f3c2809ad23111fccba9d90c31fd24d49404fd858c4122bb7d64a8.
Build verified exact source/delta and explicitly authorized ONE repair-only slot:

```
timeout --signal=TERM --kill-after=10s 60s blender --background --factory-startup --threads 2 --python assets/new-horizons/sorcery-art/illustrated-proof-v4-keyfix/scene.py -- --samples 32
python3 assets/new-horizons/sorcery-art/illustrated-proof-v4-keyfix/reduce.py
```

Actual render EXIT0 elapsed19s, reductions EXIT0; exact blender pgrep empty.
Artist released slot to Build and ran no additional render. Evidence in ignored
`research/sorcery-art/v4-keyfix/{render.log,exit.txt,audit.json}`.

## Actual inspection and review limits

Artist read real768master and native comparison. The key bow is now a complete
closed loop, with no previous broken-U/page intersection visible. This is visual
repair evidence, not comprehensive mesh-intersection certification. Shadow grain,
key/page contact feel, paper-edge modelling and large regular writing still need
quality judgment. Native44/32 shows a larger gray key group than V3; that does NOT
prove Sorcery semantics or painterly acceptance. The image remains visibly a3D
render; user must judge whether color/light/material changes sufficiently address
'less photorealistic'. Do not claim the requested painting quality achieved.

## Download-safe comparison paths

- `output/master-review.png`768x768 RGBA, SHA256
  `3d9aa573ddab127df63bc23f268913c6f23e44e327388dd83304c77cf1ab63e0`.
- `output/native-comparison.png`320x208, actual44/32 V3 versus V4, SHA256
  `b73962242af65e500d319f4fa4682a834f99c46e26fe6d4cb743dfd5423bb10d`.
- `output/native-review.png`, `native-44.png`, `native-32.png`, `slot-82x93.png`.

Review PNGs all have empty metadata; cleaned master RGBA pixels exactly match raw.
Raw master/.blend/settings PRIVATE and ignored until independent privacy review.
No publishing those files based on presence in output/manifest.json. Scene.py is
original editable recipe; .blend exists locally. All new source/art CC0-1.0 per
../LICENSE; no original-reference pixels, external art, fonts or models included.
Pillow premultiplied-alpha Lanczos then straightRGBA, no stretch/sharpen/filter.
No independent rerender/repeatability claim. V3 and firstV4 manifest hashes all
rechecked unchanged after repair, preserving evidence and comparison identity.

Next: Dispatcher presents clean master and native comparison for explicit user
feedback; Content independently reviews. No extra render/fullfamily before user
feedback/Build slot. Final20-asset completion, accepted-example style guide,
reproducible exports, Frontend/Build integration, soleContent GUI and explicit
final-family user approval remain mandatory. No live edits/game launch/commits.
