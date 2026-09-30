# New Horizons version policy

The live New Horizons product/module version has one authoring value:
[`config/newHorizonsVersion.json`](../config/newHorizonsVersion.json). The main
menu reads that root config through VCMI's built-in `JsonPath` filesystem, and
the live module generator reads the same file. The current source checkpoint is
`0.15.0`: Shield of Chaos and Paradox Shield have production runtime/AI paths,
successful Linux builds and focused native verification. This is not a completed
1.0 release or evidence that the selected playable snapshot has been updated.

The version is independent of the upstream VCMI engine version, save-schema or
serialization versions, and the exact Git revision. Change save versions only
when save compatibility requires it. Every package or test result should
identify its full source commit hash. A Windows and Linux package can be called
the same source release only when their recorded source identities match.

While New Horizons is pre-1.0, use `0.MINOR.PATCH`:

- Increase `MINOR` for a meaningful product feature checkpoint that is ready
  to be represented as part of the player-facing edition. Reset `PATCH` to zero.
- Increase `PATCH` for bug fixes, compatibility corrections, or packaging fixes
  that do not add a product capability.
- Do not interpret either component as a feature-completion percentage. `1.0`
  is reserved for the explicitly accepted Version 1.0 scope and delivery gates;
  the version value alone does not establish that they have passed.

The main menu shows `New Horizons <version>` at the bottom-left of the menu
background in the native small yellow font. Its anchor and width are derived
from the loaded background dimensions, so the label stays within that surface
when the configured menu background changes size. It is part of the shared menu
screen and remains visible across the main menu's tabs.

The older `0.7.0` value was introduced by commit `a035100a2` alongside the
canonical saved registry of 31 Skills and ten-perk pools. The commit described
that registry as planned data whose selection and effects still needed runtime
systems. It is therefore evidence of a module/content identity change, not
evidence that the project was 70% complete. Commit `9e675d976` then raised the
module version to `0.8.0` for the Tower Mage/Genie dwelling roster swap, noting
that old saves must not silently reinterpret the changed roster. The current
`0.14.0` value was introduced in `fd0f03156` with selectively reviewed hero
biography overrides. These historical module bumps track changes to the
curated content identity; they were not a completion scale.

Diagnostic generator outputs retain their historical isolated identities:
hero preview `0.3.0`, capability preview/control `0.4.0`, and mastery preview
`0.5.0`. They are test candidates, not live product releases, and do not adopt or
replace the authoritative product version.
