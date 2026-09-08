# User-approved automatic art workflow

## 2026-09-08 — process accepted, no blanket asset approval

After independently reviewed Sorcery spindle output, the user said:
"yes, solved! Please, document this workflow. As you say, from now on, I will
just say what I want and it will be done".

This accepts the production process: user art direction -> art-capable agent
invoking `$homm3-art` where available -> native RGBA generation -> alpha/edge QA
-> deterministic native-template composition -> actual user review. No manual
masking, GIMP work or Blender modeling from the user. It does NOT approve every
future output, the spindle motif as final Sorcery, whole rank families, live import
or third-party-template redistribution. Do not promise guaranteed output or a
background job/direct transport to an external chat that does not exist.

## Proven example

Private external workspace: HoMM3-art, experiment
`output/sorcery-spindle-20260908-a/`. Its `ART_WORKFLOW.md` is the full operating
runbook, linked from local AGENTS/README and the superseded prompts. For a new
art-capable conversation the user can say:

> $homm3-art — follow ART_WORKFLOW.md and create [subject/request].

The coding dispatcher still cannot directly invoke the external image tool.
Plugin invocation, filesystem access and image generation must actually be
available in the receiving session; a literal prompt is not capability evidence.

The returned master/foreground are identical1254x1254 RGBA files, SHA256:
`953af277be10fe4407bc8398f22e432b8530e0d0cfc56c112b4e7c3c003ad2cb`.
One generation, no correction, removal, chroma key or manual mask. Recorded tool
is `image_gen`; model identity was not exposed, so no named model/version claim.

Independent alpha histogram:1,160,887 zero-alpha pixels,411,248 partial-alpha,
381 alpha255. Most object alpha is252/253, approximately99%, a disclosed limitation
preserved without destructive thresholding. Black/white/brown review showed no
obvious broken metal/core or halos. All three final32x32/44x44/82x93 composites
were independently reproduced pixel-for-pixel from the foreground and pinned
native templates. User artistic judgement remains separate from those checks.

## Default steps

First read [NH_ART_ASSET_SPECIFICATIONS.md](NH_ART_ASSET_SPECIFICATIONS.md).
Expand each request into its role-specific rank/state/size manifest: a skill-only
family is12 images, the complete school plus skill is24 (including four spell
mastery borders omitted from the earlier20 count), and individual
spells have their own graphics slots. A successful single concept is not delivery
of the family. Known slot details should not become user technical chores.

1. Inspect private style references and actual native template/slot contracts.
2. Generate an original, isolated foreground with genuine transparency. Use
   modeled fantasy still-life painting, rich materials and painted edges, not
   flat vector art or a photorealistic product render. Volume does not require
   Blender. Start with one clearly readable concept before a family.
3. Preserve raw tool bytes and verify real alpha, transparent background and
   edge/solid-object quality. Run the local QA CLI after receiving RGBA. A PNG
   extension, checkerboard preview, download link or alpha count alone is not
   sufficient. Inspect black/white/brown composites and native32/44 readability.
4. Crop foreground to alpha bounds as appropriate; resize ONLY foreground with
   premultiplied-alpha LANCZOS. Composite onto each exact native template using
   explicit size-specific placement and protected borders/margins. Verify pixels
   outside foreground coverage unchanged. Do not stretch completed icons or
   regenerate/recolor the leather background.
5. Save unique non-destructive experiment output: master, foreground, native
   composites, native comparison, edge review, exact prompt, processing/provenance
   and hashes. A rejected intermediate may be saved honestly labelled rejected.
6. Show actual images and obtain user visual acceptance before family expansion
   or live import. Slot correctness and real in-game presentation remain separate
   integration gates.

One initial attempt and at most one targeted correction per request. If native
alpha or genuine automatic removal fails, report it without demanding manual
work, silently substituting opaque art or restarting repeated threshold/white/
chroma-key loops. Product development continues independently of final art.

## Templates and ownership

Pin remains `e21c90192cb2faf2c4d41347141248e19283a5ce` of
vcmi-mods/modder-tools-pack. Use secondarySkillSmallBlank32x32,
secondarySkillBlank44x44, secondarySkillLargeBlank82x93, and separate
spellSchoolBlank160x96 only for their actual roles. See
[NH_TEMPLATE_ART_WORKFLOW.md](NH_TEMPLATE_ART_WORKFLOW.md) for inventory/licensing.
The successful example's placements are not universal for new subjects.

Private reference pixels, raw generated metadata and local absolute paths stay
out of product Git/releases. The external example build.py is private/path-bound,
not a portable product script to copy wholesale. Upstream CC BY-SA4.0 collection
metadata does not independently clear all game-derived template pixels. Generated
foregrounds and template composites have different provenance; maintain attribution
and selected-file distribution review. caBX/tool sidecars are not verified rights.

Artist owns isolated generation intake/compositor preparation; Frontend approved
UI integration; Content independent evidence/rights checks; Build commits/packages.
This documentation update contains no asset import and no change to gameplay.
Earlier armillary extraction and background-color prompts are historical; this
accepted process supersedes them, not the user's later artistic direction.
