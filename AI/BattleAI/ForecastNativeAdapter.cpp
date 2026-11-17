/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "../../lib/battle/UnitStateValueExchange.h"
#include "ForecastNativePreparation.h"
#include "ForecastPerkPolicy.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "AttackPreviewHitContext.h"
#include "../../lib/battle/BattleEffectExchange.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "HypotheticBattle.h"
#include "HeroActionAdmission.h"
#include "SearchBudget.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsFocusMagic.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsSpecialtyEvents.h"
#include "../../lib/battle/NewHorizonsSpecialtyRoundEffects.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <limits>
namespace forecast
{
namespace
{
constexpr EventMask ordinary = eventBit(Event::Damage) | eventBit(Event::Healing)
	| eventBit(Event::Move) | eventBit(Event::Activation) | eventBit(Event::Retaliation) | eventBit(Event::Round) | eventBit(Event::Strike) | eventBit(Event::Effect);

bool ordinaryRecord(BonusType type)
{
	switch(type)
	{
	// Static hero spell/adventure facts cannot execute in an ordinary creature
	// exchange plan. HeroResource/effect plans require separate admission.
	case BonusType::MAGIC_SCHOOL_SKILL:
	case BonusType::SPELL_DURATION:
	case BonusType::SPELL:
	// Passive casting facts are owned; casting operations preflight separately.
	case BonusType::CASTS:
	case BonusType::SPELLCASTER:
	case BonusType::RANDOM_SPELLCASTER:
	case BonusType::CREATURE_SPELL_POWER:
	case BonusType::SPECIFIC_SPELL_POWER:
	case BonusType::CREATURE_ENCHANT_POWER:
	case BonusType::SPELLS_OF_LEVEL:
	case BonusType::SPELLS_OF_SCHOOL:
	case BonusType::SPELL_DAMAGE:
	case BonusType::SPECIFIC_SPELL_DAMAGE:
	case BonusType::SPECIAL_SPELL_LEV:
	case BonusType::SPECIAL_PECULIAR_ENCHANT:
	case BonusType::SPELL_DAMAGE_REDUCTION:
	case BonusType::ELEMENTAL_SPELL_DAMAGE:
	case BonusType::ELEMENTAL_SPELL_DAMAGE_RECEIVED:
	case BonusType::MANA_PER_KNOWLEDGE_PERCENTAGE:
	case BonusType::MANA_REGENERATION:
	case BonusType::MANA_PERCENTAGE_REGENERATION:
	case BonusType::SIGHT_RADIUS:
	case BonusType::HERO_EXPERIENCE_GAIN_PERCENT:
	case BonusType::CHANGES_SPELL_COST_FOR_ALLY:
	case BonusType::CHANGES_SPELL_COST_FOR_ENEMY:
	case BonusType::LEADERSHIP:
	case BonusType::PERCENTAGE_DAMAGE_BOOST:
	case BonusType::MORALE:
	case BonusType::LUCK:
	case BonusType::PRIMARY_SKILL:
	case BonusType::STACKS_SPEED:
	case BonusType::STACKS_INITIATIVE_BASE:
	case BonusType::STACKS_INITIATIVE:
	case BonusType::STACKS_INITIATIVE_FLAT:
	case BonusType::STACKS_MOVEMENT_RANGE:
	case BonusType::FLYING_MOVEMENT:
	case BonusType::STACK_HEALTH:
	case BonusType::FREE_SHOOTING:
	case BonusType::FLYING:
	case BonusType::SHOOTER:
	case BonusType::CHARGE_IMMUNITY:
	case BonusType::ADDITIONAL_ATTACK:
	case BonusType::UNLIMITED_RETALIATIONS:
	case BonusType::NO_MELEE_PENALTY:
	case BonusType::JOUSTING:
	case BonusType::HATE:
	case BonusType::KING:
	case BonusType::MAGIC_RESISTANCE:
	case BonusType::LEVEL_SPELL_IMMUNITY:
	case BonusType::NO_WALL_PENALTY:
	case BonusType::NON_LIVING:
	case BonusType::BLOCKS_RETALIATION:
	case BonusType::SPELL_IMMUNITY:
	case BonusType::GENERAL_DAMAGE_PREMY:
	case BonusType::MIND_IMMUNITY:
	case BonusType::UNDEAD:
	case BonusType::ENEMY_DEFENCE_REDUCTION:
	case BonusType::GENERAL_DAMAGE_REDUCTION:
	case BonusType::GENERAL_ATTACK_REDUCTION:
	case BonusType::DEFENSIVE_STANCE:
	case BonusType::LIVING:
	case BonusType::NO_DISTANCE_PENALTY:
	case BonusType::ADDITIONAL_RETALIATION:
	case BonusType::ALWAYS_MINIMUM_DAMAGE:
	case BonusType::ALWAYS_MAXIMUM_DAMAGE:
	case BonusType::NO_LUCK:
	case BonusType::NO_MORALE:
	case BonusType::SHOTS:
	case BonusType::SIEGE_WEAPON: // Only the separately admitted passive Ammo Cart role.
	case BonusType::PASS_THROUGH:
	case BonusType::ASSIMILATION:
	case BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS:
	case BonusType::MORE_DAMAGE_FROM_SPELL:
	case BonusType::LONG_REACH:
	case BonusType::PHYSICAL_DEFENSE:
	case BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS:
	case BonusType::ENEMY_ATTACK_REDUCTION:
	case BonusType::CREATURE_DAMAGE:
		return true;
	default: return false;
	}
}
bool closedLifecycle(const StackWithBonuses & unit, const forecastEffects::State & effects)
{
	// Raw capability (not effective-list absence) protects dormant sources too.
	if(unit.health.isCapacityHealthTracking()) return false;
	for(const auto type : {BonusType::COMBAT_EVENT_TRIGGER, BonusType::REBIRTH,
		BonusType::DISINTEGRATE, BonusType::CREATURE_ABILITY_SUPPRESSION,
		BonusType::HYPNOTIZED, BonusType::CONFUSION_PENDING, BonusType::GUARDIAN_SPIRIT,
		BonusType::HP_REGENERATION, BonusType::NOT_ACTIVE,
		BonusType::ATTACKS_NEAREST_CREATURE})
		if(unit.mayHaveBonusType(type)) return false;
	const auto raw = unit.getUnstackedBonuses(Selector::all);
	for(const auto & bonus : *raw)
	{
		if(!bonus || arcaneInspiration::isEffect(bonus.get())
			|| newHorizonsOffense::isNoQuarterBonus(bonus.get())) return false;
		const bool admitted = std::ranges::any_of(effects.values(), [&](const auto & record)
		{ return battle::exactBattleEffectEqual(*record.value, *bonus); });
		if(!admitted && (!ordinaryRecord(bonus->type)
			|| (bonus->duration != BonusDuration::PERMANENT && bonus->duration != BonusDuration::ONE_BATTLE))) return false;
	}
	return !unit.isClone() && !unit.isGhost() && !unit.ghostPending && !unit.summoned && !unit.hasBattleFormState()
		&& unit.getPhantomIntegrity() == 0 && unit.guardianSpiritHitPoints == 0
		&& unit.regenerationRateMillionths == 0 && unit.regenerationPendingMicroHealth == 0
		&& unit.pursuitMovementRemaining == 0 && unit.rangedFollowUpDamagePercent == 0
		&& !unit.heroicSpiritRetaliation && !unit.heroicSpiritMoralePending
		&& !(unit.physicalPoisonBaseDamage > 0 && unit.physicalPoisonActivationsRemaining > 0)
		&& !unit.dreadCavalryState.pending && !unit.luckyOwnAttackSequence;
}
Counter counter(const battle::CAmmo & value)
{
	return {value.available(), value.total(), value.isLimited()};
}
UnitInput captureUnit(const StackWithBonuses & unit, const HypotheticBattle & source,
	std::shared_ptr<const forecastEffects::State> effects)
{
	auto definition = std::make_shared<UnitDefinition>();
	definition->id = unit.unitId();
	definition->creature = unit.creatureId();
	definition->originalOwner = unit.unitOwner();
	definition->side = unit.unitSide();
	definition->baseAmount = unit.unitBaseAmount();
	definition->maximumHealth = unit.getMaxHealth();
	definition->movement = unit.getMovementRange();
	definition->initiative = unit.getInitiative();
	definition->doubleWide = unit.doubleWide();
	definition->supported = ordinary;
	const auto leadership = source.battleLeadershipReadout(&unit);
	definition->leadership = {true, leadership.active, leadership.effectiveLeadership,
		leadership.requirement, leadership.capacity};
	const auto controllingSide = source.playerToSide(source.battleGetOwner(&unit));
	if(controllingSide == BattleSide::ATTACKER || controllingSide == BattleSide::DEFENDER)
	{
		auto & context = definition->leadership;
		context.controllingSide = controllingSide;
		context.composition = source.getLeadershipComposition(controllingSide);
		const auto policy = source.battleGetVisibleHeroPolicy(controllingSide);
		if(policy.kind() == VisibleHeroPolicy::Knowledge::VISIBLE_HERO)
		{
			const auto * hero = policy.legacyHero();
			context.hero = hero->id;
			context.heroLevel = hero->level;
			context.heroTreeVersion = hero->getTreeVersion();
			context.capabilityOwner = hero->getCapturedCapabilityRules();
			context.perkOwner = hero->getPerkState().capturedOwner();
		}
	}
	for(size_t mode = 0; mode < 2; ++mode)
	{
		auto facts = nativeDamage::prepareUnit(unit, mode != 0);
		// Pair-special stat families retain shared native calculation, while
		// their ordinary HP/move/retaliation state still uses the compact model.
		if(facts)
		{
			facts->leadershipEfficiency = leadership.efficiency;
			definition->numeric[mode] = std::make_shared<const nativeDamage::PreparedUnit>(std::move(*facts));
		}
	}
	UnitDelta value(battle::CHealth::detached(unit.health, unit.health.valueContext()));
	value.numeric = definition->numeric;
	value.focus = NativeAdapter::captureFocus(unit);
	value.effects = std::move(effects);
	value.movement = definition->movement;
	value.initiative = definition->initiative;
	value.position = unit.getPosition();
	value.facing = unit.unitSide() == BattleSide::ATTACKER ? Facing::Right : Facing::Left;
	value.controller = source.battleGetActionController(&unit);
	// CRetaliations::total latches its observed allowance. Observe on a detached
	// copy so rejected root capture cannot change an existing target/observer.
	const auto resources = unit.acquireState();
	value.activation = {unit.movedThisRound, unit.waitedThisTurn, unit.waiting,
		unit.defending, unit.hadMorale, source.getActivationSerial(), 0,
		counter(resources->shots), counter(resources->counterAttacks), counter(resources->casts)};
	value.activation.waitBonusUsed = unit.battlecraftWaitBonusUsed;
	value.activation.waitMasteryDoubled = unit.battlecraftWaitMasteryDoubled;
	value.veteranPhysicalDamage = unit.veteranPhysicalDamageSinceActivation;
	value.warfareStat = source.unitWarfareStatBonus(&unit);
	// Cold native collection observes the actual updater/limiter execution
	// order. Its owning recipe, not a borrowed provider, survives this capture.
	definition->modifiers = unit.captureModifierFacts(nullptr);
	if(!definition->modifiers || !definition->modifiers->canRefresh(bonusDependencies::Change::Count))
		throw std::invalid_argument("Unsupported compact modifier dependency closure");
	value.currentModifiers = definition->modifiers;
	if(value.numeric[0] && value.numeric[1])
	{
		const std::array<nativeDamage::PreparedUnit, 2> base{*value.numeric[0], *value.numeric[1]};
		auto frame = value.currentModifiers->refresh(value.health.getCount(), bonusDependencies::Change::None,
			base, value.warfareStat);
		if(!frame) throw std::invalid_argument("Unsupported compact native modifier reduction");
		value.modifierNumbers = std::make_shared<const bonusDependencies::NumericFrame>(std::move(*frame));
		for(size_t mode = 0; mode < 2; ++mode)
			value.numeric[mode] = std::make_shared<const nativeDamage::PreparedUnit>(value.modifierNumbers->prepared[mode]);
		definition->numeric = value.numeric;
	}
	return {std::move(definition), std::move(value)};
}
}
Admission NativeAdapter::capture(HypotheticBattle & source, EventMask entirePlan)
{
	if(entirePlan & ~ordinary) return {{}, AdmissionFailure::UnsupportedEvents};
	try
	{
		const auto roster = source.battleGetAllUnits(false);
		if(roster.size() > 4096) return {{}, AdmissionFailure::Source};
		// Current live attack predicates cannot certify a dormant corpse's future
		// native specialty. Ordinary no-healing plans may retain such corpses;
		// resurrection plans need a separate potential-role proof BEFORE mutation.
		if((entirePlan & eventBit(Event::Healing)) && std::ranges::any_of(roster,
			[](const battle::Unit * unit) { return !unit->alive(); }))
			return {{}, AdmissionFailure::UnitLifecycle};
		if(source.searchBudget())
		{
			const uint64_t count = roster.size();
			const uint64_t work = count * (count + 1);
			if(work > std::numeric_limits<uint32_t>::max())
				throw battleSearch::BudgetExhausted(battleSearch::WorkKind::Node);
			source.searchBudget()->require(battleSearch::WorkKind::Node, static_cast<uint32_t>(work));
		}
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto & owned = source.forecastSide(side);
			if(!owned || !owned->magicOwner || owned->magicOwner != source.getCapturedMagicRules()
				|| owned->knowledge == VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN
				|| owned->knowledge == VisibleHeroPolicy::Knowledge::UNAVAILABLE)
				return {{}, AdmissionFailure::Source};
			if(owned->hasVisibleHero())
			{
				// Initial finite hero coverage: immutable native passives, no acquired
				// perk can create an unrepresented strike/activation reaction.
				const auto policy = source.battleGetVisibleHeroPolicy(side);
				const auto * hero = policy.legacyHero();
				if(!hero || !ordinaryPerksSupported(*hero)
					|| !ordinaryOrdersSupported(source, side)) return {{}, AdmissionFailure::UnitLifecycle};
			}
		}
		if(roster.size() < 2 || roster.size() > 4096 || source.getDefendedTown()) return {{}, AdmissionFailure::Source};
		if(source.searchBudget()) source.searchBudget()->require(battleSearch::WorkKind::Node, static_cast<uint32_t>(roster.size()));
		heroActionProjection::CanonicalSourceAdmission proof(*roster.front(), source);
		if(!proof.admits(*roster.back())) return {{}, AdmissionFailure::Source};
		NativePreparation captureRead(source);
		if(!effectClockOwnersSupported(source)) return {{}, AdmissionFailure::Source};
		std::vector<UnitInput> inputs;
		inputs.reserve(roster.size());
		for(const auto * entry : roster)
		{
			const auto unit = captureRead.unit(entry->unitId());
			if(!unit || (!newHorizonsCombatSkills::isOrdinaryCreatureAttacker(unit.get()) && !unit->isAmmoCart())) return {{}, AdmissionFailure::UnitLifecycle};
			auto effects = captureEffects(*unit, source.battleGetRound());
			if(!closedLifecycle(*unit, *effects)) return {{}, AdmissionFailure::UnitLifecycle};
			if(source.unitHasVeteranCohesion(unit.get()) || source.unitBloodragePainIncrement(unit.get()) != 0
				|| unit->getMaxHealth() > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
				return {{}, AdmissionFailure::DynamicHealth};
			if(newHorizonsSpecialtyEvents::needsRoundEffects(source, *unit,
				std::numeric_limits<int32_t>::max())) return {{}, AdmissionFailure::UnitLifecycle};
			// Freezing Touch remains the existing probabilistic control value, never
			// a guaranteed status write. Accepted Frozen expiry/shatter uses its
			// separately owned native effect receipts.
			for(const auto * target : roster)
			{
					if(newHorizonsSpecialtyEvents::needsStrikeEvents(source, unit.get(), target))
					return {{}, AdmissionFailure::UnitLifecycle};
				for(const bool ranged : {false, true})
				{
					if(ranged && !unit->isShooter()) continue;
					BattleAttackInfo attack(unit.get(), target, 0, ranged);
					const auto riders = AttackPreviewHitContext::primaryStatusAdmission(source, attack);
					if(riders.manaLeeches || riders.serpentBinding || riders.soulHarvest || riders.primaryRider)
						return {{}, AdmissionFailure::UnitLifecycle};
				}

			}
			inputs.push_back(captureUnit(*unit, source, std::move(effects)));
		}
		SourceIdentity identity{source.getBattleID(), source.battleGetMySide() == BattleSide::ALL_KNOWING
			? PlayerColor::CANNOT_DETERMINE : source.sideToPlayer(source.battleGetMySide()),
			source.battleGetRound(), source.getActivationSerial(), source.getCapturedMagicRules()};
		bindRootReadViews(source, inputs);
		auto captured = State::capture(identity, std::move(inputs), entirePlan);
		captureRead.accept({}); // pure capture must not invalidate a live observation
		return {std::move(captured), AdmissionFailure::None};
	}
	catch(const battleSearch::BudgetExhausted &) { throw; }
	catch(const std::exception &) { return {{}, AdmissionFailure::Source}; }
}

std::optional<MovePermit> NativeAdapter::prepareMove(const State & state, UnitId id, BattleHex to,
	const HypotheticBattle & source, battleSearch::SearchBudget * budget)
{
	if(!source.forecastState() || !source.forecastState()->sameSnapshot(state)) return {};
	if(source.battleGetAllUnits(false).size() != state.ids().size()
		|| !state.supports(Event::Move) || !state.unit(id) || source.getBattleID() != state.source().battle
		|| source.battleGetRound() != state.currentRound()) return {};
	if(budget && !budget->consume(battleSearch::WorkKind::Movement)) return {};
	// The existing solver, not this state model, decides paths and terrain.
	// Every compact publication must already be visible in the compatibility
	// branch before another solver proof can be issued.
	for(const auto other : state.ids())
	{
		const auto * unit = source.battleGetUnitByID(other);
		const auto * value = state.unit(other);
		if(!unit || unit->getPosition() != value->position
			|| unit->getAvailableHealth() != value->health.available()
			|| source.battleGetActionController(unit) != value->controller) return {};
	}
	const auto * actor = source.battleGetUnitByID(id);
	const auto reachability = source.getReachability(actor);
	if(!source.battleGetAvailableHexes(reachability, actor, true).contains(to)) return {};
	return MovePermit(state.source(), state.layer, state.revision(), id, actor->getPosition(), to);
}

bool NativeAdapter::publish(const State & state, UnitId id, HypotheticBattle & branch, StackWithBonuses & target)
{
	const auto * definition = state.definition(id);
	const auto * value = state.unit(id);
	if(!definition || !value || target.unitId() != id || branch.battleGetUnitByID(id) != &target
		|| !target.usesStateEnvironment(*branch.unitEnvironment())
		|| branch.getBattleID() != state.source().battle || branch.battleGetRound() != state.currentRound()
		|| target.health.valueContext() != value->health.valueContext()) return false;
	// Membership can alter the allowance without consuming a retaliation.
	// Preserve native used-count provenance rather than comparing availability
	// from two different totals (for example a lost Order extra retaliation).
	// Retaliation total() latches allowance. All observations and native use
	// happen on an owning scratch; a refused preparation leaves target values intact.
	auto preparedValues = target.acquireState();
	const auto observed = preparedValues->counterAttacks.observeWithoutLatch();
	const int64_t currentUsed = static_cast<int64_t>(observed.total) - observed.available;
	const auto & prepared = value->activation.retaliation;
	const int64_t preparedUsed = static_cast<int64_t>(prepared.total) - prepared.available;
	const int64_t retaliation = preparedUsed - currentUsed;
	if(retaliation < 0 || retaliation > std::numeric_limits<int32_t>::max()
		|| (prepared.limited && retaliation > observed.available)) return false;
	auto stagedHealth = value->health; // Allocate all health-ledger copies FIRST.
	if(retaliation) preparedValues->counterAttacks.use(static_cast<int32_t>(retaliation));
	preparedValues->health.swapPreparedContents(stagedHealth);
	preparedValues->setPosition(value->position);
	preparedValues->movedThisRound = value->activation.moved;
	preparedValues->waiting = value->activation.waiting;
	preparedValues->waitedThisTurn = value->activation.waited;
	preparedValues->defending = value->activation.defending;
	preparedValues->hadMorale = value->activation.hadMorale;
	preparedValues->veteranPhysicalDamageSinceActivation = value->veteranPhysicalDamage;
	preparedValues->battlecraftWaitBonusUsed = value->activation.waitBonusUsed;
	preparedValues->battlecraftWaitMasteryDoubled = value->activation.waitMasteryDoubled;
	const auto finalObserved = preparedValues->counterAttacks.observeWithoutLatch();
	if(static_cast<int64_t>(finalObserved.total) - finalObserved.available != preparedUsed)
		return false;
	battle::exchangePreparedUnitStateValues(target, *preparedValues);
	return true;
}
}
