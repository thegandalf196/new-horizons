# Selected runtime artwork delivery

The distribution resource is `Mods/new-horizons/NewHorizons.nhart`.
`assets/new-horizons/runtime-art-manifest.json` identifies selected bytes,
virtual resource identities, source staging paths, provenance, approval and
required family counts. The pack is genuine NHART v1, not ZIP; its existing
PNG/DEF/PCX/JSON payloads are consumed by the normal engine resource system.

The current selected presentation includes Cabir/Master Cabir animations,
map sprites, portraits and projectiles; turbaned Mage/Arch Mage resources;
Wisps; Academy and Mage Guild artwork; school/spell/casting effects; and custom
skill/perk/Order/interface resources. Original-based modifications and finished
composites are intentionally included. Selection and provisional visual
approval are distinct from origin and licensing. No blanket independent-art
ownership or third-party license grant is asserted.

Unchanged original resources remain external. Original archives, executable
files, rejected alternatives, comparison sheets and authoring masters are not
runtime container inputs. Existing source/provenance/license records remain
applicable. Earlier private-only import classifications do not remove selected
Cabirs, turbaned Magi, Guild composites or interface resources from this pack.

Selected runtime artwork is published through `NewHorizons.nhart`.
Loose authoring files, runtime staging inputs, alternatives and review materials
are maintained locally and are not committed by default. The public repository
retains the package, tools, manifests and records. Calling an export reference,
development-only or superseded is not permission to publish it as a loose image.
This includes deliberate original-based modifications and finished composites.

The supplied application icon has a specific platform-bootstrap exception:
`clientapp/icons/new-horizons.ico` is embedded in the Windows executable, and
the nine `new-horizons.<size>x<size>.png` derivatives serve Linux desktop lookup
before the engine mounts NHART. Their hashes, conversion and authorization are
recorded in `assets/new-horizons/platform-icon-provenance.json`, retained with
the package notices. The source master and review previews remain local; this
exception does not permit loose runtime-art collections.

Before removing tracked artwork, preserve and SHA256-verify recoverable copies
outside the repository. Keep the local input root explicitly configurable; do
not depend on a particular workstation path. Pulling an artwork-removal commit
can remove the tracked files from another worktree: back up its local work first.
Existing Git history, other branches, tags and releases can still contain older
loose copies. This current-tree policy does not erase those copies or prevent
extraction from the package.

## Maintenance

Prepare an explicit input staging root containing the exact source-relative
paths in the manifest. New artwork updates require reviewed inputs; a normal
build must not regenerate the pack from an incomplete local collection.
The manifest's `source` paths describe authoring/import inputs, not files that
must exist in a fresh checkout. Read selected resource bytes through NHART when
testing delivery. Use synthetic images when testing an authoring algorithm.
Generation and review workflows must use an explicit local output root rather
than recreate loose artwork in publicly tracked directories.

The offline `tools/export_new_horizons_magi_small_portraits.py` comparison tool
accepts an explicitly staged `--snapshot` root with the two Mage forms' selected
58x64 portraits, separate transparent foregrounds and current 32px portraits at
the manifest's source-relative paths. It derives nearest-neighbor and Lanczos
32px alternatives from the complete foreground, keeping a 2px inset; it does not
resize the baked faction background or change resource bindings. Pass a fresh
external `--output` and, for review, a fresh `--review` directory under
`$HOME/Downloads/provisory/`. Outputs and manifests are local authoring material,
not build inputs or additional public loose-art files. Selection, repacking and
rendered acceptance remain separate steps. Its maintained tests use synthetic
pixels and require no original-game images or private authoring inputs.

```sh
"$NH_PYTHON" tools/nhart.py pack --manifest assets/new-horizons/runtime-art-manifest.json --input-root "$NH_ART_INPUT_ROOT" --output Mods/new-horizons/NewHorizons.nhart
"$NH_PYTHON" tools/nhart.py inspect Mods/new-horizons/NewHorizons.nhart
"$NH_PYTHON" tools/nhart.py verify Mods/new-horizons/NewHorizons.nhart --manifest assets/new-horizons/runtime-art-manifest.json
"$NH_PYTHON" tools/verify_new_horizons_art_install.py --resources installed-data --manifest assets/new-horizons/runtime-art-manifest.json
```

See [NHART_FORMAT.md](NHART_FORMAT.md) for byte fields and tool contracts.
Verification of a committed distribution needs no handoffs or authoring inputs.
The packer stages and verifies its output before replacement.
For repacking, set `NH_ART_INPUT_ROOT` to the verified external staging directory;
do not use this command during an ordinary build or with incomplete inputs.

