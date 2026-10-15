# Design provenance and runtime independence

Detailed design is maintained in the private library's
[canon/game-design.md](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md).
The archival DOCX and conversion history are preserved privately, not a parallel
authority. Its embedded illustration is reference material, not selected runtime
art or evidence of redistribution permission.

`config/newHorizonsPerks.json` records the logical source identifier
`new-horizons-docs:canon/game-design.md` and a SHA-256 text identity. The runtime
validates this metadata but never opens a design-document path. Current text
identity is UTF-8 with CRLF normalized to LF; all other whitespace, Unicode and
final-newline bytes remain significant. Existing captured source identifiers and
hashes retain their historical compatibility meaning; do not rewrite them when
moving documentation. No private checkout is required to build or play.

The public perk-data suite validates shipped schema, metadata, IDs, effects and
selection limits. The private fidelity suite compares all 31 Skill/310 perk rows,
non-perk anchors and canonical Spell Point terminology using an explicit public
checkout. Separate raw archival hashes prove preservation, not newline-normalized
working-copy bytes. Source comparisons do not prove native mechanics or rendered
acceptance. Historical public copies require separate history remediation.
