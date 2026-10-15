# Academy map ownership flags — revision v3

Status: Provisional artwork; installed in source, not yet promoted to a playable snapshot.

UP246: the authored town bodies contain yellow entrance flags underneath the
engine's separate player-color flag overlay. The first built-in HoMM3 Art edit
removes the two entrance flags and standalone poles from the retained v2 Fort
master, preserving architecture and transparent exterior. Root inspected the
returned master and mechanically registered 192x192 preview. Both entrance
flags are absent and the town remains legible at native size. Registration uses
the v2 Fort masterSolidBox and sourceSolidBox unchanged; the retained preview is
`previews/fort-native.png`. Village and Capitol have separate retained masters,
exact prompts and native exports in the same revision. Capitol also removes
its baked yellow summit pennant. All three live source bodies match the pinned
manifest and exporter; registration boxes remain unchanged. Fifteen focused
Academy art/map tests pass; independent review finds no blocker. Full handoff
archive reimport is unavailable and user visual approval is not claimed.
The engine ownership overlay and shadow composition must remain unchanged.

Historical input: `../v2/masters/fort.png` (edit target).
Historical output: `masters/fort.png`, generated with the built-in image tool.
These source paths, exact prompts and review previews identify the complete
external authoring archive, not files available in a fresh checkout. Earlier
masters remain preserved there. See the Academy [registration contract](../../README.md#adventure-map-body-revisions)
for selected runtime resources and unchanged external ownership layers. This revision assigns no new licence to the
underlying supplied artwork and does not claim user visual approval.