### Additive perk/status artwork waves

Use the maintained additive wrapper for reviewed perk/status handoffs; do not
adopt its intermediate standalone perk-selection record. Set `NH_PYTHON` to
the verified local interpreter, `NH_REPO` to the checkout, and the handoff,
work and output variables to explicitly configured private paths. Work and
output roots must be fresh, external, mutually nonoverlapping and outside all
artist-input directories.

For a baseline with an additive ledger, supply its independently reviewed
SHA256 (not a value silently accepted from an unreviewed current file):

```sh
"$NH_PYTHON" tools/import_new_horizons_art_wave.py \
  --repo "$NH_REPO" --perk-handoff "$NH_PERK_HANDOFF" \
  --work-root "$NH_WAVE_WORK" --output-root "$NH_WAVE_OUTPUT" \
  --expected-baseline-ledger-sha256 "$NH_BASELINE_LEDGER_SHA256"
```

Only a reviewed first-wave/virgin baseline may replace the last option with
`--bootstrap-no-ledger`. Its additive ledger must be absent; an existing initial
selection proof must still anchor the actual package/count. Without an initial
proof, the tool requires a completely recognized empty/neutral perk-icon map.
This option is not recovery for a lost ledger on an advanced baseline.

The wrapper prepares an external delta bundle without changing the repository
pack or existing selection history. It preserves earlier wave records and the
initial selection proof, checks baseline stability, and publishes `READY.json`
only after verification. A failed attempt may retain a partial fresh bundle;
without a complete `READY.json`, do not adopt it. Existing output directories
are refused rather than overwritten. Review the complete prepared delta before
a separately authorized import; preparation does not install artwork, grant
Final approval, or justify regeneration during ordinary builds.

The same wrapper supports one explicit selected Mass-spell handoff containing
exactly Mass Bless, Mass Curse, Mass Sorrow and Mass Slow. Each entry pins its
chosen standalone handoff by SHA256; selecting a newer revision requires a new
selection handoff, not editing an earlier record. For a Mass-only wave:

```sh
"$NH_PYTHON" tools/import_new_horizons_art_wave.py \
  --repo "$NH_REPO" --mass-spell-handoff "$NH_MASS_SPELL_HANDOFF" \
  --work-root "$NH_WAVE_WORK" --output-root "$NH_WAVE_OUTPUT" \
  --expected-baseline-ledger-sha256 "$NH_BASELINE_LEDGER_SHA256"
```

No dummy perk handoff is needed. These four families replace only their five
graphics roles, using twelve selected 44/32/30-pixel PNGs. The original spell
selection record and earlier additive waves remain unchanged. Perks may share
this wave, but status and reused-scenario modes cannot. The same fresh-output,
history-preservation, READY and separate-adoption requirements above apply.

### Bounded in-game artwork witness

The opt-in native test
`NewHorizonsHeroFixtureExportTest.ExportOrdinaryFactionBrowsersWithNearbyExperienceQuest`
exports `NHFactionBrowsersXP.h3m` when
`NH_EXPORT_HERO_FACTION_BROWSER_FIXTURE=1` is set in an isolated test profile
with the ordinary New Horizons preset active. It checks canonical Lorelei,
Bron and Thunar starts and captured R2 rules before writing the map. The map
keeps ordinary hero initialization and gives Lorelei a nearby, hero-specific
1000-XP Seer Hut reward; it does not force a perk offer or grant.

Audit the resulting file with the separate header-only family check, writing
its receipt to an explicitly configured private output file:

```sh
"$NH_PYTHON" tools/tests/nh-command-fixture-audit.py \
  --hero-faction-browser "$NH_FIXTURE_MAP" --manifest "$NH_FIXTURE_AUDIT"
```

Use the maintained silent, isolated graphical-testing path for actual play.
Clicking each hero's learned faction Skill icon opens its read-only catalogue;
visiting the hut exercises a real level-up. The catalogue renders normal frame0
at24 pixels, not native44/32 or pressed/disabled states. Neither export nor
header audit proves rendering, legal client interaction or playable delivery.

For specialty presentation, the opt-in native test
`NewHorizonsSpecialtyPresentationFixtureExportTest.ExportOrdinarySpecialtyPresentationMap`
exports `NHSpecialtyPresentation.h3m` when
`NH_EXPORT_SPECIALTY_PRESENTATION_FIXTURE=1` is set in an isolated test profile
with the ordinary New Horizons preset active. It retains authored starts for
adjacent Serena, Gem, Daremyth, Monere, Gerwulf, Elleshar, Malcom, Andra,
Thorgrim, Pasis, Christian and control hero Orrin, checks their captured
rules and specialty identities, and verifies the exported map bytes by reading
and loading them again. The diagnostic contains no forced specialty proc or
custom hero statistics.

