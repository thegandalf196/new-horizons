# Hero Movement and Leadership — provisional replacements

These are the existing HoMM3 Art skill drafts, not newly generated artwork:
Movement is a riding boot with an ivory wing and silver spur; Leadership is a
red command banner held by a gauntlet. Both are original painted still lifes
on opaque brown leather, without original-game or template pixels. Exact
generation prompts are retained beside the masters; neither used image references.

The skill's `export_art.py` produced RGB LANCZOS exports directly from the
1254×1254 masters. The New Horizons hero-screen slots use 44×44 exports, and
the compact legacy-layout Leadership slot uses a 24×24 export. The hero growth
window uses the same Movement painting at its native 32×32 size. No painted
background was stretched or recomposed. The old generic capability family is
retained for unrelated consumers such as town recruitment.

The repository masters omit only the generated caBX metadata carrier; image
chunks and decoded pixels are unchanged. Original raw bytes remain in private
draft outputs; original and sanitized hashes are recorded in manifest.json.
Runtime PNG exports contain only image chunks and are copied unchanged from
the skill helper. Native exports and nearest-neighbor comparisons were inspected.

These bindings and artwork remain Provisional, not Final. In-game rendered
acceptance is still required; source tests and a build cannot establish it.
