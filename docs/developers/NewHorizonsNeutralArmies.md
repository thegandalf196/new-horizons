# Army size and recruitment growth

The two independent captured settings have these fresh-world defaults:

| Setting | Default | Admitted integers | Identity value |
| --- | ---: | --- | ---: |
| `creatures.neutralArmySizePercent` | 50 | 1–100 | 100 |
| `creatures.recruitmentGrowthPercent` | 50 | 0–1000 | 100 |

The neutral setting scales newly generated encounter armies. Recruitment growth
scales new town and external-dwelling recruitment supply, including generated
initial stock. Neither setting rebases hero armies or existing saved populations.
Their separate capture and generation boundaries are described below.

## Neutral army counts

Each positive stack count becomes `ceil(count * percent / 100)` using wide
integer arithmetic. Empty counts stay zero. Rounding occurs per stack: seven
single-creature stacks remain seven creatures at 50%, while a stack of 43
becomes 22. Species, slots, encounter formation and quantity-label thresholds
remain unchanged; labels describe the resulting actual count.

### Initialization and natural renewal

`newHorizonsNeutralArmies` owns the calculation, owner/hero exclusions, captured
setting validation and initial-world application. The fresh-map initialization
pass captures the effective setting after ordinary object initialization and
then adjusts neutral or unflaggable non-hero armies. This covers authored,
randomized and RMG wandering stacks and armed resource/artifact objects,
rewardable banks/Pandora/events, mines, dwellings, garrisons and neutral towns.
Rewardable guard descriptors and live guards agree; creature rewards and
recruitment stocks are unchanged. Wandering creatures' fractional population
state starts from the adjusted count.

Natural renewal hooks apply the same calculation only to newly generated raw
quantities: rewardable guard resets, dwelling guard rebuilds, neutral-town
reinforcements and monthly wandering population. Ordinary wandering weekly
growth operates on the adjusted population without applying the factor again.
Existing caps and fractional growth remain in force. A reset rebuilds from its
raw configuration, rather than repeatedly halving surviving guards. RNG draws
and their order are unchanged.

Player-owned armies and all heroes, including prisoners, are excluded. The
generic runtime monster/object creation APIs are unchanged: scripts with
explicit authored counts do not silently acquire a size transformation. Only
the ordinary natural-population producer scales its output before calling that
API. Scripted quest/reward armies created after initialization therefore remain
explicitly authored. This is not a multiplier for recruitment, rewards or every
possible scripted source of troops.

### Persistence

The percentage is captured in map settings for subsequent renewals. Loading a
saved world never repeats initialization. Historical absence loads as 100 even
when installed defaults differ. A reduced capture requires
`NEW_HORIZONS_NEUTRAL_ARMY_SIZE`; unsupported writers reject it before direct
settings/map/world/lobby prefixes, and readers reject unsupported reduced
overrides before applying them. Identity captures remain old-format compatible.

## Recruitment growth

`newHorizonsRecruitmentGrowth` owns strict percentage admission, capture,
checked ceiling arithmetic and stock addition. For a nonnegative newly generated
amount, apply `ceil(amount * percent / 100)` once after its ordinary calculation.
At 50%, amounts 1, 7 and 10 become 1, 4 and 5. Zero disables new growth; 100 is
identity; values above 100 amplify new growth, up to 1000. Invalid types, fractions,
negative values and values above 1000 are rejected; arithmetic overflow is an
error rather than a wrapped creature count.

Town weekly growth retains its existing base, buildings, owned dwellings, hero
bonuses and handicap ordering. The server adds applicable calendar bonuses and
applies the existing neutral-town growth reduction before the final percentage.
`GrowthInfo::totalGrowth()` exposes the scaled ordinary subtotal;
`unscaledGrowth()` is for callers that still need to complete that calculation,
including calendar stock generation and prospective AI differences. Such callers
must apply the percentage to the complete amount once, rather than scale an
already scaled display value again.

New first-day town stock follows the ordinary initial base-growth path and then
applies the percentage. The first stock grant when constructing a dwelling and
legacy Portal-generated stock also pass through the same shared calculation.
Upgrading a stocked dwelling preserves its count. External
`normalWeeklyGrowth()` applies ordinary creature-definition bonuses before the
percentage; its existing weekly replacement or accumulation policy then consumes
that scaled amount without applying the configured percentage a second time.

Existing recruitment stock is not multiplied when a world captures or loads the
setting. Ordinary weekly replacement and calendar stock events remain separate:
a double-growth event still doubles old stock, while the complete doubled new
increment is scaled once; Plague still halves old stock. At zero growth, those
existing stock events are not disabled. Ordinary authored/reward creature counts,
hero armies and the neutral-army generation/weekly population rules are not new
recruitment supply and are not scaled by this setting.

The effective percentage is captured at fresh-world initialization; that capture
alone never changes stock. Binary loads normalize historical absence to 100 even
under an installed default of 50. Explicit saved values, including zero, remain
captured. `NEW_HORIZONS_RECRUITMENT_GROWTH` protects any nonidentity capture;
unsupported direct settings, map, world and initialized-lobby writes reject it
before their prefixes, and readers reject unsupported overrides before mutation.
See [Serialization](Serialization.md) for the compatibility contract.

## Verification

Run the focused neutral-army and recruitment-growth tests for admission,
ceiling/overflow arithmetic, initial generation, natural renewal, unchanged
existing stock and armies, ordinary/calendar stock updates, captured absence,
explicit zero, unsupported-format guards and save/load. Include paired RNG/state
checks where the producer already draws randomness. Source or schema validation
does not establish a native pass, rendered stock/label agreement, AI use or save
compatibility. Performance and pacing acceptance require representative gameplay;
a count test does not establish easier creeping or faster combat.
