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
with a separate profile. The first start that creates the beta profile copies
the stable saves into it.

Each player package records its track as `release_channel` in
`BUILD-IDENTITY.json`; a package without it is stable. Dispatch the Windows
workflow for a release tag with `release_channel` set to `beta` for a
prerelease or `stable` otherwise (tag builds without it fail), and pass the
same value to `tools/ci/stage_linux_client.py --release-channel`. Publish the
GitHub release with the matching prerelease flag. A beta package opens on the
Beta track and plays it from its own folder with the beta profile; its Stable
track starts empty and offers the latest stable release even when it is older.
Hand-over only follows the installation's own track.

### Promoting a beta to Stable

A stable release promotes a beta that held up. Do not cut a stable release from
`main` while an experiment is still in beta.

- **What to promote.** The owner picks a published beta whose changes they
  want, for example 0.93.5. It does not have to be the newest beta.
- **When.** The default checkpoint is the end of a beta series (for the 0.93
  faction series, after 0.93.9). The chosen beta must have been available for
  about one to two weeks with no open crash, corruption or save-loss report.
  Any earlier beta can be promoted at the owner's request.
- **How.** The stable release is the chosen beta's exact source. A detached
  commit on top of that beta's tag changes only the version metadata (the next
  MINOR, for example 0.94.0) and the changelog. Build it with
  `release_channel=stable` and publish it as an ordinary, non-prerelease
  GitHub release. Push only its tag; do not create a branch. Fixes for
  confirmed bugs in that beta can be added as separate detached commits on the
  same line, but no new mechanics.
- **Later betas.** Changes from betas after the promoted one (0.93.6 onward in
  the example) stay on `main`. A change the owner rejects is reverted or
  reworked on `main`; it never reaches Stable unless a later beta that contains
  it is promoted. Because the Beta track only offers releases newer than both
  installed tracks, the next beta after a promotion uses the next MINOR after
  the new stable one (0.95.0 after 0.94.0).
- **Saves.** Stable saves from the previous stable line load in the new stable
  release as usual. Beta saves stay in the beta profile. A save made by a later
  beta (0.93.6 in the example) may use a newer format than the promoted stable
  release can read.
- **Stable fixes during a beta.** Urgent fixes go out as detached commits on the
  current stable tag, with only the new tag pushed, for example 0.94.1.

From 0.92.6 the launcher extracted by the player hands over to the launcher in
the selected stable update after a self-test, so launcher changes reach existing
installations through ordinary updates. Launchers extracted before 0.92.6
cannot hand over; from 0.92.7 the selected update's game setup step, which they
do run on Play, replaces their launcher scripts once (backup kept in the update
store, restored if the new launcher fails its self-test).

Isolated diagnostic exporter identities are not live product releases and do not replace this authority. Read [release scope](NH_VERSION_1_0_SCOPE.md), [phase evidence](NH_PHASE1_EXIT_AUDIT.md) and [delivery contract](NH_DELIVERY_PIPELINE.md).
