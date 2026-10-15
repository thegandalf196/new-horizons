# Artwork production and acceptance workflow

Use [NH_ART_DIRECTION.md](NH_ART_DIRECTION.md) with the current
[asset specifications](NH_ART_ASSET_SPECIFICATIONS.md),
[template composition contract](NH_TEMPLATE_ART_WORKFLOW.md), and
[UI style guide](NH_HOMM3_UI_STYLE_GUIDE.md). No art plugin is required.
This production process is separate from approval of any individual asset,
whole family, runtime import, or selected-file distribution.

## Steps

1. Inspect approved references and expand the actual requested role/rank/state
   manifest. Resolve native dimensions and resource bindings from product consumers.
2. Use an actually available image-generation/editing capability to create original
   subject art. For template composition, generate a real transparent foreground.
   Preserve source bytes; do not claim a prompt or saved sample is a new generation.
3. Verify alpha, dimensions, object identity, materials, clipping, and edge quality.
   Review over black, white, and brown and at native scale. Tool success or a PNG
   extension alone does not establish separation, legibility, or artistic approval.
4. Resize only foreground using premultiplied-alpha LANCZOS and explicit placement.
   Composite onto exact native templates; preserve protected borders/backgrounds.
   Do not stretch completed icons or generate substitute template fields.
5. Save non-destructive versioned masters, foregrounds, exports, comparisons,
   prompts, processing records, and hashes in configured private authoring storage.
6. Present actual previews for user visual review before family expansion or live
   import. Check role/frame order, required family breadth, bindings, and real
   in-game presentation independently. Use the NHART delivery rules for distribution.

Explicit authorization may select technically suitable provisional drafts for
runtime use before Final visual approval. Record that selection separately;
neither process approval nor a preview silently changes an asset's approval state.

Use one initial attempt and at most one targeted correction per request before
reporting an unresolved production limitation. No manual masking from the user
or repeated threshold/chroma-key extraction loop is part of the approved process.
Image tools must actually be available; do not fabricate external dispatch or
promise guaranteed output. Continue unrelated product work when art is blocked.

## Ownership and evidence

Artist owns generation/revision and visual evidence. UI developer confirms native
bindings and integrates approved UI assets. Content designer checks registrations,
portable provenance/attribution, and stated approval state. Engine engineer owns
assigned build/package integration; game tester verifies the exact rendered
candidate. Partition overlapping ownership with the orchestrator.

Process acceptance is not motif approval. Technical checks are not visual
acceptance, in-game verification, or proof of third-party redistribution rights.
Keep prompt/raw metadata, private references, sample experiment names, machine
paths, and full execution receipts outside Git. Public records retain selected
resource identity, required attribution, approval/provenance, and concise contracts.
See [NH_DOCUMENTATION_POLICY.md](NH_DOCUMENTATION_POLICY.md).
