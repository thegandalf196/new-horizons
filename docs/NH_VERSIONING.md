# New Horizons version policy

The single product/module authoring value is [config/newHorizonsVersion.json](../config/newHorizonsVersion.json); current authoring value is `0.86.0`, prepared for the Early Access release. Menu and generated module read it. The version alone does not establish release publication or playable acceptance. The former 0.85 release/tag was withdrawn.

Product version, VCMI engine revision, save-schema version and Git commit are separate identities. Save epochs change only for actual compatibility boundaries. Source-matched Windows/Linux packages must record the same full source revision; selected playable promotion is a separate validated operation.

Before 1.0 use `0.MINOR.PATCH`: meaningful player capability checkpoints increase MINOR/reset PATCH; fixes/compatibility/packaging increase PATCH. Reserve 1.0 for explicit scope and delivery acceptance, not the value alone. The menu label uses native small yellow text anchored within the loaded menu background.

Isolated diagnostic exporter identities are not live product releases and do not replace this authority. Read [release scope](NH_VERSION_1_0_SCOPE.md), [phase evidence](NH_PHASE1_EXIT_AUDIT.md) and [delivery contract](NH_DELIVERY_PIPELINE.md).
