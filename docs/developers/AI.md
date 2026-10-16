# AI

There are two types of AI: adventure and battle.

**Adventure AIs** are responsible for moving heroes across the map and developing towns  
**Battle AIs** are responsible for fighting, i.e. moving stacks on the battlefield  

We have 3 battle AIs so far:

* BattleAI - strongest
* StupidAI - for neutrals, should be simple so that experienced players can abuse it
* Empty AI - should do nothing at all. If needed another battle AI can be introduced.  

Each battle AI consist of a few classes, but the main class, kind of entry point usually has the same name as the package itself. In BattleAI it is the BattleAI class. It implements some battle specific interface, do not remember. Main method there is `activeStack(battle::Unit * stack)`. It is invoked by the system when it's time to move your stack. The thing you use to interact with the game and receive the gamestate is usually referenced in the code as `cb`. `CPlayerSpecificCallback` it should be. It has a lot of methods and can do anything. For instance it has battleGetUnitsIf(), which returns all units on the battlefield matching some lambda condition.
Each side in a battle is represented by an `CArmedInstance` object. `CHeroInstance` and `CGDwelling`, `CGMonster` and more are subclasses of `CArmedInstance`. `CArmedInstance` contains a set of stacks. When the battle starts, these stacks are converted to battle stacks. Usually Battle AIs reference them using the interface `battle::Unit *`.
Units have bonuses. Nearly everything aspect of a unit is configured in the form of bonuses. Attack, defense, health, retaliation, shooter or not, initial count of shots and so on.
When you call `unit->getAttack()` it summarizes all these bonuses and returns the resulting value.  

One important class is `HypotheticBattle`. It is used to evaluate the effects of an action without changing the actual gamestate. It is a wrapper around `CPlayerSpecificCallback` or another `HypotheticBattle` so it can provide you data, Internally it has a set of modified unit states and intercepts some calls to underlying callback and returns these internal states instead. These states in turn are wrappers around original units and contain modified bonuses (`CStackWithBonuses`). So if you need to emulate an attack you can call `hypotheticbattle.getforupdate()` and it will return the `CStackWithBonuses` which you can safely change.  

## BattleAI

### Detached forecast integrity

Use current allegiance/controller and caster perspective where authority requires
them; original side is not a substitute for current hostility. Preserve exact
creature-target IDs through enumeration, validation and reacquisition. A missing
ID must not select another unit sharing its hex; neutral location targets retain
their separate contract.

Position, alive/ghost state, clone backlinks, visible obstacles and wall/gate
changes must invalidate corresponding projected geometry and caches. Reserve
inherited live/ghost unit identities without silently changing legacy allocation.
Keep live/parent/sibling branches isolated and capture branch-owned effect values
before an ancestor expires. Age only effects owned by the modeled clock; the
source-specific timed-arrival limits below still apply.

Merge pending effect updates before selector filtering. Preserve distinct equal
bonuses, source/identity/value-type distinctions and ordered add/refresh/remove
behavior. Copy actual affected shot/retaliation consumption while retaining each
resource cache's owner binding. Per-strike eligibility and later exchange loops
must agree with authority; a direct-helper test does not prove that the production
caller supplies the required sequence information. Temporary-unit healing/damage
scoring needs explicit Boolean predicates, not signed flags used as truth values.

Forecast scripted post-strike state through an explicit deterministic bridge for
the required mutation, not blanket dispatch of combat scripts that may cast,
damage, heal, animate or log. Apply marks after the resolved hit, never during a
read-only target probe. Retain the owning hypothetical battle for borrowed unit
and bonus state, and preserve the pre-effect damage baseline while invalidating
current marked-damage caches on removal or expiry.

Compare actual health, actions, resources and RNG; AI scores are not physical hit
counts. Retain the legal, meaningful AI/server controls and fixture boundaries
described below rather than forcing a chosen action through invented setup.

### Current-rule spell fixtures

