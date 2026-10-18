# Code Organization

Keep each file and function focused on a coherent responsibility. Apply this
policy to every future implementation, maintenance task and review. Existing
large files are technical debt to reduce incrementally; their current size does
not establish a precedent for new work.

## Review thresholds and default ceilings

These are project review guardrails, not claims about universal code quality.
Use physical lines, including comments and blank lines, so counts remain simple
and comparable. Measure a function from its signature through its closing brace.

| Handwritten production code | Review trigger | Default ceiling for new or expanded code |
| --- | --- | --- |
| Implementation file | Above 500 lines | 1,000 lines |
| Function or method | Above 80 lines | 150 lines |

Crossing a review trigger requires examining responsibility boundaries before
adding more logic. Crossing a default ceiling requires decomposition or a
narrowly documented, independently reviewed technical exception. Staying below
the limits does not excuse mixed responsibilities, repeated rules or unclear
ownership. Apply the same principles to headers, Lua and Python; moving an
implementation into a header or another language does not avoid the policy.

Do not reduce counts through compressed statements, removed explanations,
meaningless forwarding layers, textual include fragments or relocation into a
new oversized helper. Keep useful names, comments and explicit execution order.

## Design before expansion

Before implementing a feature, identify the existing owner of its rules, state,
authoritative execution, presentation and AI valuation. Extend an appropriate
cohesive component or extract one with a narrow interface. A large central file
may route to a component; it must not become the default home for every new
mechanic, tooltip, prediction or reaction.

Shared deterministic rules and value transitions belong in the existing shared
layer. Authoritative RNG and mutations stay in simulation execution. UI owns
interaction/presentation, and AI owns candidate policies and utility. Preserve
callbacks, visible-state restrictions, saved-rule identity and serialization.
Prefer explicit ordered stages for combat and lifecycle work; avoid hiding
ordering in an unrestricted event system.

Assignments must state the intended component boundary and file ownership.
Independent review checks whether a change increases concentration, duplicates
behavior, or simply transfers complexity elsewhere. A feature is incomplete when
it leaves an unjustified structural violation, even if it compiles.

## Existing oversized files

Inventory all first-party handwritten implementation files and long functions,
not just the hotspots already named in a review. Rank them by responsibilities,
coupling, change frequency and correctness risk as well as line count. Record
bounded extractions with interfaces, dependencies and regression requirements.
Do not reorganize untouched upstream code merely to meet a numerical target.

When changing an oversized file, first identify a safe extraction in the affected
area. Put new substantive behavior in the appropriate component and keep routing
small. Land characterization and bounded behavior-preserving extraction before,
or separately from, behavior changes. Progressively lower the accepted baseline
after each extraction; do not regrow a file to its original size.

A focused urgent correctness fix may precede extraction when coupling the two
would increase risk. Explain the reason, limit the added lines and record a
specific deferred extraction with owner, interface and acceptance criteria in the
private work plan. This is an exception for that change, not a blanket exemption
for the file or a reason to suspend unrelated work for permission.

## Exceptions and measurement

Generated/vendor code is outside the handwritten thresholds; identify its source
and generator. Do not label handwritten rules as generated to avoid review.
Mechanical registries and serialization tables can justify a larger unit when
their coherence is demonstrated. Record the exact scope, rationale, allowed size
and verification for any exception. Preserve legitimate third-party attribution.

For every changed production file, report before/after lines and explain the
responsibility changes. Report the affected function spans when long functions
are edited. State tool/measurement limits rather than claiming an AST or
complexity check that was not run. Include moved-to files in the comparison so
the report exposes relocated concentration.

Keep per-change measurements, execution receipts and deferred assignments in
private coordination records or the review handoff. Public documentation holds
enduring interfaces and rationale, not another growing activity diary.

## Verification and prevention

Retain and run meaningful behavior tests for each extraction. Protect reaction
and packet/RNG order, rejection atomicity, saved-state compatibility, AI branch
isolation and UI session/teardown behavior where relevant. Update CMake, includes,
tool consumers and test registration together. Preserve existing useful guards
until their protected invariant has adequate replacement coverage.

