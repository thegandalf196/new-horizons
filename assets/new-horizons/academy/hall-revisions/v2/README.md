# Academy Village Hall roof revision v2

Status: **Provisional**. Built-in image generation through the HoMM3 Art skill,
using the existing authored `masters/town/buildings/tbtwhall.png` as the edit
target. The exact request is retained in `village-hall.prompt.txt`; no extracted
original-game colors or screenshots are supplied to this revision.

The requested correction joins the visible teal tile plane and gold sunburst
pediment into one coherent gabled roof, rather than two apparently overlapping
roofs. The facade identity, footprint and perspective are intended invariants,
not a claim of pixel-identical repainting. The original master/native handoff
remains unchanged. The generated master is 1927×816 RGBA with real transparent
alpha. Root inspected the 177×75 native and 4× nearest-neighbor before/after
comparisons: the tiled side now reads as one continuous roof slope. This is
Provisional visual review, not user approval or in-game acceptance.

`tools/export_new_horizons_academy_hall_v2.py` mechanically crops the master's
alpha bounds and fits them to the original visible box `(0,2,177,75)` on the
177×75 transparent canvas. It performs no repainting. The exact facade pixels
and alpha contour are not claimed to match the original. Runtime integration
must retain the original interaction masks and scene position, independently
of the revised raster's alpha; the preserved native source remains their input.

This directory preserves an original image-tool output, not a procedural
painting or a screenshot presented as new artwork. Mechanical reduction and
registration are separate from creative generation. No final-art approval,
in-game acceptance, gameplay change or executable delivery is inferred here.
