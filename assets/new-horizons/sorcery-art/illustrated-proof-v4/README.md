# V4 single illustrative refinement — awaiting render/user judgment

User said V3 is undoubtedly a step in the right direction, but too photorealistic.
This is directional feedback, NOT motif approval, final quality acceptance or
permission for a full family. V3 remains the exact immutable comparison proof.

## Actual source changes, not a fake painting claim

Original Blender scene.py copied into this new isolated revision, then refined:
- Remove leather micro-noise entirely. No post-render painterly/noise/pixelation/
  posterization filter. No external textures or reference pixels.
- Broad, manually specified warm/cool pigment color stops in object coordinates
  on parchment, blue leather and carmine cloth. Soft EASE interpolation groups
  color over physical forms; these are procedural shader fields, NOT brushwork.
- Lower broad specular response for paper/leather/cloth. Silver uses a darker
  slate body and explicit restricted ivory accent curves on meaningful bow/stem,
  instead of uniformly photoreal mirror response or all-around luminous rim.
- Smaller dominant warm light, darker environment, much reduced violet bounce:
  intended grouped chiaroscuro rather than soft product-studio illumination.
- Exaggerate key silhouette23/30percent along local axes, tighten camera framing.
- Correct left page face normals so solidified sheet no longer hides annotation;
  replace uniform dash grid with sparse flowing authored sepia stroke groups and
  red ornamental initial. No font, historical script or copied manuscript art.
- Close curve end caps so shaft is not an accidental hollow tube.

This method attempts illustration through original modeled form, material/color
and selective accents. It is still a3D render recipe, NOT a hand-painted master
or a guarantee of painterly quality. No human/image-generator commission claimed.
All new original code/geometry/materials/outputs CC0-1.0 per ../LICENSE.

## Bounded execution requested, not yet performed at this checkpoint

Syntax-checked scene.py and reduce.py only. Requested ONE180sTERM/+10kill CPU2threads
32samples768 render from Build after its active native batch. Await explicit GO:

```
timeout --signal=TERM --kill-after=10s 180s blender --background --factory-startup --threads 2 --python assets/new-horizons/sorcery-art/illustrated-proof-v4/scene.py -- --samples 32
# only after successful render, lightweight:
python3 assets/new-horizons/sorcery-art/illustrated-proof-v4/reduce.py
```

reduce.py strips inherited metadata into master-review.png, verifies original
768size, premultiplied-alpha Lanczos reduces actual44/32 and aspect-preserving82x93,
then produces current/native-comparison sheets against read-only V3. No stretch,
sharpening, added texture or painted-filter overlay. Raw master/.blend/settings
remain private/ignored pending metadata review. Editable source is original
scene.py and locally saved .blend; deterministic settings are not yet demonstrated
rerender repeatability. Capture actual command/exit/elapsed/BlenderPID teardown,
inspect real master and native images, and verify clean metadata before sending.

No live asset/generator/product/candidate edits, game launch or commits by Artist.
Need actual user comparison feedback before any further revision/style guide/full
20-family work, and eventually Frontend/Build import, Content sole composed GUI
review, complete technical acceptance and explicit final visual approval.
