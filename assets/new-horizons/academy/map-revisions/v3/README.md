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

Input: `../v2/masters/fort.png` (edit target).
Output: `masters/fort.png`, generated with the host built-in image generator.
Earlier masters are retained. This revision assigns no new licence to the
underlying supplied artwork and does not claim user visual approval.

## Exact generation prompt

Use case: precise-object-edit. Edit target is the supplied transparent Academy fortified-town sprite. Remove ONLY the two bright yellow entrance flags and their two standalone flagpoles at the bottom foreground, one on each side of the paved entrance. Reconstruct the tiny areas of paving/rock concealed behind them where appropriate; otherwise retain transparent exterior. No yellow flag cloth or poles should remain. Preserve the exact town architecture, towers, roofs, stone texture, light direction, camera, composition, rocky footing, entrance, scale and framing. Preserve genuinely transparent background. This is an original late-1990s Heroes III style miniature modeled fantasy sprite. Do not redesign, add detail, smooth surfaces, change roof shapes, add flags, text or decorative borders. Keep all other pixels/regions as unchanged as possible.