Current weighted-perk capture requires active hero development rules and their
class-weight table. Do not disable Commands while retaining that current capture:
expanded hero ratings also require Commands. To isolate spell valuation, keep the
current rule pair and neutralize command effects in the fixture. Historical tests
must explicitly preserve their matching captured context instead of weakening
production validation or silently converting all fixtures to legacy rules.

Retired hero spells need separate availability and historical-behavior fixtures.
For example, current Sacrifice has `heroAccess=false`; an inscribed spell must
still be rejected under that captured roster. A historical saved-v3 fixture may
explicitly omit that marker to exercise its older default-access behavior. Keep
the override local to the captured fixture, verify unrelated rows are unchanged,
and retain a current-roster rejection control. Do not reactivate canonical data
merely to preserve a positive AI test.

Metamagic continuation tests must establish a typed spell allowance before AI
evaluation. Legacy pending counters alone do not grant it in a Commands battle.
Prefer accepted paid triggering casts and assert the resulting grant's source,
legality and payment. Check that preview preserves the live allowance ledger,
mana, serialized unit state and RNG; validate submitted follow-ups through the
authoritative action path. Perk setup must retain Basic/Advanced prerequisites.
These fixture grants do not prove ordinary class-weighted perk acquisition.

Choose damage witnesses for their actual current semantics. Implosion uses a
percentage of current aggregate Health with a final floor, so a one-HP target
cannot establish positive damage credit. A generic flat-damage valuation test
should use a current flat-damage spell, retaining meaningful victim, summon,
partial-damage and read-only assertions rather than changing expected results.

### New Horizons timed arrival snapshots

`HypotheticBattle` branches must retain the timer state present when they are
constructed, even if the underlying battle or another branch subsequently
advances. Infernal Beacon and Reserve Discipline attach unit-local
`HERO_SPECIAL` / `N_TURNS` bonuses when a gated creature arrives. These bonuses
belong to the forecast's captured effects and age on its existing round clock.
Inactive recipients must also be captured at branch construction; waiting until
their first query would import a later parent's expiry.

This classification is deliberately limited to those timed hero-special effects,
not every timed bonus. Inherited bonuses may have a different owning clock.
Future timed `HERO_SPECIAL` producers must be checked against that ownership
assumption. Permanent specialties, Spell Lock preservation and activation-clock
effects retain their existing paths. No save format or live battle rule changes
are required for this forecast correction.

The four real-arrival regressions in `NewHorizonsDemonicGatingTest` cover expiry,
parent/sibling isolation and first reads after parent or live advancement.

BattleAI's most important classes are the following:

- AttackPossibility - one particular way to attack on the battlefield. Each AttackPossibility instance has multiple ...DamageReduce attributes. These represent how much damage an enemy will lose after our attack. Effects can reduce this damage. We add them up and this value is used as attack score.

- PotentialTargets - a set of all AttackPossibility instances

- BattleExchangeVariant - it is an extension of AttackPossibility, a result of a set of units attacking each other for a fixed number of turns according to the turn order. It is kind of an oversimplified battle simulation. A set of units is restricted according to AttackPossibility which particular exchange extends. Exchanges can be waited (when stacks/units wait for a better time to attack) and non-waited (when stack acts right away). For non-waited exchanges the first attack score is taken from AttackPossibility (together with various effects like 2 hex breath, shooters blocking and so on). All the other attacks are simplified, only respect retaliations. At the end we have a final score.

- BattleExchangeEvaluator - calculates all possible BattleExchangeVariants and selects the best

- BattleEvaluator - is a top level logic layer which also adds spellcasts and movement to unreachable targets

BattleAI itself handles all the rest and issues actual commands

## Nullkiller AI

Adventure AI responsible for moving heroes on map, gathering things, developing town. Main idea is to gather all possible tasks on map, prioritize them and select the best one for each heroes. Initially was a fork of VCAI

### R2 perk development

For new strict progression, Nullkiller checks offered perks through the shared
`canAcquirePerk` rule and retains its faction, tactical and value preferences.
An occupied tier cannot supply another legal choice, so no capacity-reservation
heuristic runs for these worlds. Weight policy2 and preference ratio `R = 2.0`
control parent probabilities, not tier legality.

