# Neutral army size trial

`creatures.neutralArmySizePercent` is an opt-in new-world trial setting. Its
default is **100**, preserving ordinary counts; **50** requests half-size
neutral armies. Admission requires an integer from 1 through 100. No balance
conclusion or change to the default follows from exposing this control.

Each positive stack count becomes `ceil(count * percent / 100)` using wide
integer arithmetic. Empty counts stay zero. Rounding occurs per stack: seven
single-creature stacks remain seven creatures at 50%, while a stack of 43
becomes 22. Species, slots, encounter formation and quantity-label thresholds
remain unchanged; labels describe the resulting actual count.

## Initialization and natural renewal

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

## Persistence and verification

The percentage is captured in map settings for subsequent renewals. Loading a
saved world never repeats initialization. Historical absence loads as 100 even
when installed defaults differ. A reduced capture requires
`NEW_HORIZONS_NEUTRAL_ARMY_SIZE`; unsupported writers reject it before direct
settings/map/world/lobby prefixes, and readers reject unsupported reduced
overrides before applying them. Identity captures remain old-format compatible.

Focused native tests cover arithmetic/admission, historical absence, enclosing
serialization guards, initial object categories, weekly fractional growth,
natural resets/population/reinforcements, runtime authored creation and paired
RNG states. Executing these tests is required before using the trial candidate.
Performance and pacing acceptance additionally require measured representative
games; a count test does not establish easier creeping or faster combat.