Keep exact deterministic state and packet comparisons separate from elapsed-clock observations; timings are not deterministic gameplay fields. Retain raw timing observations and evaluate performance separately under the [measurement protocol](../NH_PERFORMANCE_EXPERIMENT.md), without weakening semantic assertions.

Organize large test files by coherent scenarios with reusable fixtures when that
helps. Do not weaken assertions, skip coverage or duplicate entire fixtures to
make a test file smaller. Research papers and design prose do not use source-code
line ceilings.

The repository-maintenance goal must establish a maintained size inventory and
an automated growth check against a reviewed baseline. It should flag new ceiling
violations and unreviewed growth in existing hotspots, with explicit scoped
exceptions. Until that check is implemented, the measurements and independent
review above remain mandatory. Thresholds are review signals; behavior and clear
boundaries are the acceptance criteria.

### Inventory and growth-check tooling

Use an isolated Linux x86-64 CPython 3.15 environment for the pinned parsers.
Set `NH_PYTHON` explicitly to its verified absolute interpreter before running
these commands; no system Python alias or private prebuilt wheel is required.
A C compiler and the matching Python development headers must be available:

```sh
"$NH_PYTHON" -I -B -m pip install --require-hashes --only-binary=:all: -r tools/ci/source_organization_build_requirements.txt
"$NH_PYTHON" -I -B -m pip install --require-hashes --no-build-isolation --no-binary=tree-sitter \
  --only-binary=tree-sitter-cpp,tree-sitter-lua,tree-sitter-java,tree-sitter-javascript,tree-sitter-objc,tree-sitter-bash \
  -r tools/ci/source_organization_requirements.txt
"$NH_PYTHON" tools/ci/source_organization.py inventory --output "$NH_REPORT"
"$NH_PYTHON" tools/ci/source_organization.py check --base-ref "$NH_BASE_REVISION"
```

The core `tree-sitter` pin has no CPython 3.15 wheel: pip authenticates its source
archive before building locally with the separately hash-pinned build tools and
build isolation disabled. The six grammar packages remain binary-only ABI3
wheels with unchanged hashes. The requirements retain the CPython 3.14 core
wheel hash for compatibility; the source hash is not a hash of the locally built
wheel. Fresh CI bootstraps from these public pinned artifacts, not a private
wheel cache. Parser versions and growth-baseline semantics are unchanged.

Set `NH_REPORT` to a local report destination and `NH_BASE_REVISION` to the
reviewed comparison commit.
The comparison commit must already contain the baseline; bootstrap review is
not a normal growth-check bypass. Inventory/proposal commands measure debt but
never approve it. Diagnostic `--include-untracked` is explicit and cannot be
used for the growth gate; do not publish inventories of private input files.

The baseline records exact sizes and content hashes, independent review and
technical rationale. An initial legacy record freezes existing debt, not its
design quality: edits require a reviewed reduction, or a bounded technical
exception with deferred extraction. Removed/reduced units ratchet the baseline
down; new or moved oversized units cannot inherit an old allowance.

Physical file counts cover the selected tracked sources. Function inventories
use Python AST, pinned language parsers, a CMake lexer, or exact-content reviewed
manual spans. Parser recovery and unsupported dialects are explicit coverage
gaps, not proof that no long function exists. A pending gap can freeze only
unchanged pre-existing bytes; editing it requires resolving its function
inventory before acceptance. Keep the remaining coverage visible until resolved.

Function coverage includes first-party tests as well as production sources.
Reviewed manual records replace the complete callable inventory of an exact
file, not just its parser diagnostics. Inventory output applies those records
and reports missing or stale coverage as errors. Test measurements do not impose
the production size ceilings: the growth gate still compares production units.
When comparing an older baseline without test coverage records, historical size
comparison remains production-only; current test coverage must still be valid.

The repository-contracts workflow also checks tracked JSONC, text line endings
and relative includes. Explicit Git attributes preserve intentional Windows CRLF
and pinned byte inventories; binary packs are not text inputs. Markdown uses the
existing lint configuration on changed tracked documents; a lint-configuration
change selects all tracked Markdown. These source checks do not replace native
or rendered acceptance.