Only historical captured policy2 without the strict progression marker first
prefers offered perks that do not
increase the shortage of slots needed for the parent's remaining active tiers.
The calculation uses the hero's saved registry, selected tiers and per-Skill cap;
it does not change eligibility, class weights or offer generation. Existing
faction/value preferences then rank the capacity-preserving candidates. If only
capacity-consuming perks compete with a Skill choice, the existing Skill
evaluator decides. Other historical perk policies retain their previous preferences.

If no Skill or capacity-preserving perk is offered, the AI still selects a legal
offered perk. This is a preference, not a guarantee of reaching every tier:
forced offers, class restrictions and earlier investments can prevent that.
Faction-path fixtures must isolate both learned Skills and authored starting
perks while retaining the captured rules. Historical duplicate-tier tests must
explicitly capture that old policy; current strict tests must reject a second
Basic, Advanced or Expert choice for the same Skill.

With Nullkiller2 enabled, run
`vcmitest --gtest_filter='NewHorizonsPerkAIJourneyTest.*' --gtest_output=xml:journeys.xml`
for the bounded sequential-offer pilot. It compares policy2 ratios1/1.5/2/3
for canonical Solmyr/Christian starting Skills, perks and class weights, using
seed17 and synthetic next-level XP through level20. XML properties retain offers,
accepted replies, development snapshots and initial/final RNG state. The fixture
uses ten Pikemen, disables Masteries and creates a fresh AI interface per level;
it is not a campaign, persistent-AI-memory test or estimate of population odds.
Different ratios can legitimately produce different downstream RNG histories.

### Adventure route integrity

Keep adventure path storage bounded. Admission or reclamation changes must preserve
queued/predecessor pointer identity, cheaper re-relaxation, required battle actions
and deterministic route selection under saturation. An `UNKNOWN` action does not
prove that a node is unreferenced or safe to recycle; allocation-limit warnings
alone justify neither increasing capacity nor discarding route alternatives.
Pair pool tests with saturated route-reconstruction controls.

### Parts

Gateway - a callback for server used to invoke AI actions when server thinks it is time to do something. Through this callback AI is informed about various events like hero level up, tile revialed, blocking dialogs and so on. In order to do this Gaateway implements specific interface. The interface is exactly the same for human and AI
Another important actor for server interaction is CCallback * cb. This one is used to retrieve gamestate information and ask server to do things like hero moving, spell casting and so on. Each AI has own instance of Gateway and it is a root object which holds all AI state. Gateway has an event method yourTurn which invokes makeTurn in another thread. The last passes control to Nullkiller engine.

Nullkiller engine - place where actual AI logic is organized. It contains a main loop for gathering and prioritizing things. Its algorithm:

* reset AI state, it avoids keeping some memory about the game in general to reduce amount of things serialized into savefile state. The only serialized things are in nullkiller->memory. This helps reducing save incompatibility. It should be mostly enough for AI to analyze data avaialble in CCallback
* main loop, loop iteration is called a pass
  * update AI state, some state is lazy and updates once per day to avoid performance hit, some state is recalculated each loop iteration. At this stage analysers and pathfidner work
  * gathering goals, prioritizing and decomposing them
  * execute selected best goals

Analyzer - a module gathering data from CCallback *. Its goal to make some statistics and avoid making any significant decissions.

* HeroAnalyser - decides upong which hero suits better to be main (army carrier and fighter) and which is better to be a scout (gathering unguarded resources, exploring)
* BuildAnalyzer - prepares information on what we can build in our towns, and what resources we need to do this
* DangerHitMapAnalyser - checks if enemy hero can rich each tile, how fast and what is their army strangth
* Pathfinder - core thing used to calculate paths including bypassing monsters, quests, using advmap spells
* Graph - experimental thing connecting all objects into a network by paths (using common hero characteristics), does not work without maphack, allows simplified faster paths calculation. Possibly can be used for something else
* ArmyManager - for now only helps calculating best army from two CreatureSet objects. Later may be responsible for forming ideal army for each main hero so that we know what we need to build and buy.
* ObjectClusterizer - aggregates all objects into clusters depending on which object blocks way towards them.
* DeepDecomposer - sometimes pathfinder may return path through some object which canno be simply bypassed but instead it requires something to be done first. DeepDecomposer allows to detalizing such paths. Examples: building a boat requires capturing shipyard, bypassing bordergate requires visiting masterkey tent. See AbstractGoal
* FuzzyEngines - looks like some legacy from VCAI