Use ordinary Hero Windows, hero exchanges, Kingdom, pregame hero information
and encyclopedia pages to inspect the shared specialty artwork. Check identity
changes, both exchange sides, and save/reload without stale icons. The older
pregame tooltip branch requires interface enhancements disabled in the disposable
profile; with enhancements enabled, the ordinary hero overview replaces it.
The Kingdom list currently requests the small icon only, so it does not establish
large-icon rendering there. Map export and native assertions do not establish
rendered acceptance or playable promotion.

Builtin bootstrap and module scope mount the same full-path pack. Explicit
overlay ordering gives builtin custom art precedence over original archives;
the module mounts its pack after readable Content configuration. No alternate
renderer or image format is introduced. Per-entry independent bounded streams
preserve the existing decoded-image/animation caching policy.

### Installed artwork preparation

NHART remains the public distribution authority; installing prepared resources
does not authorize committing loose artwork. Before gameplay, the native
resource loader prepares a verified, package-digest-keyed directory in the
current profile's cache. Both mounts retain their original resource identities
and precedence, but read prepared files after preparation completes. There is
no per-frame extraction, Python dependency for players, or silent archive
fallback. Preparation preserves payload bytes, including animation descriptors
and frame offsets; it does not regenerate or resize artwork.

The first preparation requires writable cache space. Subsequent launches verify
the prepared inventory before reuse. A damaged or unsafe cache produces an
actionable startup error rather than unverified artwork. Other package versions
and user saves are not removed. Keep the shipped NHART file: the cache is not a
replacement distribution, and ordinary builds still use the committed package.

Windows cache paths can exceed 260 characters. Keep cache enumeration and
no-follow status checks on the project's Boost filesystem backend: the current
MinGW runtime's CRT directory enumeration can fail on such paths even when file
creation succeeds. `CNhArtLoaderTest.DeepCachePathPreparesReusesAndRejectsExtraOrCorruptPayloads`
protects preparation, reuse and integrity rejection. Verify Windows startup with
an equally deep cold cache; Linux regression results alone do not establish it.

Storage preparation and rendering performance are separate concerns. In
particular, replacing archive reads does not by itself fix SDL texture uploads
or image decoding; improvements must be measured rather than assumed.

The native startup cache currently admits packages up to 1 GiB. A crashed
preparation can leave a `<package-digest>.lock` or a digest-prefixed staging
directory under `new-horizons-art/v1`. With all game processes closed, recover
only the identified generated cache entries, retaining the shipped package;
do not delete a whole profile. Failed staging directories are retained rather
than recursively deleting potentially substituted paths. Cache integrity is
checked at startup, not continuously against later same-user modifications.

## Acceptance boundaries

Container integrity, source compilation, resource/decoder integration, rendered
clean-install acceptance, performance comparison and downloadable release
integrity are separate gates. See [integration gaps](NH_INTEGRATION_GAPS.md) for remaining acceptance breadth;
these instructions do not claim completion of unexecuted gates.

Performance comparisons must use identical payload bytes and distinguish
index/loading costs from decoding/drawing. Fresh processes are not proof of a
cold OS cache. No system cache flushing or system-wide setting changes are
required or permitted by this workflow.

### Conditional perk artwork records

The additions ledger and NHART manifest retain format1. An unversioned
`perkPresentations` row is the original strict Arcane Ballistics contract:
captured authored name, historical Countermage fallback and selected alpha
maxima254/253. Its existing rows and reversible header history remain unchanged.

The ten Sylvan Tactics rows use `schemaVersion: 2` with exact ID/name/current
and historical stems, `capturedPolicy: {"sylvanTacticsPolicy": 1}`, alpha maxima255,
original master/native44/native32/export-manifest/selection-evidence hashes and
the reviewed captured resolver's source hash. Missing captured policy retains
Sylvan Luck art, including historical Perfect Moment; Archery Crossfire stays
separate. Unknown fields, identities, policy types/values, provenance or resource
sets fail validation. The allowed origin and selection remain explicit; neither
this schema nor provisional selection grants a license or Final approval.

Each row declares exactly seven resources: native44 alias, native32 companion,
descriptor and four44 states ordered normal0, pressed1, disabled2, highlighted3.
Common checks verify clean RGBA, exact dimensions/native hashes, descriptor,
normal alias equivalence and alpha-preserving state transforms (brightness0.82;
saturation0.15 then brightness0.65; brightness1.13). Sylvan foregrounds retain a
clear two-pixel perimeter at both native sizes. Existing panels and selection
outlines remain UI-owned. Static32 is a packaging companion, not evidence of a
live32 consumer. Verify these records through the maintained additions tests;
source/payload checks do not establish actual rendered acceptance.

