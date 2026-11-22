# New Horizons version policy

The single product/module authoring value is [config/newHorizonsVersion.json](../config/newHorizonsVersion.json). Menu and generated module read it; development versions may be ahead of the published releases. The version alone does not establish release publication or playable acceptance.

Current source metadata targets **0.93.1 Beta**. Each release tag
identifies its own source revision.

The current writer is **1166** and the minimum accepted save format remains **1164**. Existing
supported saves retain their captured rules. Starting spellbooks and perks apply to fresh
heroes. Earlier save formats are rejected without conversion
or modification. See the
[save boundary](developers/Serialization.md#current-save-boundary) and
[changelog](NEW_HORIZONS_CHANGELOG.md#0931-beta).

Product version, VCMI engine revision, save-schema version and Git commit are separate identities. Save epochs change only for actual compatibility boundaries. Source-matched Windows/Linux packages must record the same full source revision; selected playable promotion is a separate validated operation.

Before 1.0 use `0.MINOR.PATCH`: meaningful player capability checkpoints increase MINOR/reset PATCH; fixes/compatibility/packaging increase PATCH. Reserve 1.0 for explicit scope and delivery acceptance, not the value alone. The menu label uses native small yellow text anchored within the loaded menu background.

## Stable and Beta tracks

Stable releases are ordinary GitHub releases; the launchers' Stable track only
accepts the official latest release. Beta releases are GitHub prereleases built
from `main`, numbered like any other release: while stable continues on
`0.MINOR.PATCH`, betas use the next MINOR (for example 0.93.x beside 0.92.x).
The Beta track offers the newest published release of either kind that is newer
than both installed tracks, installs it beside the stable version, and plays it
with a separate profile. A stable release promotes a beta that held up; do not
cut a stable release from `main` while an experiment is still in beta. An urgent
stable fix during a beta may use a temporary branch from the stable tag.

From 0.92.6 the launcher extracted by the player hands over to the launcher in
the selected stable update after a self-test, so launcher changes reach existing
installations through ordinary updates. Launchers extracted before 0.92.6
cannot hand over; from 0.92.7 the selected update's game setup step, which they
do run on Play, replaces their launcher scripts once (backup kept in the update
store, restored if the new launcher fails its self-test).

Isolated diagnostic exporter identities are not live product releases and do not replace this authority. Read [release scope](NH_VERSION_1_0_SCOPE.md), [phase evidence](NH_PHASE1_EXIT_AUDIT.md) and [delivery contract](NH_DELIVERY_PIPELINE.md).
