# New Horizons version policy

The single product/module authoring value is [config/newHorizonsVersion.json](../config/newHorizonsVersion.json). Menu and generated module read it; development versions may be ahead of the published releases. The version alone does not establish release publication or playable acceptance.

Current source metadata is **0.91.0**; the latest published release remains
[0.90 Early Access](https://github.com/thegandalf196/new-horizons/releases/tag/v0.90).
The Leadership-efficiency
migration requires a new game even from 0.90: writer and minimum accepted save
format are both **1159**. Earlier saves are rejected without conversion or
modification. Product metadata alone does not identify the published package.
See the [save boundary](developers/Serialization.md#current-save-boundary) and
[0.91 preparation changelog](NEW_HORIZONS_CHANGELOG.md#091--unreleased).

Product version, VCMI engine revision, save-schema version and Git commit are separate identities. Save epochs change only for actual compatibility boundaries. Source-matched Windows/Linux packages must record the same full source revision; selected playable promotion is a separate validated operation.

Before 1.0 use `0.MINOR.PATCH`: meaningful player capability checkpoints increase MINOR/reset PATCH; fixes/compatibility/packaging increase PATCH. Reserve 1.0 for explicit scope and delivery acceptance, not the value alone. The menu label uses native small yellow text anchored within the loaded menu background.

Isolated diagnostic exporter identities are not live product releases and do not replace this authority. Read [release scope](NH_VERSION_1_0_SCOPE.md), [phase evidence](NH_PHASE1_EXIT_AUDIT.md) and [delivery contract](NH_DELIVERY_PIPELINE.md).
