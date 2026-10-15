# Design source boundary

The sole gameplay specification is the private library's
[canon/game-design.md](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md).
Authorized maintainers use a sibling checkout or an explicit `NH_DOCS_ROOT`
outside public worktrees. If unavailable, report that access gap; do not restore
a public design copy as a fallback.

Ordinary builds and runtime tests use shipped JSON and the committed NHART pack,
not private documents. See the [provenance contract](../NH_MARKDOWN_CANONICAL_MIGRATION.md)
for metadata and fidelity-check boundaries. The archival DOCX is retained
privately, is not a second canon, and its embedded illustration is not licensed
runtime art merely because it appeared in a design document.

Removing the current copies does not erase previously published Git history,
source archives or downloaded clones. Historical remediation is a separate,
specifically approved operation.
