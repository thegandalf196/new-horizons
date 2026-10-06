# Barehanded Cabir — provisional source art

This v3 supersedes the pot-based v2 presentation at the user's direction.
`standing-master.png` is the new original base standing master; the matching
upgraded master lives in `../../cabir-master/v3/`. Exact built-in image-generation
prompts are preserved beside each master. All new poses use the HoMM3 Art skill.
Original project artwork is dedicated under CC0-1.0, as with the earlier Cabir
drafts. These are not extracted purchaser game sprites.

`walk-v1/candidate-01.png` is a provisional four-pose barehanded walking atlas,
generated with the base standing master as its identity reference. Its native
export is **not complete**: the existing strict horizontal-atlas exporter rejected
its 2169-pixel width as not divisible into four equal columns before writing any
output. Preserve the master; do not stretch it or install it as a reviewed cycle.
Transparent edge/cell separation, common scale, anchor and gait still need review.

`walk-v2/candidate-01.png` is a new 2×2 walking atlas intended to avoid the
horizontal-strip width constraint. Its user-authorized alpha-only cleanup now
exists in `walk-v2/cleaned/`: 26,581 exterior pixels cleared, opacity 1–9 only,
source and retained RGBA unchanged. Four provisional 450×400 native frames share
one scale and anchor, with a 60-pixel body-height basis and ground y=268. Root and
independent review find no offline export blocker. Colored fringe specks and
animated gait still need review; this is not an installed replacement.

Neither these drafts nor the older pot-based animation sheets constitute a
complete in-game animation set. Runtime bindings and playable delivery are open.

`melee-front-v1/candidate-01.png` contains ready, windup, claw-swipe and recovery
poses. Its extended impact arm crosses the nominal quadrant seam; do not use
blind quadrant cropping. Whole-component separation and native alignment are
still pending. Source SHA-256 identities:

- Base standing: `e9e5cfacb4cafa8f55c0160a400a4f0c6ea3426d77f631c928de27eaa4033060`
- Master standing: `c1c88872bf0cc8e969f9d883b289aff86071db31241d35a16255b74532fbca15`
- Base walk: `8634ed8e7963d692627de292fb559764ba9511feae12fa3f62596ba73111035c`
- Base melee: `b58a766ea7512b10315b8b82f0aa85b6a161534434745b402706da9547935efb`