### Accepted nongraphics config history

`runtime-art-config-revisions.json` records the explicit accepted config changes
that followed the original artwork waves. It preserves the old ledger hashes.
The two supported files have exact from/to canonical projection hashes, source
commit references and JSON-pointer edits with old/new values. Tests require the
reviewed record fingerprints and exact current projections before reversing any
edit. No Git checkout, network or full history is needed at verification time.

Only the four previously selected status icon pointers and twenty selected Mass
spell icon pointers are excluded from these projections; the existing graphics,
resource, descriptor, provenance and rendering requirements still govern them.
Unknown keys, new graphic roles, descriptions, costs or other changes fail until
a specific new transition is reviewed. Older waves default to revision0. Later
waves explicitly select `configRevisions` for each supported config file; current
accepted stages are5 for `newHorizons.json` and1 for `massVariants.json`.

These are source-history proofs of accepted changes, including The Chosen,
Focus Magic and Mass Slow revisions. They do not certify new functional tests,
rendered behavior or playable delivery. Keep current acceptance evidence separate;
never replace an original art-only historical hash with a current gameplay hash.

Missing historical artwork transitions use finite `artRoles` records for the
selected Feint/Vanguard controls, Living Armor symbols, Ghost animation/portraits
and bug-report icon. Their exact reviewed record fingerprints retain existing
origins, rights, selection and pending rendered acceptance. Tests compare every
original manifest entry and payload hash, PNG facts, descriptors and DEF group
facts; an arbitrary future role cannot be appended as an exception. The eleven
historical specialty families retain `hero-specialty-icons`; explicit reviewed
source entries justify that family without relabeling original resources.

Current original specialty additions use a separate explicit
`provenanceCohort: "current-original-ordinary-specialties-v1"`. Its finite six-record
validator pins complete records, including original generated foreground origin,
selected master and native hashes, authored descriptors, selection evidence and
pending Final/rendered status. It also compares every source entry against the
selected manifest. The historical eleven-record allowlist and payload checks
remain independent and exact. A separate finite thirteen-record cohort uses
`provenanceCohort: "current-original-ordinary-specialties-thirteen-v1"` for Ryland,
Charna, Galthran, Shakti, Fiona, Vokial, Piquedram, Vey, Lacus, Broghild, Gretchin,
Aeris and Theodorus. It pins the same complete provenance, including export
manifest and selected master hashes, without changing the earlier six-record
cohort. These bindings populate the existing ordinary-specialty rows and retain
their captured-policy gate; historical absence retains original presentation.
Selection remains provisional, with Final approval and rendered acceptance
separate from package verification. Missing, unknown or relabeled cohorts cannot
bypass either validator. An additive wave preserves all prior wave objects and
records its baseline archive, manifest, entry inventory, header and config
revision before declaring its exact new resources and resulting archive.

The next four selected ordinary identities use the separate finite cohort
`current-original-ordinary-specialties-four-v1`: Pasis, Kalt, Korbac and Tyraxor.
Their exact master/export/selection/native/source-entry records remain distinct
from the earlier six and thirteen. Runtime artwork follows each existing row's
captured-policy gate, including zero-valued captures; missing policies retain
historical presentation. Package verification grants no Final or rendered approval.

The ranged pair has its own finite cohort,
`current-original-ordinary-specialties-ranged-pair-v1`: Christian Rangefinder
and Jabarkas Cruel Volley. Its eight resources are unchanged selected provisional
44/32 PNGs and normal group0/frame0 descriptors. Earlier cohorts and historical
fallback bindings retain their meaning. Selection does not establish Final visual
approval, gameplay verification or rendered acceptance.

Maintenance and machine artwork remain separate finite three-identity cohorts:
`current-original-ordinary-specialties-maintenance-three-v1` covers Josephine
Field Reassembly, Gerwulf Field Maintenance and Krellion Ogre Bulwark;
`current-original-ordinary-specialties-machine-three-v1` covers Torosar Gunnery
Professor, Arlach Siege Sapper and Gurnisson Heavy Battery. Each has twelve
selected resources with exact original provenance. Resource selection is separate
from runtime binding and mechanic activation; held mechanics remain held.

The remaining nineteen ordinary specialty illustrations are a finite selected
cohort, `current-original-ordinary-specialties-remaining-nineteen-v1`, with
76 native resources. Prior provenance and selections remain exact. Illustration
availability does not activate mechanics, bind a hero or resolve held gameplay.
