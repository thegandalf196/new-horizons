# Giant sword preservation — UP312

Root provisionally accepted this geometry-only Giant correction on 2026-10-08
after native and enlarged private review. This is not Final artwork or user
approval, and does not establish rendered recruitment or playable delivery.

`masters/giant-sword-matte.png` is the single built-in imagegen revision, with
its exact prompt alongside it. Mechanical LANCZOS reduction to 58x64 followed
by grayscale threshold >=128 produces the generated geometry. The versioned
`mattes/giant-sword.png` preserves the old v1 body mask and combines its white
pixels with generated white pixels only in the sword rectangle `(4,0,11,28)`
(right/bottom excluded). Exactly 73 pixels are added; none are removed, and
all pixels outside this rectangle remain unchanged. No manual painting or
original-colour repainting was performed.

The master SHA256 is
`be45034312ccdb52ded953acad79ca1dadbaa43a92b98c59596cecc0f7fe6b69`.
The native matte SHA256 is
`5cb48b0f40db757569d6e500d2d199c9724d10d942942e6322f5dfecef1f29dc`.
The preserved v1 matte SHA256 is
`2e576469ff77e6c07ad529caedead80bc342e39f7fd5c87bd293dd53a83102bd`.

The runtime compositor continues to use purchaser-supplied TWCRPORT frame42
and the existing Academy backdrop. No original colour pixels or private
composites are included here. Cool steel edge pixels near the guard remain a
provisional visual uncertainty. Other creatures, old versions and private R1
review outputs remain unchanged.

Export only the Giant runtime mask with:
`tools/export_new_horizons_academy_gremlin_portraits.py --creature giant`.
Focused tests verify the pinned source/runtime bytes, reproducibility from the
generated master, original body preservation and the bounded 73-pixel addition.
