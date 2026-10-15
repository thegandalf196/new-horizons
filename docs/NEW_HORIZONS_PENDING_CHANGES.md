# Design amendment boundary

Detailed amendments, accepted decision rationale and unresolved alternatives live
in the private design library's
[legacy amendment index](https://github.com/thegandalf196/new-horizons-docs/blob/main/proposals/legacy-amendments.md).
The [canon](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md)
remains authoritative. Migration does not adopt drafts, reverse accepted
decisions, or reinterpret historical saves. This public path is retained for
existing contributor links; it is not a queue for new design proposals.

Public contributors can build and test shipped configuration without private
design input. Use [content contracts](developers/NewHorizonsContent.md),
[integration limits](NH_INTEGRATION_GAPS.md) and
[mechanic review contracts](NH_PROVISIONAL_RULE_REVIEW.md) for engineering work.
If a design decision is required and private access is unavailable, report that
gap rather than inventing a public fallback rulebook.

## Captured configuration boundary

- Validate installed and per-instance resolved rule shapes separately. An absent
  optional policy preserves its documented historical behavior; a malformed
  present policy is not absence. Never merge current defaults into old snapshots
  merely to make a fixture or load succeed.
- Apply default-start replacements only through their captured admission rules.
  Explicit map books/development, saved and crossover contexts remain distinct.
  A prototype class lookup is not proof of captured historical class identity.
- Convert only recognized specialty producers on local hero state; preserve
  unrelated bonuses and prototypes. Some conversions are DEFAULT-only; others
  independently apply to opted-in current PRESET heroes. Do not generalize one
  family's boundary to every specialty.
- Reject unsupported policy/state before writing enclosing hero, settings, map,
  world, crossover or lobby prefixes. Raw decoding and installation must validate
  presence as well as values. Do not silently strip an unrepresentable field.
- Experimental values remain in shipped configuration and captured policies.
  Documentation relocation changes neither those values nor their save identity.
  Regenerate the module through the maintained tool when authored JSON changes.

## Compressed parent-first perk offers

Weighting and progression are independent captured policies. Preserve historical
weight policy1, weight policy2 without a progression marker, and current strict
progression as distinct contexts. Pending queries regenerate and revalidate from
their own captured rules, not installed defaults. Player admission, AI accepted
replies and UI availability must agree without drawing extra world RNG.

The implementation identifiers and compatibility fence are maintained in
[perk offer policy2](developers/NewHorizonsContent.md#perk-offer-policy2).
This section retains the existing fragment used by integration documentation; it
does not reproduce the private probability or balance discussion.

## Acceptance boundary

A declaration, marker, active flag or description is not a working consumer.
Protect atomic rejection, actual accepted actions, read-only detached forecasts,
captured-policy roundtrips and ordinary initialization before claiming coverage.
A historical native result is not a current-build, campaign, graphical, Windows
or balance certificate. New proposals and decisions use the private library;
public evidence stays concise and tied to the behavior actually verified.
