# New Horizons source design

`New Horizons.docx` is the sole canonical gameplay design specification.
The original copy received on 2026-09-19 had SHA-256 digest
`d0aa9c0017967e85120b4e63e04df3d58441d606ce9c496d29117515330654ce`.
That historical digest does not identify the newly supplied revision. The
current comparison baseline is recorded in
[the legacy migration audit](../NH_OVERRIDE_MIGRATION.md).

Legacy Overrides must be audited item by item and retired only after every
decision is incorporated, explicitly superseded, or otherwise resolved. It is
not a permanent precedence layer. Unintegrated amendments belong exclusively
in [Pending Changes](../NEW_HORIZONS_PENDING_CHANGES.md).

The document is the newest authority for gameplay scope and mechanics. Detailed
per-system sections take precedence over earlier roadmap and summary tables when
the document conflicts with itself. Repository architecture, authoritative
server-side validation, AI support, save compatibility, licensing, asset
provenance, and validation requirements continue to come from
`NEW_HORIZONS_MVP.md` and `NEW_HORIZONS_DESIGN.md` where the source document is
silent.

The embedded Orders illustration is reference material only. Do not export or
ship it as game art without a separate provenance and redistribution review.
