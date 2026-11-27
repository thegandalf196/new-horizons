# New Horizons version policy

The single product/module authoring value is [config/newHorizonsVersion.json](../config/newHorizonsVersion.json). Menu and generated module read it; development versions may be ahead of the published releases. The version alone does not establish release publication or playable acceptance.

Current source metadata targets **0.94.0-beta.5**. Each release tag
identifies its own source revision.

The current writer is **1169** and the minimum accepted save format remains **1164**. Existing
supported saves retain their captured rules. Starting spellbooks and perks apply to fresh
heroes. Earlier save formats are rejected without conversion
or modification. See the
[save boundary](developers/Serialization.md#current-save-boundary) and
[changelog](NEW_HORIZONS_CHANGELOG.md#0940-beta5).

Product version, VCMI engine revision, save-schema version and Git commit are separate identities. Save epochs change only for actual compatibility boundaries. Source-matched Windows/Linux packages must record the same full source revision; selected playable promotion is a separate validated operation.

Before 1.0 use `0.MINOR.PATCH`: meaningful player capability checkpoints increase MINOR/reset PATCH; fixes/compatibility/packaging increase PATCH. Reserve 1.0 for explicit scope and delivery acceptance, not the value alone. The menu label uses native small yellow text anchored within the loaded menu background.

### Version numbers

Stable releases are `MAJOR.MINOR.PATCH`. Beta-track prereleases carry a
semantic-version suffix: `-beta.N` for betas and `-rc.N` for release
candidates, with N from 1 and no leading zeros (nine digits at most). No other
suffix is accepted. Tags are `v` plus the version, for example
`v0.94.0-beta.1`. Versions compare by semantic-version precedence, never as
text: 0.93.2 < 0.94.0-beta.1 < 0.94.0-beta.9 < 0.94.0-beta.10 < 0.94.0-rc.1 <
0.94.0-rc.2 < 0.94.0 < 0.94.1. A suffixed version is always a GitHub
prerelease and a plain version never is; the launchers ignore or reject a
release whose flag and version disagree, and the release build refuses a tag
whose `release_channel` does not match its version. The launchers show
`0.94.0-beta.3` as "0.94.0 Beta 3" and `0.94.0-rc.1` as "0.94.0 Release
Candidate 1". `Mods/new-horizons/mod.json` keeps the plain `MAJOR.MINOR.PATCH`,
because VCMI mod versions are numeric.

Rampart, 0.94.0-beta.1, is the first suffixed beta; later betas follow as
0.94.0-beta.2, up to 0.94.0-beta.9. 0.93.2 was never published. Packages up to
0.93.x and 0.92.x cannot read suffixed versions, so their launchers never offer
0.94.0-beta.1: players of those packages download 0.94.0-beta.1 manually once,
after which updates arrive as usual.

## Stable and Beta tracks

The repository has two long-lived branches. `beta` is the development and Beta
line: development commits, the Windows tests and the repository-contract checks
run there, and beta tags are cut on it. `main` is the Stable line and receives
only Stable promotions and Stable hotfixes. `main` is currently at 0.92.8
(Stable).

Stable releases are ordinary GitHub releases; the launchers' Stable track only
accepts the official latest release, never a release candidate. Betas are
GitHub prereleases built from tags on `beta`, versioned `MAJOR.MINOR.0-beta.N`
for the next stable MINOR (0.94.0-beta.1 … while Stable is 0.92.x). Release
candidates are prereleases built on `main` (see below). The Beta track offers
only prereleases with a `-beta.N` or `-rc.N` version: the newest one, when it is
newer than both installed tracks (an empty Beta track therefore needs one newer
than the installed stable). A Stable release is never offered there, even when
it was promoted from a beta: after 0.94.0 ships, a player on 0.94.0-beta.6 keeps
it and waits for 0.95.0-beta.1, while the Stable track offers 0.94.0. A beta
install revalidates that its release is still a prerelease with a suffixed
version. The Beta track installs beside the stable version and plays with
a separate profile. The first start that creates the beta profile copies
the stable saves into it.

Each player package records its track as `release_channel` in
`BUILD-IDENTITY.json`; a package without it is stable. Dispatch the Windows
workflow for a release tag with `release_channel` set to `beta` for a `-beta.N`
or `-rc.N` prerelease or `stable` for a plain version (tag builds without it or
with the other value fail), and pass the
same value to `tools/ci/stage_linux_client.py --release-channel`. Publish the
GitHub release with the matching prerelease flag. A beta package opens on the
Beta track and plays it from its own folder with the beta profile; its Stable
track starts empty and offers the latest stable release even when it is older.
Hand-over only follows the installation's own track.

### Promoting a beta to Stable

A stable release promotes a beta that held up. Do not cut a stable release from
the head of `beta` while an experiment is still in beta.

- **What to promote.** The owner picks a published beta whose changes they
  want, for example 0.94.0-beta.5. It does not have to be the newest beta.
- **When.** The default checkpoint is the end of a beta series (after
  0.94.0-beta.9 for the faction series). Any earlier beta can be promoted at the
  owner's request.
- **Release candidate.** The chosen beta's exact source goes onto `main` as
  `0.94.0-rc.1`, a prerelease. On `main`, create one commit whose tree is the
  beta tag's tree and whose parents are `main` and the tag, then one commit
  that changes only the version metadata and the changelog:

  ```sh
  git checkout main
  git merge --ff-only $(git commit-tree 'v0.94.0-beta.5^{tree}' -p main \
      -p 'v0.94.0-beta.5^{commit}' -m "Promote 0.94.0-beta.5 to 0.94.0-rc.1")
  # set config/newHorizonsVersion.json to 0.94.0-rc.1, add the changelog section, commit
  ```

  `main` moves forward without a force push. Tag `v0.94.0-rc.1`, push `main`
  and the tag, build it with `release_channel=beta` and publish it as a GitHub
  prerelease; Beta-track players receive it. Only fixes for confirmed bugs
  follow, as 0.94.0-rc.2 and onward; no new mechanics.
- **Stable.** After about one to two weeks with no open crash, corruption or
  save-loss report, the same code ships as 0.94.0 with only the version and
  changelog changed: tag `v0.94.0`, build it with `release_channel=stable` and
  publish an ordinary, non-prerelease release.
- **Meanwhile.** Development continues on `beta` as 0.95.0-beta.N. Changes from
  betas after the promoted one stay on `beta`; a change the owner rejects is
  reverted or reworked there and reaches Stable only through a later promoted
  beta.
- **Saves.** Stable saves from the previous stable line load in the new stable
  release as usual. Beta saves stay in the beta profile. A save made by a later
  beta (0.94.0-beta.6 in the example) may use a newer format than the promoted
  stable release can read.
- **Stable fixes.** Urgent fixes are commits on `main`, tagged there, for
  example 0.94.1. Copy each one to `beta` with `git cherry-pick`. Never merge
  `main` into `beta`.

From 0.92.6 the launcher extracted by the player hands over to the launcher in
the selected stable update after a self-test, so launcher changes reach existing
installations through ordinary updates. Launchers extracted before 0.92.6
cannot hand over; from 0.92.7 the selected update's game setup step, which they
do run on Play, replaces their launcher scripts once (backup kept in the update
store, restored if the new launcher fails its self-test).

Isolated diagnostic exporter identities are not live product releases and do not replace this authority. Read [release scope](NH_VERSION_1_0_SCOPE.md), [phase evidence](NH_PHASE1_EXIT_AUDIT.md) and [delivery contract](NH_DELIVERY_PIPELINE.md).