### Goals

Units of activity in AI. Can be AbstractGoal, Task, Marker and Behavior

Task - simple thing which can be done right away in order to gain some reward. Or a composition of simple things in case if more than one action is needed to gain the reward.

* AdventureSpellCast - town portal, water walk, air walk, summon boat
* BuildBoat - builds a boat in a specific shipyard
* BuildThis - builds a building in a specified town
* BuyArmy - buys specific amount of army in AIValue in specific town
* DigAtTile - for grail, not implemented yet
* DismissHero - sometimes we may want to get rid of some scout
* ExchangeSwapTownHeroes - puts specifc hero in garrison or extracts hero from garrison. Also makes possible upgrades and buys army in town (used by defence and startup behaviors)
* ExecuteHeroChain - moves hero accross some path (or a few heroes forming a chain in order to move army to the target hero), can bypass simple obstacles like monsters, garrisons
* ExploreNeighborTile - after AI visits initial tile for exploration - makes a few sequential explorations of nearby tiles to save some performance
* RecruitHero - recruits specific hero in specifc town
* SaveResources - locks some resources for later use during next day
* StayAtTown - stay at town for the rest of the day (to regain mana)

Behavior - a core game activity

* CaptureObjectsBehavior - generally it is about visiting map objects which give reward. It can capture any object, even those which are behind monsters and so on. But due to performance considerations it is not allowed to handle monsters and quests now.
* ClusterBehavior - uses information of ObjectClusterizer to unblock objects hidden behind various blockers. It kills guards, completes quests, captures garrisons.
* BuildingBehavior - develops our towns
* BuyArmyBehavior - buys army in towns
* GatherArmyBehavior - picks army from towns and brings it to main hero by scout, or main itslef goes for it
* RecruitHeroBehavior - recruits hero it it is either stronger than any main or there is something to gather
* StartupBehavior - scripted behavior which helps a bit on the first day. It keeps main hero in town garrison and accumulates army from initial heroes bought in tavern
* StayAtTownBehavior - stay at town to gain mana from mage guild
* DefenceBehavior - defend towns by eliminating treatening heroes or hiding in town garrison

AbstractGoal - some goals can not be completed because it is not clear how to do this. They express desire to do something, not exact plan. DeepDecomposer is used to refine such goals until they are turned into such plan or discarded. Some examples:

* CaptureObject - you need to visit some object (flag a shipyard for instance) but do not know how
* CompleteQuest - you need to bypass bordergate or borderguard or questguard but do not know how
AbstractGoal usually comes in form of composition with some elementar task blocked by abstract objective. For instance CaptureObject(Shipyard), ExecuteHeroChain(visit x, build boat, visit enemy town). When such composition is decomposed it can turn into either a pair of herochains or into another abstract composition if path to shipyard is also blocked with something.
Sometimes such decomposition may form a loop of abstract goals and will be discarded in such case. Generally the current architecture attempts to avoid decomposition as quite a heavy operation.

Composition - a goal which can be both elementar (a set of tasks) or abstract (contains unresolved abstract goal at the end). Compositions express a chain of tasks in order to achieve some reward. They consist of sequences. Each sequence is a vector of goals. Only last sequence is actually executed or decomposed. All the rest adds value to reward evaluator.

Marker - a goal used to just add value (reward) into some composition. We want to capture some shipyard not just because but in order to capture a town (or something else) later. Thus when we are capturing a shipyard we should know that later we will unlock town so we contribute towards town reward as well.
