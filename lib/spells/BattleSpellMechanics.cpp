/*
 * BattleSpellMechanics.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "BattleSpellMechanics.h"
#include "NewHorizonsSpellcraft.h"

#include "Problem.h"
#include "CSpell.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsCrossSchoolFormula.h"
#include "NewHorizonsBlink.h"
#include "NewHorizonsNaturesWrath.h"
#include "NewHorizonsPandemonium.h"
#include "NewHorizonsRealityWarp.h"
#include "NewHorizonsSorcery.h"
#include "NewHorizonsVengefulVines.h"
#include "NewHorizonsOverwhelmingFormula.h"
#include "TargetCondition.h"

#include "../battle/IBattleState.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/CUnitState.h"
#include "../battle/NewHorizonsDivineMandate.h"
#include "../battle/NewHorizonsFrozen.h"
#include "../battle/NewHorizonsSoulChain.h"
#include "../battle/NewHorizonsPuppetMaster.h"
#include "../battle/NewHorizonsWarcasting.h"
#include "../battle/Unit.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/Updaters.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../networkPacks/SetStackEffect.h"
#include "../ScopeGuard.h"
#include "../CStack.h"

#include <vstd/RNG.h>

namespace spells
{

namespace
{

constexpr std::string_view NEW_HORIZONS_SUMMON_TROLLS_SPELL = "new-horizons:summonTrolls";
constexpr std::string_view NEW_HORIZONS_VERDANT_PRISON_SPELL = "new-horizons:verdantPrison";
constexpr std::string_view NEW_HORIZONS_HYDRAS_VITALITY_SPELL = "new-horizons:hydrasVitality";

bool hasCounterpressurePerk(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
		std::string(newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE));
}

bool isLivingCureTarget(const battle::Unit * unit)
{
	return unit && unit->isValidTarget(false) && unit->alive()
		&& !unit->hasBonusOfType(BonusType::UNDEAD)
		&& !unit->hasBonusOfType(BonusType::NON_LIVING)
		&& !unit->hasBonusOfType(BonusType::MECHANICAL)
		&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
}

bool isLivingPhysicalPoisonTarget(const battle::Unit * unit)
{
	return isLivingCureTarget(unit);
}

SpellID newHorizonsRegenerationSpellId()
{
	static const SpellID regenerationSpell(SpellID::decode(std::string(newHorizonsMagic::NATURE_REGENERATION_SPELL)));
	return regenerationSpell;
}

bool isNewHorizonsRegenerationSpell(const CSpell * spell, const JsonNode & savedRules)
{
	return spell && newHorizonsMagic::spellVariantBase(savedRules, spell->getId()) == newHorizonsRegenerationSpellId()
		&& newHorizonsMagic::spellAllowedBySavedRoster(savedRules, spell->getId());
}

bool isNewHorizonsMassRegenerationSpell(const CSpell * spell, const JsonNode & savedRules)
{
	return isNewHorizonsRegenerationSpell(spell, savedRules)
		&& newHorizonsMagic::spellVariantBase(savedRules, spell->getId()) != spell->getId();
}

bool isNewHorizonsLifeDrainSpell(const CSpell * spell, const JsonNode & savedRules)
{
	return spell && spell->getJsonKey() == newHorizonsMagic::SHADOW_LIFE_DRAIN_SPELL
		&& newHorizonsMagic::spellAllowedBySavedRoster(savedRules, spell->getId());
}

bool isNewHorizonsBlinkSpell(const CSpell * spell)
{
	return spell && spell->getJsonKey() == newHorizonsBlink::SPELL_ID;
}

bool isNewHorizonsSoulChainSpell(const CSpell * spell, const JsonNode & savedRules)
{
	return spell && spell->getJsonKey() == newHorizonsSoulChain::SPELL_ID
		&& newHorizonsSoulChain::isEnabled(savedRules);
}

bool isNewHorizonsVengefulVinesSpell(const CSpell * spell, const JsonNode & savedRules)
{
	return spell && newHorizonsVengefulVines::enabled(savedRules, spell->getId());
}

bool isRegenerationTarget(const battle::Unit * unit)
{
	return isLivingCureTarget(unit) && !unit->isClone() && unit->getPhantomInitialIntegrity() <= 0;
}

bool hasRegenerationMarker(const battle::Unit * unit)
{
	if(!unit)
		return false;
	return unit->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(newHorizonsRegenerationSpellId()))
		.And(Selector::type()(BonusType::HP_REGENERATION)));
}

int32_t newHorizonsRegenerationRate(const BattleSpellMechanics & mechanics)
{
	const auto * hero = mechanics.getHeroCaster();
	const bool herbalist = hero && hero->hasActivePerk(
		std::string(newHorizonsMagic::NATURE_MAGIC_SKILL),
		std::string(newHorizonsMagic::NATURE_HERBALIST));
	return newHorizonsMagic::regenerationRateMillionthsBasisPoints(
		std::max<int32_t>(0, mechanics.getEffectPower()), mechanics.getSpellPowerCoefficientBasisPoints(),
		herbalist, mechanics.getWarcastingBonusPercent(), mechanics.getEmpowerSpellBonusPercent());
}

void applyRegenerationRateSnapshot(ServerCallback * server, const CBattleInfoCallback * battle,
	const battle::Units & affectedUnits, const int32_t regenerationRate)
{
	if(!server || !battle)
		return;

	std::set<uint32_t> affectedUnitIds;
	for(const auto * unit : affectedUnits)
		if(unit)
			affectedUnitIds.insert(unit->unitId());

	for(const auto unitId : affectedUnitIds)
	{
		const auto * unit = battle->battleGetUnitByID(unitId);
		if(!unit || !hasRegenerationMarker(unit))
			continue;

		auto state = unit->acquireState();
		if(!state)
			continue;
		state->regenerationRateMillionths = regenerationRate;

		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		BattleUnitsChanged changed;
		changed.battleID = battle->getBattle()->getBattleID();
		changed.changedStacks.push_back(std::move(update));
		server->apply(changed);
	}
}

bool isSpellLocked(const battle::Unit * unit)
{
	if(!unit)
		return false;

	static const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	const auto lockBonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellLock)));
	return lockBonuses && vstd::contains_if(*lockBonuses, [](const std::shared_ptr<Bonus> & bonus)
	{
		return bonus && bonus->type == BonusType::MAGIC_RESISTANCE
			&& Bonus::NTurns(bonus.get()) && bonus->turnsRemain > 0;
	});
}

void filterMassRegenerationTargets(effects::Effects::EffectsToApply & effectsToApply)
{
	for(auto & effect : effectsToApply)
		vstd::erase_if(effect.second, [](const Destination & destination)
		{
			return !isRegenerationTarget(destination.unitValue);
		});
}

class EffectPacketRecorder final : public ServerCallback
{
private:
	struct EffectState
	{
		SpellID spell;
		std::vector<Bonus> bonuses;
	};

public:
	enum class ChangeKind
	{
		ADDED,
		UPDATED,
		REMOVED
	};

	struct EffectChange
	{
		uint32_t unitId;
		SpellID spell;
		ChangeKind kind;
		std::optional<int32_t> turns;
	};

	struct HealingChange
	{
		uint32_t unitId;
		CreatureID creature;
		int32_t countBefore = 0;
		int32_t countAfter = 0;
		int64_t healthRestored = 0;
	};

	struct AddedUnit
	{
		uint32_t unitId;
		CreatureID creature;
		int32_t count;
		int64_t availableHealth;
		bool natureSummoned;
		bool clone;
		int64_t phantomInitialIntegrity;
		int64_t phantomIntegrity;
		int32_t phantomDuration;
	};

	EffectPacketRecorder(ServerCallback & delegate, const CBattleInfoCallback & battle,
		const Mechanics & mechanics, PlayerColor casterOwner, SpellID castSpellId, SpellID effectSpellId,
		bool captureArmyChanges)
		: delegate(delegate)
		, battle(battle)
		, mechanics(mechanics)
		, casterOwner(casterOwner)
		, castSpellId(castSpellId)
		, effectSpellId(effectSpellId)
		, captureArmyChanges(captureArmyChanges)
	{
	}

	void complain(const std::string & problem) override { delegate.complain(problem); }
	bool describeChanges() const override { return delegate.describeChanges(); }
	vstd::RNG * getRNG() override { return delegate.getRNG(); }
	bool rollCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor, int percentageChance) override
	{
		return delegate.rollCombatAbility(battle, actor, percentageChance);
	}
	bool rollHostileCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor,
		const battle::Unit & recipient, int percentageChance) override
	{
		return delegate.rollHostileCombatAbility(battle, actor, recipient, percentageChance);
	}
	bool resolveAdverseCombatRoll(const BattleID & battleID, BattleSide affectedSide,
		bool stochastic, bool adverseOnTrue, const std::function<bool()> & draw) override
	{
		return delegate.resolveAdverseCombatRoll(battleID, affectedSide, stochastic, adverseOnTrue, draw);
	}

	void apply(CPackForClient & pack) override
	{
		if(auto * effects = dynamic_cast<SetStackEffect *>(&pack))
		{
			record(*effects);
			return;
		}
		if(auto * units = dynamic_cast<BattleUnitsChanged *>(&pack))
		{
			record(*units);
			return;
		}
		if(auto * moved = dynamic_cast<BattleStackMoved *>(&pack))
		{
			apply(*moved);
			return;
		}
		if(auto * injured = dynamic_cast<StacksInjured *>(&pack))
		{
			applyInjuries(*injured);
			return;
		}
		delegate.apply(pack);
	}
	void apply(BattleLogMessage & pack) override { delegate.apply(pack); }
	void apply(BattleStackMoved & pack) override
	{
		const auto before = armySnapshot(pack.stack);
		delegate.apply(pack);
		recordArmyChange(before);
	}
	void apply(BattleUnitsChanged & pack) override { record(pack); }
	void apply(SetStackEffect & pack) override { record(pack); }
	void apply(StacksInjured & pack) override
	{
		applyInjuries(pack);
	}
	void apply(BattleObstaclesChanged & pack) override { delegate.apply(pack); }
	void apply(CatapultAttack & pack) override { delegate.apply(pack); }

	const std::vector<BattleStackAttacked> & injuries() const { return recordedInjuries; }
	const std::vector<HealingChange> & healingChanges() const { return recordedHealingChanges; }
	std::vector<AddedUnit> addedUnits() const
	{
		std::vector<AddedUnit> result;
		result.reserve(addedUnitIds.size());
		for(const auto unitId : addedUnitIds)
		{
			const auto * unit = battle.battleGetUnitByID(unitId);
			if(!unit || !unit->alive() || unit->isGhost())
				continue;
			const auto duration = std::ranges::find_if(addedUnitDurations, [unitId](const auto & entry)
			{
				return entry.first == unitId;
			});
			const auto state = unit->acquireState();
			result.push_back({unitId, unit->creatureId(), unit->getCount(), unit->getAvailableHealth(),
				state && state->natureSummoned, unit->isClone(),
				unit->getPhantomInitialIntegrity(), unit->getPhantomIntegrity(),
				duration == addedUnitDurations.end() ? 0 : duration->second});
		}
		return result;
	}
	bool touchedStackEffects() const { return stackEffectsTouched; }
	bool changedArmy(BattleSide side) const
	{
		if(actualArmyChanges.count(side) != 0)
			return true;
		// Timed effects may implement an identical refresh as remove + add.
		// Finalize bonus semantics once after all effects of this cast have applied;
		// intermediate transport absence is not a lasting effect on the army.
		for(const auto & [unitId, before] : initialArmyStates)
		{
			const auto after = armySnapshot(unitId);
			if((before.exists && before.side == side) || (after.exists && after.side == side))
				if(!sameArmyBonuses(before, after))
					return true;
		}
		return false;
	}
	const std::vector<EffectChange> & effectChanges()
	{
		if(effectChangesFinalized)
			return recordedEffectChanges;

		recordedEffectChanges.clear();
		for(const auto & [unitId, oldStates] : initialEffectStates)
		{
			const auto newStates = snapshot(unitId);
			std::vector<SpellID> spells;
			for(const auto & state : oldStates)
				spells.push_back(state.spell);
			for(const auto & state : newStates)
				if(!vstd::contains(spells, state.spell))
					spells.push_back(state.spell);

			for(const auto spell : spells)
			{
				const auto oldState = std::ranges::find(oldStates, spell, &EffectState::spell);
				const auto newState = std::ranges::find(newStates, spell, &EffectState::spell);
				const auto * oldEffect = oldState == oldStates.end() ? nullptr : &*oldState;
				const auto * newEffect = newState == newStates.end() ? nullptr : &*newState;
				if(oldEffect && newEffect && sameEffectState(*oldEffect, *newEffect))
					continue;

				ChangeKind kind = ChangeKind::UPDATED;
				if(!oldEffect)
					kind = ChangeKind::ADDED;
				else if(!newEffect)
					kind = ChangeKind::REMOVED;
				recordedEffectChanges.push_back({unitId, spell, kind,
					kind == ChangeKind::REMOVED ? duration(oldEffect) : duration(newEffect)});
			}
		}
		effectChangesFinalized = true;
		return recordedEffectChanges;
	}

private:
	ServerCallback & delegate;
	const CBattleInfoCallback & battle;
	const Mechanics & mechanics;
	PlayerColor casterOwner;
	SpellID castSpellId;
	SpellID effectSpellId;
	std::vector<BattleStackAttacked> recordedInjuries;
	std::vector<HealingChange> recordedHealingChanges;
	std::vector<uint32_t> addedUnitIds;
	std::vector<std::pair<uint32_t, int32_t>> addedUnitDurations;
	std::vector<EffectChange> recordedEffectChanges;
	std::vector<std::pair<uint32_t, std::vector<EffectState>>> initialEffectStates;
	bool effectChangesFinalized = false;
	bool stackEffectsTouched = false;
	bool captureArmyChanges;
	std::set<BattleSide> actualArmyChanges;

	struct ArmySnapshot
	{
		uint32_t unitId;
		bool exists = false;
		BattleSide side = BattleSide::NONE;
		CreatureID creature;
		BattleHex position;
		int32_t count = 0;
		int64_t health = 0;
		JsonNode state;
		std::vector<JsonNode> bonuses;
	};
	std::map<uint32_t, ArmySnapshot> initialArmyStates;

	ArmySnapshot armySnapshot(uint32_t unitId) const
	{
		ArmySnapshot result;
		result.unitId = unitId;
		if(!captureArmyChanges)
			return result;
		const auto * unit = battle.battleGetUnitByID(unitId);
		if(!unit)
			return result;
		result.exists = true;
		result.side = unit->unitSide();
		result.creature = unit->creatureId();
		result.position = unit->getPosition();
		result.count = unit->getCount();
		result.health = unit->getAvailableHealth();
		if(auto state = unit->acquireState())
			result.state = state->save(); // Includes health, count, position and unit-owned state.
		const auto bonuses = unit->getBonuses(Selector::all);
		for(const auto & bonus : *bonuses)
		{
			if(!bonus)
				continue;
			// Attribution/icon bookkeeping alone is not an effect on the army.
			// Keep gameplay provenance (including appliedByEnemy and parameters).
			auto semantic = bonus->toJsonNode();
			semantic.Struct().erase("spellCasterOwner");
			semantic.Struct().erase("description");
			semantic.Struct().erase("hidden");
			semantic["bonusOwner"].Integer() = bonus->bonusOwner.getNum();
			if(bonus->propagationUpdater)
				semantic["propagationUpdater"] = bonus->propagationUpdater->toJsonNode();
			// Snapshot values now: shared limiter/parameter pointers may later mutate.
			result.bonuses.push_back(std::move(semantic));
		}
		return result;
	}

	static bool sameArmyBonuses(const ArmySnapshot & before, const ArmySnapshot & after)
	{
		if(before.bonuses.size() != after.bonuses.size())
			return false;
		std::vector<bool> matched(after.bonuses.size(), false);
		for(const auto & bonus : before.bonuses)
		{
			size_t index = 0;
			for(; index < after.bonuses.size(); ++index)
				if(!matched[index] && bonus == after.bonuses[index])
					break;
			if(index == after.bonuses.size())
				return false;
			matched[index] = true;
		}
		return true;
	}

	void recordArmyChange(const ArmySnapshot & before)
	{
		if(!captureArmyChanges)
			return;
		initialArmyStates.try_emplace(before.unitId, before);
		const auto after = armySnapshot(before.unitId);
		if(before.exists == after.exists && before.side == after.side
			&& before.creature == after.creature && before.position == after.position
			&& before.count == after.count && before.health == after.health
			&& before.state == after.state)
			return;
		// Sticky per applied packet: a later reversal does not erase an actual
		// effect of this cast. Retain the old side even if the unit was removed.
		if(before.exists)
			actualArmyChanges.insert(before.side);
		if(after.exists)
			actualArmyChanges.insert(after.side);
	}

	void applyInjuries(StacksInjured & pack)
	{
		std::vector<ArmySnapshot> before;
		for(const auto & injury : pack.stacks)
			before.push_back(armySnapshot(injury.stackAttacked));
		record(pack);
		delegate.apply(pack);
		for(const auto & previous : before)
			recordArmyChange(previous);
	}

	void record(const StacksInjured & pack)
	{
		const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
		if(auto claim = overwhelmingFormulaClaim(battle, spell,
			mechanics.getCapturedMdrPenetration(), pack.stacks))
			delegate.apply(*claim);
		recordedInjuries.insert(recordedInjuries.end(), pack.stacks.begin(), pack.stacks.end());
	}

	void record(BattleUnitsChanged & pack)
	{
		std::vector<ArmySnapshot> armyBefore;
		for(const auto & change : pack.changedStacks)
			armyBefore.push_back(armySnapshot(change.id));
		struct UnitState
		{
			uint32_t unitId;
			CreatureID creature;
			int32_t count;
			int64_t health;
		};
		std::vector<UnitState> before;
		before.reserve(pack.changedStacks.size());
		std::vector<uint32_t> updatedUnits;
		for(const auto & change : pack.changedStacks)
		{
			if(change.operation == UnitChanges::EOperation::ADD
				&& !vstd::contains(addedUnitIds, change.id))
			{
				battle::UnitInfo addedUnit;
				addedUnit.load(change.id, change.data);
				addedUnitIds.push_back(change.id);
				addedUnitDurations.emplace_back(change.id, addedUnit.phantomDuration);
			}
			if(change.operation != UnitChanges::EOperation::UPDATE || change.healthDelta <= 0
				|| vstd::contains(updatedUnits, change.id))
				continue;
			updatedUnits.push_back(change.id);
			const auto * unit = battle.battleGetUnitByID(change.id);
			if(unit)
				before.push_back({change.id, unit->creatureId(), unit->getCount(), unit->getAvailableHealth()});
		}

		// The wrapped callback remains the sole authority that applies the packet.
		// Outcomes below are derived from the resulting battle state, never from a
		// speculative copy of the spell effect.
		delegate.apply(pack);
		for(const auto & previous : armyBefore)
			recordArmyChange(previous);

		for(const auto unitId : updatedUnits)
		{
			const auto * unit = battle.battleGetUnitByID(unitId);
			if(!unit)
				continue;
			const auto previous = std::ranges::find(before, unitId, &UnitState::unitId);
			if(previous == before.end())
				continue;
			const auto restored = std::max<int64_t>(0, unit->getAvailableHealth() - previous->health);
			if(restored <= 0)
				continue;

			const auto existing = std::ranges::find(recordedHealingChanges, unitId, &HealingChange::unitId);
			if(existing == recordedHealingChanges.end())
				recordedHealingChanges.push_back({unitId, previous->creature,
					previous->count, unit->getCount(), restored});
			else
			{
				existing->countAfter = unit->getCount();
				existing->healthRestored += restored;
			}
		}
	}

	static bool sameBonusState(const Bonus & left, const Bonus & right)
	{
		const auto samePropagationUpdater = [&]()
		{
			if(static_cast<bool>(left.propagationUpdater) != static_cast<bool>(right.propagationUpdater))
				return false;
			return !left.propagationUpdater
				|| left.propagationUpdater->toJsonNode() == right.propagationUpdater->toJsonNode();
		};
		// Bonus::toJsonNode covers every gameplay field exposed by content (including
		// parameters, limiter, updater and propagator).  The remaining serialized
		// state is compared explicitly so a packet-only refresh is never mistaken for
		// a no-op merely because its visible value stayed the same.
		return left.toJsonNode() == right.toJsonNode()
			&& samePropagationUpdater()
			&& left.customIconPath == right.customIconPath
			&& left.bonusOwner == right.bonusOwner
			&& left.appliedByEnemy == right.appliedByEnemy;
	}

	static bool sameEffectState(const EffectState & left, const EffectState & right)
	{
		if(left.bonuses.size() != right.bonuses.size())
			return false;

		std::vector<bool> matched(right.bonuses.size(), false);
		for(const auto & leftBonus : left.bonuses)
		{
			size_t found = right.bonuses.size();
			for(size_t index = 0; index < right.bonuses.size(); ++index)
			{
				if(!matched[index] && sameBonusState(leftBonus, right.bonuses[index]))
				{
					found = index;
					break;
				}
			}
			if(found == right.bonuses.size())
				return false;
			matched[found] = true;
		}
		return true;
	}

	std::vector<EffectState> snapshot(uint32_t unitId) const
	{
		std::vector<EffectState> result;
		const auto * unit = battle.battleGetUnitByID(unitId);
		if(!unit)
			return result;

		const auto bonuses = unit->getBonuses(Selector::sourceType()(BonusSource::SPELL_EFFECT));
		for(const auto & bonus : *bonuses)
		{
			if(!bonus)
				continue;
			if(!bonus->sid.as<SpellID>().hasValue())
				continue;
			const SpellID spell = bonus->sid.as<SpellID>();
			auto state = std::ranges::find(result, spell, &EffectState::spell);
			if(state == result.end())
				state = result.emplace(result.end(), EffectState{spell, {}});
			state->bonuses.push_back(*bonus);
		}
		return result;
	}

	static std::optional<int32_t> duration(const EffectState * state)
	{
		if(!state)
			return std::nullopt;
		std::optional<int32_t> result;
		for(const auto & bonus : state->bonuses)
		{
			if(!Bonus::NTurns(&bonus))
				continue;
			result = std::max(result.value_or(bonus.turnsRemain), static_cast<int32_t>(bonus.turnsRemain));
		}
		return result;
	}

	void record(SetStackEffect & pack)
	{
		stackEffectsTouched = true;
		captureHypnotizeCeiling(pack);
		if(casterOwner != PlayerColor::CANNOT_DETERMINE)
		{
			// Stamp incoming copies. Generic updates may only extend the old timer,
			// but special refreshable statuses such as Guardian Spirit replace their marker.
			const auto stampSpellCaster = [this](auto & changes)
			{
				for(auto & change : changes)
				{
					for(auto & bonus : change.second)
					{
						if(bonus.source != BonusSource::SPELL_EFFECT
							|| (bonus.sid != BonusSourceID(castSpellId)
								&& bonus.sid != BonusSourceID(effectSpellId)))
							continue;
						bonus.spellCasterOwner = casterOwner;
					}
				}
			};
			stampSpellCaster(pack.toAdd);
			stampSpellCaster(pack.toUpdate);

			const auto stampHostility = [&](auto & changes)
			{
				for(auto & [unitId, bonuses] : changes)
				{
					const auto * recipient = battle.battleGetUnitByID(unitId);
					if(!recipient)
						continue;
					const auto recipientOwner = battle.battleGetOwner(recipient);
					if(recipientOwner == PlayerColor::CANNOT_DETERMINE)
						continue;
					const bool appliedByEnemy = casterOwner != recipientOwner;
					for(auto & bonus : bonuses)
					{
						if(bonus.type != BonusType::MORALE || bonus.val >= 0
							|| bonus.source != BonusSource::SPELL_EFFECT
							|| (bonus.sid != BonusSourceID(castSpellId)
								&& bonus.sid != BonusSourceID(effectSpellId)))
							continue;
						bonus.appliedByEnemy = appliedByEnemy;
					}
				}
			};
			stampHostility(pack.toAdd);
			stampHostility(pack.toUpdate);
		}

		std::vector<uint32_t> touched;
		const auto collect = [&touched](const auto & changes)
		{
			for(const auto & [unitId, bonuses] : changes)
				if(!vstd::contains(touched, unitId))
					touched.push_back(unitId);
		};
		collect(pack.toAdd);
		collect(pack.toUpdate);
		collect(pack.toRemove);
		std::vector<ArmySnapshot> armyBefore;
		for(const auto unitId : touched)
			armyBefore.push_back(armySnapshot(unitId));

		for(const auto unitId : touched)
		{
			if(std::ranges::none_of(initialEffectStates, [unitId](const auto & entry)
			{
				return entry.first == unitId;
			}))
				initialEffectStates.emplace_back(unitId, snapshot(unitId));
		}

		delegate.apply(pack);
		for(const auto & previous : armyBefore)
			recordArmyChange(previous);
		effectChangesFinalized = false;
	}

	void captureHypnotizeCeiling(SetStackEffect & pack)
	{
		static const SpellID hypnotizeSpell(SpellID::decode("core:hypnotize"));
		if(castSpellId != hypnotizeSpell && effectSpellId != hypnotizeSpell)
			return;

		const auto capture = [this](auto & changes)
		{
			for(auto & [unitId, bonuses] : changes)
			{
				const auto * recipient = battle.battleGetUnitByID(unitId);
				if(!recipient)
					continue;

				for(auto & bonus : bonuses)
				{
					if(bonus.type != BonusType::HYPNOTIZED
						|| bonus.source != BonusSource::SPELL_EFFECT
						|| (bonus.sid != BonusSourceID(castSpellId)
							&& bonus.sid != BonusSourceID(effectSpellId)))
						continue;

					const int64_t maximumTargetHealth = mechanics.applySpellBonus(
						mechanics.getEffectValue(), recipient);
					if(maximumTargetHealth < 0)
						throw std::runtime_error("Hypnotize produced a negative captured health ceiling");

					JsonNode parameters;
					if(bonus.parameters)
					{
						try
						{
							parameters = bonus.parameters->template toCustom<JsonNode>();
						}
						catch(const std::exception &)
						{
							throw std::runtime_error("Hypnotize bonus has incompatible non-JSON parameters");
						}

						if(!parameters.isStruct())
							throw std::runtime_error("Hypnotize bonus parameters must be a named object");

						const auto & existingCeiling = parameters["maximumTargetHealth"];
						if(!existingCeiling.isNull()
							&& (existingCeiling.getType() != JsonNode::JsonType::DATA_INTEGER
								|| existingCeiling.Integer() != maximumTargetHealth))
							throw std::runtime_error("Hypnotize bonus has an incompatible captured health ceiling");
					}

					parameters["maximumTargetHealth"].Integer() = maximumTargetHealth;
					bonus.parameters = std::make_shared<BonusParameters>(parameters);
				}
			}
		};

		capture(pack.toAdd);
		capture(pack.toUpdate);
	}
};

bool hasCounterpressureEffect(const EffectPacketRecorder & recorder, BattleSide victimSide)
{
	return recorder.changedArmy(victimSide);
}

}

namespace SRSLPraserHelpers
{
	static int XYToHex(int x, int y)
	{
		return x + GameConstants::BFIELD_WIDTH * y;
	}

	static int XYToHex(std::pair<int, int> xy)
	{
		return XYToHex(xy.first, xy.second);
	}

	static int hexToY(int battleFieldPosition)
	{
		return battleFieldPosition/GameConstants::BFIELD_WIDTH;
	}

	static int hexToX(int battleFieldPosition)
	{
		int pos = battleFieldPosition - hexToY(battleFieldPosition) * GameConstants::BFIELD_WIDTH;
		return pos;
	}

	static std::pair<int, int> hexToPair(int battleFieldPosition)
	{
		return std::make_pair(hexToX(battleFieldPosition), hexToY(battleFieldPosition));
	}

	//moves hex by one hex in given direction
	//0 - left top, 1 - right top, 2 - right, 3 - right bottom, 4 - left bottom, 5 - left
	static std::pair<int, int> gotoDir(int x, int y, int direction)
	{
		switch(direction)
		{
		case 0: //top left
			return std::make_pair((y%2) ? x-1 : x, y-1);
		case 1: //top right
			return std::make_pair((y%2) ? x : x+1, y-1);
		case 2:  //right
			return std::make_pair(x+1, y);
		case 3: //right bottom
			return std::make_pair((y%2) ? x : x+1, y+1);
		case 4: //left bottom
			return std::make_pair((y%2) ? x-1 : x, y+1);
		case 5: //left
			return std::make_pair(x-1, y);
		default:
			throw std::runtime_error("Disaster: wrong direction in SRSLPraserHelpers::gotoDir!\n");
		}
	}

	static std::pair<int, int> gotoDir(std::pair<int, int> xy, int direction)
	{
		return gotoDir(xy.first, xy.second, direction);
	}

	static bool isGoodHex(std::pair<int, int> xy)
	{
		return xy.first >=0 && xy.first < GameConstants::BFIELD_WIDTH && xy.second >= 0 && xy.second < GameConstants::BFIELD_HEIGHT;
	}

	//helper function for rangeInHexes
	static std::set<ui16> getInRange(unsigned int center, int low, int high)
	{
		std::set<ui16> ret;
		if(low == 0)
		{
			ret.insert(center);
		}

		std::pair<int, int> mainPointForLayer[6]; //A, B, C, D, E, F points
		for(auto & elem : mainPointForLayer)
			elem = hexToPair(center);

		for(int it=1; it<=high; ++it) //it - distance to the center
		{
			for(int b=0; b<6; ++b)
				mainPointForLayer[b] = gotoDir(mainPointForLayer[b], b);

			if(it>=low)
			{
				std::pair<int, int> curHex;

				//adding lines (A-b, B-c, C-d, etc)
				for(int v=0; v<6; ++v)
				{
					curHex = mainPointForLayer[v];
					for(int h=0; h<it; ++h)
					{
						if(isGoodHex(curHex))
							ret.insert(XYToHex(curHex));
						curHex = gotoDir(curHex, (v+2)%6);
					}
				}

			} //if(it>=low)
		}

		return ret;
	}
}

bool targetsSanctifiedStackDirectly(const Mechanics & mechanics, const Target & target)
{
	if(target.size() != 1 || mechanics.isMassive())
		return false;
	const auto * spell = mechanics.getSpell();
	if(!spell || !(spell->isOffensive() || spell->isNegative() || spell->isDamage()))
		return false;

	const auto targetTypes = mechanics.getTargetTypes();
	if(targetTypes != std::vector<AimType>{AimType::CREATURE})
		return false;
	if(mechanics.getSpellId() == SpellID::CHAIN_LIGHTNING || mechanics.isNewHorizonsStormOfDaggers())
		return false;

	const auto affectedStacks = mechanics.getAffectedStacks(target);
	if(affectedStacks.size() != 1)
		return false;

	const auto * battle = mechanics.battle();
	if(!battle)
		return false;

	const auto & destination = target.front();
	const auto * unit = destination.unitValue;
	if(!unit && destination.hexValue.isValid())
		unit = battle->battleGetUnitByPos(destination.hexValue, true);

	return unit && unit->hasBonusOfType(BonusType::SANCTIFIED) && mechanics.ownerMatches(unit, false);
}

BattleSpellMechanics::BattleSpellMechanics(const IBattleCast * event,
										   std::shared_ptr<effects::Effects> effects_,
										   std::shared_ptr<IReceptiveCheck> targetCondition_):
	BaseMechanics(event),
	eventSnapshot(dynamic_cast<const BattleCast *>(event)
		? std::make_optional(*dynamic_cast<const BattleCast *>(event)) : std::nullopt),
	effects(std::move(effects_)),
	targetCondition(std::move(targetCondition_))
{}

void BattleSpellMechanics::forEachEffect(const std::function<bool (const spells::effects::Effect &)> & fn) const
{
	if (!effects)
		return;

	effects->forEachEffect(getEffectLevel(), [&](const spells::effects::Effect * eff, bool & stop)
	{
		if(!eff)
			return;

		if(fn(*eff))
			stop = true;
	});
}

BattleSpellMechanics::~BattleSpellMechanics() = default;

void BattleSpellMechanics::applyEffects(ServerCallback * server, const Target & targets, bool indirect, bool ignoreImmunity) const
{
	Target unlockedTargets = targets;
	const bool naturesWrath = newHorizonsNaturesWrath::enabled(*this);
	const auto * battleState = battle() ? battle()->getBattle() : nullptr;
	if(battleState && isNewHorizonsMassRegenerationSpell(owner, battleState->getMagicRules()))
		vstd::erase_if(unlockedTargets, [](const Destination & destination)
		{
			return !isRegenerationTarget(destination.unitValue);
		});

	if(isMagicalEffect())
	{
		const auto targetTypes = getTargetTypes();
		const bool compoundCreatureTarget = targetTypes == std::vector<AimType>{AimType::CREATURE, AimType::LOCATION}
			|| targetTypes == std::vector<AimType>{AimType::CREATURE, AimType::CREATURE};
		const bool compoundTargetLocked = compoundCreatureTarget && vstd::contains_if(unlockedTargets,
			[](const Destination & destination)
			{
				return destination.unitValue && isSpellLocked(destination.unitValue);
			});
		if(compoundTargetLocked)
			return;
		if(naturesWrath)
		{
			for(auto & destination : unlockedTargets)
				if(destination.unitValue && isSpellLocked(destination.unitValue))
					destination = Destination(destination.hexValue);
		}
		else
			vstd::erase_if(unlockedTargets, [](const Destination & destination)
			{
				return destination.unitValue && isSpellLocked(destination.unitValue);
			});
	}

	auto callback = [&](const effects::Effect * effect, bool & stop)
	{
		if(indirect == effect->indirect)
		{
			if(ignoreImmunity)
			{
				effect->apply(server, this, unlockedTargets);
			}
			else
			{
				Target filtered = effect->filterTarget(this, unlockedTargets);
				effect->apply(server, this, filtered);
			}
		}
	};

	effects->forEachEffect(getEffectLevel(), callback);
}

bool BattleSpellMechanics::canBeCast(Problem & problem) const
{
	// The source selector belongs only to the explicitly enabled Cure action.
	// Reject stray client metadata on all other spells and legacy snapshots.
	if((getCureAffliction() != SpellID::NONE || !getCurePhysicalAffliction().empty()) && !isNewHorizonsCure())
		return adaptGenericProblem(problem);
	if(!getCurePhysicalAffliction().empty()
		&& (getCurePhysicalAffliction() != "frozen" || getCureAffliction() != SpellID::NONE))
		return adaptGenericProblem(problem);

	if(mode == Mode::HERO && isMetamagicFollowup()
		&& !battle()->battleCanUseMetamagicSpell(casterSide, owner->getId(), isMetamagicGrand()))
		return adaptGenericProblem(problem);
	// The shared Hero Action ledger is payload-aware: Divine Mandate's typed
	// Spell grant may pay only for a Light spell. Keep this in the authoritative
	// mechanics path so forged StartAction/cast metadata cannot spend a grant.
	if(mode == Mode::HERO && battle()->battleUsesHeroCommands()
		&& !battle()->battleGetSpellActionAllowance(casterSide, owner->getId()))
		return adaptGenericProblem(problem);

	if(!newHorizonsMagic::spellAllowedByBattleRoster(*battle(), owner->getId()))
		return adaptGenericProblem(problem);
	if(owner->getJsonKey() == newHorizonsRealityWarp::SPELL_KEY
		&& (mode != Mode::HERO || !getHeroCaster() || !usesNewHorizonsMagicV3()))
		return adaptGenericProblem(problem);
	// The mixed route/power contract belongs to saved-v3 rules. A legacy cast
	// must fail before costs, rather than paying for the Lua guard's empty route.
	if(owner->getJsonKey() == newHorizonsNaturesWrath::SPELL_KEY
		&& !newHorizonsNaturesWrath::enabled(*this))
		return adaptGenericProblem(problem);
	if(owner->getJsonKey() == newHorizonsPandemonium::SPELL_KEY
		&& !newHorizonsPandemonium::enabled(*this))
		return adaptGenericProblem(problem);
	// Blink's target geometry and School-scaled radius are defined by the saved
	// v3 snapshot. Do not let a legacy-profile cast spend resources as a no-op.
	if(isNewHorizonsBlinkSpell(owner) && !usesNewHorizonsMagicV3())
		return adaptGenericProblem(problem);
	// Vengeful Vines exists only in the saved v3 roster. Reject stale requests
	// before any mana or hero-action state can be committed.
	const auto * battleState = battle()->getBattle();
	const auto & savedRules = battleState->getMagicRules();
	if(owner->getJsonKey() == newHorizonsVengefulVines::SPELL_KEY
		&& !newHorizonsVengefulVines::enabled(savedRules, owner->getId()))
		return adaptGenericProblem(problem);
	// Entangle's movement-only duration contract belongs to saved-v3 rules.
	// Reject stale requests before spending resources, rather than allowing
	// the Lua effect's defensive version guard to turn a cast into a no-op.
	if(owner->getJsonKey() == "new-horizons:entangle" && !usesNewHorizonsMagicV3())
		return adaptGenericProblem(problem);
	// Summon Trolls uses the saved-v3 School-rank Spell Power formula and
	// temporary Nature-summon state. Reject stale requests before resources or a
	// Hero Action can be spent; its Lua guard alone would only prevent the effect.
	if(owner->getJsonKey() == NEW_HORIZONS_SUMMON_TROLLS_SPELL && !usesNewHorizonsMagicV3())
		return adaptGenericProblem(problem);
	// Verdant Prison's shared pool, Warden bonus, and legal spawn ring are
	// defined by the saved-v3 snapshot. Reject stale casts before any resources
	// or Hero Action can be spent.
	if(owner->getJsonKey() == NEW_HORIZONS_VERDANT_PRISON_SPELL && !usesNewHorizonsMagicV3())
		return adaptGenericProblem(problem);
	// Hydra's Vitality relies on the saved-v3 max-health basis and compact
	// survivor ledger. Reject legacy snapshots before any resource/action cost.
	if(owner->getJsonKey() == NEW_HORIZONS_HYDRAS_VITALITY_SPELL && !usesNewHorizonsMagicV3())
		return adaptGenericProblem(problem);
	if(owner->getJsonKey() == newHorizonsMagic::SHADOW_SOUL_REAPER_SPELL
		&& !newHorizonsMagic::soulReaperEnabled(battle()->getBattle()->getMagicRules(), owner->getId()))
		return adaptGenericProblem(problem);
	if(owner->getJsonKey() == newHorizonsMagic::SHADOW_DOOM_SPELL
		&& !newHorizonsMagic::doomRulesEnabled(battle()->getBattle()->getMagicRules(), owner->getId()))
		return adaptGenericProblem(problem);

	// Overcharge is an action parameter, not a client-side damage hint.  Keep
	// the legality gate in the authoritative mechanics path so malformed or
	// stale requests cannot spend a hero action/mana with a different effect.
	const int selectedOvercharge = getOvercharge();
	const auto modifiers = newHorizonsMagic::magicArrowOverchargeModifiers(
		dynamic_cast<const CGHeroInstance *>(caster));
	const bool adjustableMagicArrow = newHorizonsMagic::magicArrowOverchargeEnabled(
		battle()->getBattle()->getMagicRules(), owner->getId());
	if(selectedOvercharge < 0
		|| (!adjustableMagicArrow && selectedOvercharge != 0)
		|| (adjustableMagicArrow && selectedOvercharge > newHorizonsMagic::magicArrowMaxOvercharge(
			battle()->getBattle()->getMagicRules(), owner->getId(), getEffectPower(), modifiers)))
		return adaptGenericProblem(problem);

	const bool selectiveDispel = isSelectiveDispel();
	const bool massSlow = isMassSlow();
	const auto * castingHero = dynamic_cast<const CGHeroInstance *>(caster);
	const bool newHorizonsRegeneration = isNewHorizonsRegenerationSpell(owner,
		battle()->getBattle()->getMagicRules());
	const bool newHorizonsHydrasVitality = owner->getJsonKey() == NEW_HORIZONS_HYDRAS_VITALITY_SPELL;
	const bool newHorizonsLifeDrain = isNewHorizonsLifeDrainSpell(owner,
		battle()->getBattle()->getMagicRules());
	const bool newHorizonsSoulChain = isNewHorizonsSoulChainSpell(owner,
		battle()->getBattle()->getMagicRules());
	if(selectiveDispel && (mode != Mode::HERO || owner->getId() != SpellID::DISPEL || !castingHero
		|| !castingHero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel")))
		return adaptGenericProblem(problem);
	if(massSlow && (newHorizonsMagic::hasDistinctMassSlow(savedRules)
		|| mode != Mode::HERO || owner->getId() != SpellID::SLOW || !castingHero
		|| !castingHero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalField")
		|| casterSide == BattleSide::NONE || battle()->battleWasTemporalFieldUsed(casterSide)))
		return adaptGenericProblem(problem);

	auto genProblem = battle()->battleCanCastSpell(caster, mode);
	// Orb of Inhibition (BLOCK_ALL_MAGIC) must not block level-0 creature abilities (stone gaze, death stare, ...)
	if(genProblem == ESpellCastProblem::MAGIC_IS_BLOCKED && getSpellLevel() <= 0)
		genProblem = ESpellCastProblem::OK;
	if(genProblem != ESpellCastProblem::OK)
		return adaptProblem(genProblem, problem);

	switch(mode)
	{
	case Mode::HERO:
		{
			//todo: unify hero|creature spell cost
			if(!castingHero)
			{
				logGlobal->debug("CSpell::canBeCast: invalid caster");
				genProblem = ESpellCastProblem::NO_HERO_TO_CAST_SPELL;
			}
			else if(!castingHero->getArt(ArtifactPosition::SPELLBOOK))
				genProblem = ESpellCastProblem::NO_SPELLBOOK;
			else if(!castingHero->canCastThisSpell(owner))
				genProblem = ESpellCastProblem::HERO_DOESNT_KNOW_SPELL;
			else
			{
				int requiredMana = battle()->battleGetSpellCost(owner, castingHero,
					massSlow ? 3 : 1, isMetamagicFollowup());
				if(adjustableMagicArrow)
					requiredMana += selectedOvercharge;
				if(castingHero->getManaAvailable() < requiredMana) //not enough Spell Points
					genProblem = ESpellCastProblem::NOT_ENOUGH_MANA;
			}
		}
		break;
	}

	if(genProblem != ESpellCastProblem::OK)
		return adaptProblem(genProblem, problem);

	if(!owner->isCombat())
		return adaptProblem(ESpellCastProblem::ADVMAP_SPELL_INSTEAD_OF_BATTLE_SPELL, problem);

	const PlayerColor player = caster->getCasterOwner();
	const BattleSide side = battle()->playerToSide(player);

	if(side == BattleSide::NONE)
		return adaptProblem(ESpellCastProblem::INVALID, problem);

	//Cursed Ground blocks magic of creatures - their active casts as well as passively triggered abilities
	bool castByCreature = (mode == Mode::CREATURE_ACTIVE || mode == Mode::ENCHANTER || mode == Mode::PASSIVE) && !caster->getHeroCaster();

	if(castByCreature && owner->isMagical())
	{
		const auto * unitCaster = battle()->battleGetUnitByID(caster->getCasterUnitId());

		if(unitCaster && unitCaster->hasBonusOfType(BonusType::BLOCK_CREATURE_MAGIC))
			return adaptProblem(ESpellCastProblem::MAGIC_IS_BLOCKED, problem);
	}

	//effect like Recanter's Cloak. Blocks also passive casting.
	//TODO: check any possible caster

	if(battle()->battleMaxSpellLevel(side) < getSpellLevel() || battle()->battleMinSpellLevel(side) > getSpellLevel())
		return adaptProblem(ESpellCastProblem::SPELL_LEVEL_LIMIT_EXCEEDED, problem);

	// Counterspell arms authoritative battle-side state and deliberately has no
	// ordinary effect payload. Passing it through Effects::applicable would make
	// the valid no-target action look unusable to both the server and BattleAI.
	if(newHorizonsMagic::isCounterspell(owner))
		return true;

	if(newHorizonsRegeneration)
	{
		if(mode != Mode::HERO || !castingHero)
			return adaptGenericProblem(problem);
		const bool availableTarget = std::ranges::any_of(battle()->battleGetAllUnits(false), [this](const battle::Unit * unit)
		{
			return isRegenerationTarget(unit) && ownerMatches(unit, true) && !isSpellLocked(unit)
				&& isReceptive(unit);
		});
		return availableTarget ? true : adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	}

	if(isNewHorizonsBlinkSpell(owner))
	{
		if(mode != Mode::HERO || !castingHero || casterSide == BattleSide::NONE)
			return adaptGenericProblem(problem);

		const bool availableTarget = std::ranges::any_of(battle()->battleGetAllUnits(false), [this](const battle::Unit * unit)
		{
			return unit && unit->alive() && unit->isValidTarget(false)
				&& !unit->isInvincible() && !isSpellLocked(unit) && isReceptive(unit)
				&& newHorizonsBlink::preview(*this, unit).has_value();
		});
		return availableTarget ? true : adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	}

	if(newHorizonsHydrasVitality)
	{
		if(mode != Mode::HERO || !castingHero)
			return adaptGenericProblem(problem);
		const bool availableTarget = std::ranges::any_of(battle()->battleGetAllUnits(false), [this](const battle::Unit * unit)
		{
			return isRegenerationTarget(unit) && ownerMatches(unit, true) && !isSpellLocked(unit)
				&& isReceptive(unit);
		});
		return availableTarget ? true : adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	}

	if(newHorizonsLifeDrain)
	{
		if(mode != Mode::HERO || !castingHero || casterSide == BattleSide::NONE)
			return adaptGenericProblem(problem);
		bool availableEnemy = false;
		bool availableFriend = false;
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			if(!unit || !unit->alive() || !unit->isValidTarget(false) || isSpellLocked(unit)
				|| !isReceptive(unit))
				continue;
			if(unit->unitSide() == battle()->otherSide(casterSide) && !unit->isInvincible())
				availableEnemy = true;
			else if(unit->unitSide() == casterSide)
				availableFriend = true;
		}
		return availableEnemy && availableFriend
			? true : adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	}

	if(newHorizonsSoulChain)
	{
		if(mode != Mode::HERO || !castingHero || casterSide == BattleSide::NONE)
			return adaptGenericProblem(problem);

		const auto availableTarget = std::ranges::any_of(battle()->battleGetAllUnits(false), [this](const battle::Unit * unit)
		{
			return unit && unit->alive() && !isSpellLocked(unit) && isReceptive(unit)
				&& newHorizonsSoulChain::validEnemyTargetSet(*battle(), casterSide,
					Target{Destination(unit)});
		});
		return availableTarget ? true : adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	}

	if(isNewHorizonsStormOfDaggers())
	{
		if(mode != Mode::HERO || !castingHero)
			return adaptGenericProblem(problem);

		const auto availableTarget = std::ranges::any_of(battle()->battleGetAllUnits(false), [this](const battle::Unit * unit)
		{
			return unit && unit->alive() && unit->isValidTarget(false) && !unit->isInvincible()
				&& unit->unitSide() == battle()->otherSide(casterSide) && !isSpellLocked(unit)
				&& isReceptive(unit);
		});
		return availableTarget ? true : adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	}

	if(isNewHorizonsVengefulVinesSpell(owner, savedRules))
	{
		if(mode != Mode::HERO || !castingHero || casterSide == BattleSide::NONE)
			return adaptGenericProblem(problem);

		const auto enemySide = battle()->otherSide(casterSide);
		const bool availableTarget = std::ranges::any_of(battle()->battleGetAllUnits(false),
			[this, enemySide](const battle::Unit * unit)
		{
			return unit && unit->alive() && unit->isValidTarget(false) && !unit->isInvincible()
				&& unit->unitSide() == enemySide && !isSpellLocked(unit) && isReceptive(unit);
		});
		return availableTarget ? true : adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	}

	// Spellbook/target-picker availability cannot know the eventual Cure choice.
	// Accept the spell when some friendly living unit can be healed or has a
	// supported affliction; canBeCastAt below validates the submitted selection
	// against the exact target before the server spends anything.
	if(isNewHorizonsCure())
	{
		if(mode != Mode::HERO || !castingHero)
			return adaptGenericProblem(problem);

		const auto & rules = battle()->getBattle()->getMagicRules();
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			if(!isLivingCureTarget(unit)
				|| !ownerMatches(unit, true) || !isReceptive(unit))
				continue;

			if((newHorizonsFrozen::enabled(rules) && newHorizonsFrozen::isFrozen(*unit))
				|| !newHorizonsMagic::cureAfflictions(rules, unit).empty()
				|| unit->getSurvivingMissingHealth() > 0)
				return true;
		}
		return adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	}

	return effects->applicable(problem, this);
}

bool BattleSpellMechanics::canCastAtTarget(const battle::Unit * target) const
{
	if(mode == Mode::HERO)
		return true;

	if(!target)
		return true;

	auto spell = getSpell();
	int range = caster->getEffectRange(spell);

	if(range <= 0)
		return true;

	auto casterStack = battle()->battleGetStackByID(caster->getCasterUnitId(), false);
	std::vector<BattleHex> casterPos = { casterStack->getPosition() };
	BattleHex casterWidePos = casterStack->occupiedHex();
	if(casterWidePos != BattleHex::INVALID)
		casterPos.push_back(casterWidePos);

	std::vector<BattleHex> destPos = { target->getPosition() };
	BattleHex destWidePos = target->occupiedHex();
	if(destWidePos != BattleHex::INVALID)
		destPos.push_back(destWidePos);
	
	int minDistance = std::numeric_limits<int>::max();
	for(auto & caster : casterPos)
		for(auto & dest : destPos)
		{
			int distance = BattleHex::getDistance(caster, dest);
			if(distance < minDistance)
				minDistance = distance;
		}

	if(minDistance > range)
		return false;
	
	return true;
}

bool BattleSpellMechanics::canBeCastAt(const Target & target) const
{
	spells::detail::ProblemImpl ignore;
	return canBeCastAt(target, ignore);
}

bool BattleSpellMechanics::canBeCastAt(const Target & target, Problem & problem) const
{
	if(!canBeCast(problem))
		return false;
	if(targetsSanctifiedStackDirectly(*this, target))
		return false;

	const bool newHorizonsCure = isNewHorizonsCure();
	const bool naturesWrath = newHorizonsNaturesWrath::enabled(*this);
	const bool newHorizonsHydrasVitality = owner->getJsonKey() == NEW_HORIZONS_HYDRAS_VITALITY_SPELL;
	const bool newHorizonsPhysicalPoison = mode == Mode::HERO && getHeroCaster()
		&& newHorizonsMagic::physicalPoisonEnabled(battle()->getBattle()->getMagicRules(), owner->getId());
	const bool newHorizonsRegeneration = isNewHorizonsRegenerationSpell(owner,
		battle()->getBattle()->getMagicRules());
	const bool newHorizonsMassRegeneration = isNewHorizonsMassRegenerationSpell(owner,
		battle()->getBattle()->getMagicRules());
	const bool newHorizonsLifeDrain = isNewHorizonsLifeDrainSpell(owner,
		battle()->getBattle()->getMagicRules());
	const bool newHorizonsBlink = isNewHorizonsBlinkSpell(owner);
	const bool newHorizonsSoulChain = isNewHorizonsSoulChainSpell(owner,
		battle()->getBattle()->getMagicRules());
	const bool vengefulVinesEnabled = isNewHorizonsVengefulVinesSpell(owner,
		battle()->getBattle()->getMagicRules());
	Target spellTarget = transformSpellTarget(target);
	if(naturesWrath)
	{
		if(target.size() != 1 || spellTarget.size() != 1)
			return false;
		const auto * first = spellTarget.front().unitValue;
		if(!first && spellTarget.front().hexValue.isAvailable())
			first = battle()->battleGetUnitByPos(spellTarget.front().hexValue, true);
		if(!newHorizonsNaturesWrath::validConductor(first) || isSpellLocked(first))
			return false;
	}
	if(newHorizonsBlink)
	{
		if(mode != Mode::HERO || target.size() != 1)
			return false;

		const battle::Unit * blinkTarget = target.front().unitValue;
		if(!blinkTarget && target.front().hexValue.isValid())
			blinkTarget = battle()->battleGetUnitByPos(target.front().hexValue, true);
		if(!blinkTarget && spellTarget.size() == 1)
			blinkTarget = spellTarget.front().unitValue;
		if(!blinkTarget || !blinkTarget->alive() || !blinkTarget->isValidTarget(false)
			|| blinkTarget->isGhost() || blinkTarget->isInvincible() || isSpellLocked(blinkTarget)
			|| !isReceptive(blinkTarget))
			return false;

		// This same shared preview is consumed by the Lua effect, UI, and AI.
		// Empty landing rings fail before mana or Hero Action are committed.
		return newHorizonsBlink::preview(*this, blinkTarget).has_value();
	}
	if(vengefulVinesEnabled)
	{
		const auto path = newHorizonsVengefulVines::footprint(target);
		if(mode != Mode::HERO || casterSide == BattleSide::NONE || path.size() != 3
			|| spellTarget.size() != path.size())
			return false;

		const auto enemySide = battle()->otherSide(casterSide);
		const bool affectedEnemy = std::ranges::any_of(battle()->battleGetAllUnits(false),
			[this, enemySide, &path](const battle::Unit * unit)
			{
				return unit && unit->alive() && unit->isValidTarget(false) && !unit->isInvincible()
					&& unit->unitSide() == enemySide && !isSpellLocked(unit) && isReceptive(unit)
					&& std::ranges::any_of(path, [unit](const BattleHex & hex)
					{
						return unit->coversPos(hex);
					});
			});
		if(!affectedEnemy)
			return false;
	}
	if(newHorizonsLifeDrain)
	{
		if(mode != Mode::HERO || casterSide == BattleSide::NONE
			|| target.size() != 2 || spellTarget.size() != 2)
			return false;

		const auto * enemy = spellTarget[0].unitValue;
		const auto * ally = spellTarget[1].unitValue;
		if(!enemy || !ally || enemy == ally
			|| !enemy->alive() || !ally->alive()
			|| !enemy->isValidTarget(false) || !ally->isValidTarget(false)
			|| enemy->unitSide() != battle()->otherSide(casterSide)
			|| ally->unitSide() != casterSide
			|| enemy->isInvincible() || isSpellLocked(enemy) || isSpellLocked(ally)
			|| !isReceptive(enemy) || !isReceptive(ally))
			return false;
	}
	if(isNewHorizonsStormOfDaggers())
	{
		if(mode != Mode::HERO || casterSide == BattleSide::NONE
			|| target.size() < 1 || target.size() > newHorizonsMagic::STORM_OF_DAGGERS_MAX_TARGETS
			|| spellTarget.size() != target.size())
			return false;

		std::set<uint32_t> selectedUnitIds;
		for(const auto & destination : spellTarget)
		{
			const auto * unit = destination.unitValue;
			if(!unit || !unit->alive() || !unit->isValidTarget(false) || unit->isInvincible()
				|| unit->unitSide() != battle()->otherSide(casterSide) || isSpellLocked(unit)
				|| !isReceptive(unit)
				|| !selectedUnitIds.insert(unit->unitId()).second)
				return false;
		}
	}
	if(newHorizonsSoulChain)
	{
		if(mode != Mode::HERO || casterSide == BattleSide::NONE
			|| target.size() < 1 || target.size() > newHorizonsSoulChain::MAX_TARGETS
			|| spellTarget.size() != target.size()
			|| !newHorizonsSoulChain::validEnemyTargetSet(*battle(), casterSide, target)
			|| !newHorizonsSoulChain::validEnemyTargetSet(*battle(), casterSide, spellTarget))
			return false;

		// This spell's ordered selection is its relationship contract. Never let a
		// generic effect transform substitute, reorder, or silently drop a unit.
		for(size_t index = 0; index < target.size(); ++index)
		{
			const auto * unit = target[index].unitValue;
			if(!unit || spellTarget[index].unitValue != unit
				|| isSpellLocked(unit) || !isReceptive(unit))
				return false;
		}
	}
	if(newHorizonsCure)
	{
		if(mode != Mode::HERO || target.size() != 1 || spellTarget.size() != 1)
			return false;

		const auto * cureTarget = spellTarget.front().unitValue;
		if(!isLivingCureTarget(cureTarget)
			|| !ownerMatches(cureTarget, true) || !isReceptive(cureTarget))
			return false;

		const auto & rules = battle()->getBattle()->getMagicRules();
		const auto afflictions = newHorizonsMagic::cureAfflictions(rules, cureTarget);
		const auto selected = getCureAffliction();
		const auto physical = getCurePhysicalAffliction();
		if(!physical.empty())
		{
			if(physical != "frozen" || selected != SpellID::NONE
				|| !newHorizonsFrozen::enabled(rules) || !newHorizonsFrozen::isFrozen(*cureTarget))
				return false;
		}
		else if(selected == SpellID::NONE)
		{
			if(!afflictions.empty()
				|| cureTarget->getSurvivingMissingHealth() <= 0)
				return false;
		}
		else if(!vstd::contains(afflictions, selected))
		{
			return false;
		}
	}
	if(newHorizonsHydrasVitality)
	{
		if(mode != Mode::HERO || target.size() != 1)
			return false;

		// Human creature targeting can arrive as a hex-only destination. Generic
		// transformSpellTarget intentionally retains that hex for effect scripts;
		// resolve only this spell's selected footprint here so the pre-cost
		// eligibility and overflow checks see the same wide stack as Lua.
		const battle::Unit * vitalityTarget = nullptr;
		std::set<uint32_t> resolvedTargetIds;
		for(const auto & destination : spellTarget)
		{
			const battle::Unit * candidate = destination.unitValue;
			if(!candidate && destination.hexValue.isValid())
				candidate = battle()->battleGetUnitByPos(destination.hexValue, true);
			if(!candidate || !resolvedTargetIds.insert(candidate->unitId()).second)
				continue;
			if(resolvedTargetIds.size() > 1)
				return false;
			vitalityTarget = candidate;
		}
		if(!vitalityTarget)
			return false;
		if(!isRegenerationTarget(vitalityTarget)
			|| !ownerMatches(vitalityTarget, true)
			|| isSpellLocked(vitalityTarget)
			|| !isReceptive(vitalityTarget))
			return false;

		// The script effect also rejects an unrepresentable bonus value, but
		// spell-target applicability runs before resources are charged. Keep this
		// guard here as well so the action cannot consume mana before a Lua
		// transform filters the target out.
		const auto vitalityState = vitalityTarget->acquireState();
		const int64_t referenceMaximum = vitalityState->getCapacityHealthReferenceMax();
		const int64_t effectPercentMillionths = std::clamp<int64_t>(getEffectValue(),
			25'000'000, 50'000'000);
		const int64_t targetMaximum = referenceMaximum
			* (100'000'000 + effectPercentMillionths) / 100'000'000;
		if(referenceMaximum <= 0 || targetMaximum > std::numeric_limits<int32_t>::max())
			return false;
	}
	if(newHorizonsRegeneration)
	{
		if(mode != Mode::HERO)
			return false;
		if(newHorizonsMassRegeneration)
			return isMassive() && !target.empty();
		if(target.size() != 1 || spellTarget.size() != 1)
			return false;
		const auto * regenerationTarget = spellTarget.front().unitValue;
		if(!isRegenerationTarget(regenerationTarget)
			|| !ownerMatches(regenerationTarget, true)
			|| isSpellLocked(regenerationTarget)
			|| !isReceptive(regenerationTarget))
			return false;
	}
	if(newHorizonsPhysicalPoison)
	{
		if(target.size() != 1 || spellTarget.size() != 1 || casterSide == BattleSide::NONE)
			return false;

		const auto * poisonTarget = spellTarget.front().unitValue;
		if(!isLivingPhysicalPoisonTarget(poisonTarget)
			|| poisonTarget->unitSide() != battle()->otherSide(casterSide)
			|| poisonTarget->isInvincible()
			|| !isReceptive(poisonTarget))
			return false;
	}

	const battle::Unit * mainTarget = nullptr;

	if(!vengefulVinesEnabled && spellTarget.front().unitValue)
	{
		mainTarget = spellTarget.front().unitValue;
	}
	else if(!vengefulVinesEnabled && spellTarget.front().hexValue.isValid())
	{
		mainTarget = battle()->battleGetUnitByPos(target.front().hexValue, true);
	}
	if(isMagicalEffect() && mainTarget && !target.empty() && target.front().unitValue && isSpellLocked(mainTarget))
		return false;

	if(!canCastAtTarget(mainTarget))
		return false;

	if (!getSpell()->canCastOnSelf() && !getSpell()->canCastOnlyOnSelf())
	{
		if(mainTarget && mainTarget == caster)
			return false; // can't cast on self

		if(mainTarget && mainTarget->isInvincible() && getSpell()->isNegative())
			return false;
	}
	else if(getSpell()->canCastOnlyOnSelf())
	{
		if(!mainTarget || mainTarget != caster)
			return false; // can't cast on others
	}
	// Cure's target and selected affliction (or healing need) were validated
	// above. Its legacy HEAL/DISPEL applicability checks do not recognize
	// physical-only Poison, so do not let those checks reject a valid action.
	if(newHorizonsCure || newHorizonsRegeneration || newHorizonsHydrasVitality
		|| newHorizonsPhysicalPoison || newHorizonsLifeDrain
		|| newHorizonsSoulChain || vengefulVinesEnabled
		|| naturesWrath
		|| newHorizonsMagic::isCounterspell(owner))
		return true;

	return effects->applicable(problem, this, target, spellTarget);
}

std::vector<const CStack *> BattleSpellMechanics::getAffectedStacks(const Target & target) const
{
	Target spellTarget = transformSpellTarget(target);
	const bool newHorizonsMassRegeneration = isNewHorizonsMassRegenerationSpell(owner,
		battle()->getBattle()->getMagicRules());

	Target all;

	effects->forEachEffect(getEffectLevel(), [&all, &target, &spellTarget, this](const effects::Effect * e, bool & stop)
	{
		Target one = e->transformTarget(this, target, spellTarget);
		vstd::concatenate(all, one);
	});

	std::set<const CStack *> stacks;

	for(const Destination & dest : all)
	{
		if(newHorizonsMassRegeneration && !isRegenerationTarget(dest.unitValue))
			continue;
		if(dest.unitValue && !dest.unitValue->isInvincible()
			&& (!isMagicalEffect() || !isSpellLocked(dest.unitValue)))
		{
			//FIXME: remove and return battle::Unit
			stacks.insert(battle()->battleGetStackByID(dest.unitValue->unitId(), false));
		}
	}

	std::vector<const CStack *> res;
	std::copy(stacks.begin(), stacks.end(), std::back_inserter(res));
	return res;
}

int64_t BattleSpellMechanics::getTargetAwareEffectValue(const battle::Unit * target) const
{
	if(!target || !eventSnapshot || eventSnapshot->hasSpellcraftTargetSnapshot())
		return getEffectValue();
	return eventSnapshot->mechanicsForTarget({Destination(target)})->getEffectValue();
}

size_t BattleSpellMechanics::getTargetedStackCount(const Target & target) const
{
	const bool previous = countingSpellTargets;
	countingSpellTargets = true;
	const auto restore = vstd::makeScopeGuard([this, previous]() { countingSpellTargets = previous; });
	const auto spellTarget = transformSpellTarget(target);
	std::set<uint32_t> ids;
	effects->forEachEffect(getEffectLevel(), [this, &target, &spellTarget, &ids](const effects::Effect * effect, bool &)
	{
		for(const auto & destination : effect->transformTarget(this, target, spellTarget))
			if(destination.unitValue)
				ids.insert(destination.unitValue->unitId());
	});
	// Hand of Fate targets a random additional living stack even when that
	// stack's defenses later negate its spill. Count that before any draw.
	if(owner->getJsonKey() == "new-horizons:handOfFate" && ids.size() == 1)
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->alive() && unit->getPosition().isValid() && !unit->isTurret() && !ids.contains(unit->unitId()))
				return 2;
	return ids.size();
}

void BattleSpellMechanics::cast(ServerCallback * server, const Target & target)
{
	if(eventSnapshot && !eventSnapshot->hasSpellcraftTargetSnapshot()
		&& mode == Mode::HERO && getHeroCaster()
		&& getHeroCaster()->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsSpellcraft::CONCENTRATION)))
	{
		eventSnapshot->mechanicsForTarget(target)->cast(server, target);
		return;
	}
	// wouldResist may be called from script target preparation or from a
	// secondary-hit consumer while an authoritative cast is running. Keep its
	// callback and RNG access strictly inside this cast, including all early
	// returns and exceptional exits.
	const auto clearResistanceContext = vstd::makeScopeGuard([this]()
	{
		clearPandemoniumDebuffs();
		activeResistanceServer = nullptr;
		activeResistanceRng = nullptr;
		resistanceRolls.clear();
		resistantUnitIds.clear();
	});
	resistanceRolls.clear();
	resistantUnitIds.clear();
	clearPandemoniumDebuffs();
	activeResistanceServer = server;
	activeResistanceRng = server ? server->getRNG() : nullptr;

	const bool newHorizonsPhysicalPoison = mode == Mode::HERO && getHeroCaster()
		&& newHorizonsMagic::physicalPoisonEnabled(battle()->getBattle()->getMagicRules(), owner->getId());
	const bool newHorizonsRegeneration = isNewHorizonsRegenerationSpell(owner,
		battle()->getBattle()->getMagicRules());
	const bool newHorizonsHydrasVitality = owner->getJsonKey() == NEW_HORIZONS_HYDRAS_VITALITY_SPELL;
	const bool newHorizonsLifeDrain = isNewHorizonsLifeDrainSpell(owner,
		battle()->getBattle()->getMagicRules());
	const bool newHorizonsSoulChain = isNewHorizonsSoulChainSpell(owner,
		battle()->getBattle()->getMagicRules());
	if(isNewHorizonsStormOfDaggers()
		&& (!setStormOfDaggersTargetCount(static_cast<int32_t>(target.size()))
			|| !canBeCastAt(target)))
		return;
	if(newHorizonsSoulChain && !canBeCastAt(target))
		return;
	if((newHorizonsRegeneration || newHorizonsHydrasVitality
		|| newHorizonsPhysicalPoison || newHorizonsLifeDrain)
		&& !canBeCastAt(target))
		return;

	BattleSpellCast sc;

	// The authoritative battle state still contains the sequence that led to a
	// follow-up when this method starts.  BattleSpellCast is applied before the
	// effects below, and applying the final leg clears that sequence, so retain
	// the ordinal and target snapshots while they are still available.  These
	// snapshots are used only for the causal battle-log line; ordinary casts do
	// not take this path.
	const bool logMetamagicFollowup = isMetamagicFollowup() && mode == Mode::HERO;
	const int metamagicOrdinal = logMetamagicFollowup
		? static_cast<int>(battle()->getBattle()->getMetamagicSequenceSpells(casterSide).size()) + 1
		: 0;
	struct FollowupTargetSnapshot
	{
		uint32_t unitId = std::numeric_limits<uint32_t>::max();
		CreatureID creature;
		int32_t count = 0;
	};
	std::vector<FollowupTargetSnapshot> followupTargets;

	int spellCost = 0;
	int32_t knightlySequenceSpellCostReduction = 0;

	sc.side = casterSide;
	sc.spellID = getSpellId();
	sc.battleID = battle()->getBattle()->getBattleID();
	sc.tile = target.at(0).hexValue;

	sc.castByHero = mode == Mode::HERO;
	if (mode != Mode::HERO)
		sc.casterStack = caster->getCasterUnitId();
	sc.manaGained = 0;
	sc.counterspellSide = getCounterspellSide();
	sc.counterspellNegated = isCounterspellNegated();
	sc.metamagicFollowup = isMetamagicFollowup();
	sc.metamagicGrand = isMetamagicGrand();
	sc.metamagicTargetUnitId = (!target.empty() && target.front().unitValue)
		? target.front().unitValue->unitId() : std::numeric_limits<uint32_t>::max();
	sc.metamagicManaRefund = getMetamagicManaRefund();
	sc.extendedSpell = sc.castByHero
		&& newHorizonsSpellcraft::extendAvailable(*battle()->getBattle(), casterSide, *owner, getEffectLevel());

	sc.activeCast = false;
	sc.temporalFieldCast = isMassSlow();
	affectedUnits.clear();

	const CGHeroInstance * otherHero = nullptr;
	{
		//check it there is opponent hero
		const BattleSide otherSide = battle()->otherSide(casterSide);

		const auto visibleSide = battle()->battleGetMySide();
		if((visibleSide == BattleSide::ALL_KNOWING || visibleSide == otherSide)
			&& battle()->battleHasHero(otherSide))
			otherHero = battle()->battleGetFightingHero(otherSide);
	}

	//calculate spell cost
	if(mode == Mode::HERO)
	{
		const auto * casterHero = dynamic_cast<const CGHeroInstance *>(caster);
		if(casterHero && !isMetamagicFollowup())
		{
			const auto allowance = battle()->battleGetSpellActionAllowance(casterSide, owner->getId());
			if(allowance && allowance->allowance == HeroActionAllowanceState::AllowanceKind::SPELL
				&& allowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE)
				knightlySequenceSpellCostReduction =
					newHorizonsDivineMandate::knightlySequenceSpellCostReduction(casterHero);
		}
		spellCost = battle()->battleGetSpellCost(owner, casterHero,
			isMassSlow() ? 3 : 1, isMetamagicFollowup());
		if(newHorizonsMagic::magicArrowOverchargeEnabled(battle()->getBattle()->getMagicRules(), owner->getId()))
			spellCost += getOvercharge();

		if(nullptr != otherHero && !isCounterspellNegated()) //handle mana channel
		{
			int manaChannel = 0;
			for(const auto * stack : battle()->battleGetAllStacks(true)) //TODO: shouldn't bonus system handle it somehow?
			{
				if(stack->unitOwner() == otherHero->tempOwner && stack->alive())
					vstd::amax(manaChannel, stack->valOfBonuses(BonusType::MANA_CHANNELING));
			}
			sc.manaGained = (manaChannel * spellCost) / 100;
		}
		sc.activeCast = true;
	}
	else if(mode == Mode::CREATURE_ACTIVE || mode == Mode::ENCHANTER)
	{
		spellCost = 1;
		sc.activeCast = true;
	}
	// Publish the exact resolved hero cost with the accepted-cast packet. This
	// includes all cost modifiers and is independent of later mana refunds.
	sc.paidHeroManaCost = mode == Mode::HERO && sc.activeCast ? spellCost : 0;
	sc.paidCounterspellManaCost = mode == Mode::HERO && sc.activeCast
		? getCounterspellManaSpent() : 0;

	// Capture eligibility before the accepted BattleSpellCast packet consumes
	// the selected action allowance and Order-to-Spell readiness. Hypothetical
	// spell evaluation uses castEval and never enters this authoritative path.
	const auto * casterHero = mode == Mode::HERO ? dynamic_cast<const CGHeroInstance *>(caster) : nullptr;
	const auto * battleInfo = battle()->getBattle();
	const int32_t battleRound = battleInfo->getRound();
	const bool validHeroSide = casterSide == BattleSide::ATTACKER || casterSide == BattleSide::DEFENDER;
	// Capture this before beforeCast can turn a successful Magic Mirror redirect
	// into Mode::MAGIC_MIRROR. The reflected effect is not a second enemy hero cast.
	const bool originalHeroCast = sc.activeCast && sc.castByHero && casterHero && validHeroSide;
	if(originalHeroCast && casterHero == battleInfo->getSideHero(casterSide))
		sc.crossSchoolFormula = newHorizonsCrossSchoolFormula::acceptedReceipt(*battleInfo, casterSide, getSpellId());
	const BattleSide originalCasterSide = casterSide;
	SpellResponseState spellResponseAfterCast;
	bool consumeSpellResponse = false;
	if(originalHeroCast && battleRound >= 0 && hasCounterpressurePerk(casterHero))
	{
		spellResponseAfterCast = battleInfo->getSpellResponseState(casterSide);
		consumeSpellResponse = spellResponseAfterCast.consumeAt(battleRound);
	}
	const uint8_t divineMandatePairsBeforeSpell = mode == Mode::HERO && casterHero && validHeroSide
		? battle()->battleGetDivineMandateStatus(casterSide).completedPairs : 0;
	bool spendsHeroAllowance = !sc.metamagicFollowup;
	std::vector<uint32_t> sharedPurposeFirstRecipients;
	if(mode == Mode::HERO && validHeroSide && battleRound >= 0
		&& heroCommands::supportedByRules(battleInfo->getHeroCommandRules(), HeroCommand::CHARGE))
	{
		const auto & allowances = battleInfo->getHeroActionAllowances(casterSide);
		if(allowances.currentRound == battleRound)
		{
			const auto selection = battle()->battleGetSpellActionAllowance(casterSide, owner->getId());
			spendsHeroAllowance = selection
				&& selection->allowance == HeroActionAllowanceState::AllowanceKind::HERO;
			if(selection && selection->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE
				&& newHorizonsDivineMandate::hasSharedPurposePerk(casterHero))
			{
				const auto grant = std::find_if(allowances.grants.begin(), allowances.grants.end(),
					[&selection](const auto & item) { return item.id == selection->grantId; });
				if(grant != allowances.grants.end())
					sharedPurposeFirstRecipients = grant->divineMandateRecipients;
			}
		}
		else
			spendsHeroAllowance = false;
	}
	const bool recoverBattleMeditation = sc.activeCast && casterHero && spendsHeroAllowance && validHeroSide
		&& newHorizonsWarcasting::battleMeditationEligible(battleInfo->getMagicRules(), casterHero,
			battleInfo->getWarcastingState(casterSide), battleRound);

	registerOverwhelmingFormulaCast(server);
	if(!isCounterspellNegated())
		beforeCast(server, sc, *server->getRNG(), target);

	if(logMetamagicFollowup)
	{
		followupTargets.reserve(affectedUnits.size());
		for(const auto * unit : affectedUnits)
		{
			if(!unit)
				continue;
			followupTargets.push_back({unit->unitId(), unit->creatureId(), unit->getCount()});
		}
	}

	BattleLogMessage castDescription;
	castDescription.battleID = battle()->getBattle()->getBattleID();

	switch (mode)
	{
	case Mode::CREATURE_ACTIVE:
	case Mode::ENCHANTER:
	case Mode::HERO:
	case Mode::PASSIVE:
	case Mode::MAGIC_MIRROR:
		{
			MetaString line;
			caster->getCastDescription(owner, affectedUnits, line);
			if(!line.empty())
				castDescription.lines.push_back(line);
		}
		break;

	default:
		break;
	}

	const auto acceptedSpellId = owner->getId();
	const auto effectSpellId = newHorizonsMagic::spellVariantBase(
		battle()->getBattle()->getMagicRules(), acceptedSpellId);
	// Magic Mirror resolves from the reflecting side, although caster still identifies
	// the original spell caster; use the effective side for application provenance.
	const auto effectCasterOwner = battle()->sideToPlayer(effectiveCasterSide());
	EffectPacketRecorder effectRecorder(*server, *battle(), *this, effectCasterOwner,
		acceptedSpellId, effectSpellId,
		originalHeroCast && !sc.counterspellNegated && battleRound >= 0
			&& hasCounterpressurePerk(battleInfo->getSideHero(battle()->otherSide(originalCasterSide))));
	if(!isCounterspellNegated())
		doRemoveEffects(&effectRecorder, affectedUnits, std::bind(&BattleSpellMechanics::counteringSelector, this, _1));

	// Wrath captures every conductor in route order, including blocked hops,
	// while affectedUnits remains limited to actual recipients for effect cleanup.
	if(!newHorizonsNaturesWrath::enabled(*this))
		for(auto & unit : affectedUnits)
			sc.affectedCres.push_back(unit->unitId());

	if(!castDescription.lines.empty())
		server->apply(castDescription);

	server->apply(sc);
	if(consumeSpellResponse)
	{
		SetSpellResponseState consumedResponse;
		consumedResponse.battleID = sc.battleID;
		consumedResponse.side = originalCasterSide;
		consumedResponse.state = spellResponseAfterCast;
		server->apply(consumedResponse);
	}
	if(mode == Mode::HERO && knightlySequenceSpellCostReduction > 0 && !isCounterspellNegated())
	{
		BattleLogMessage knightlySequenceDescription;
		knightlySequenceDescription.battleID = battle()->getBattle()->getBattleID();
		MetaString line;
		line.appendTextID(caster->getCasterNameTextID());
		line.appendRawString(" uses Knightly Sequence on ");
		line.appendTextID(owner->getNameTextID());
		line.appendRawString(" through Divine Mandate.");
		line.appendRawString(" Cost reduction: up to 2 Mana (minimum 1); paid ");
		line.appendNumber(sc.paidHeroManaCost);
		line.appendRawString(" Mana).");
		knightlySequenceDescription.lines.push_back(std::move(line));
		server->apply(knightlySequenceDescription);
	}
	if(mode == Mode::HERO && getConsecratedCastingBonusPercent() > 0 && !isCounterspellNegated())
	{
		BattleLogMessage consecratedDescription;
		consecratedDescription.battleID = battle()->getBattle()->getBattleID();
		MetaString line;
		line.appendTextID(caster->getCasterNameTextID());
		line.appendRawString(" casts ");
		line.appendTextID(owner->getNameTextID());
		line.appendRawString(" through Divine Mandate: Consecrated Casting adds +");
		line.appendNumber(getConsecratedCastingBonusPercent());
		line.appendRawString("% to its Spell Power-derived components.");
		consecratedDescription.lines.push_back(std::move(line));
		server->apply(consecratedDescription);
	}
	if(mode == Mode::HERO && getWarcastingBonusPercent() > 0)
	{
		BattleLogMessage warcastingDescription;
		warcastingDescription.battleID = battle()->getBattle()->getBattleID();
		MetaString line;
		line.appendTextID(caster->getCasterNameTextID());
		line.appendRawString(" consumes Warcasting (+");
		line.appendNumber(getWarcastingBonusPercent());
		line.appendRawString("%) while casting ");
		line.appendTextID(owner->getNameTextID());
		if(isCounterspellNegated())
			line.appendRawString("; the spell was counterspelled");
		warcastingDescription.lines.push_back(std::move(line));
		server->apply(warcastingDescription);
	}

	if(!isCounterspellNegated())
	{
		for(auto & p : effectsToApply)
			p.first->apply(&effectRecorder, this, p.second);
	}
	if(newHorizonsRegeneration && !isCounterspellNegated())
		applyRegenerationRateSnapshot(&effectRecorder, battle(), affectedUnits, newHorizonsRegenerationRate(*this));
	// BattleSpellCast spends the typed grant before effects resolve. Publish the
	// benefit after restoration/cleansing so a valid restored corpse can receive it.
	if(!sharedPurposeFirstRecipients.empty() && !sc.counterspellNegated)
	{
		const auto recipients = newHorizonsDivineMandate::sharedPurposeFriendlyRecipients(
			*battle(), originalCasterSide, sc.affectedCres);
		std::vector<uint32_t> overlap;
		std::set_intersection(sharedPurposeFirstRecipients.begin(), sharedPurposeFirstRecipients.end(),
			recipients.begin(), recipients.end(), std::back_inserter(overlap));
		SetStackEffect purpose;
		purpose.battleID = sc.battleID;
		for(const auto id : overlap)
			if(const auto * unit = battle()->battleGetUnitByID(id);
				unit && !unit->hasBonus(CSelector(newHorizonsDivineMandate::isSharedPurposeMoraleBonus)))
				purpose.toAdd.emplace_back(id, std::vector<Bonus>{newHorizonsDivineMandate::sharedPurposeMoraleBonus()});
		if(!purpose.toAdd.empty())
			server->apply(purpose);
	}
	if(logMetamagicFollowup)
	{
		struct FollowupTargetOutcome
		{
			const FollowupTargetSnapshot * snapshot = nullptr;
			int64_t damage = 0;
			int32_t killed = 0;
		};
		std::vector<FollowupTargetOutcome> damagedTargets;
		damagedTargets.reserve(effectRecorder.injuries().size());
		for(const auto & injury : effectRecorder.injuries())
		{
			if(injury.damageAmount <= 0)
				continue;
			const auto snapshot = std::ranges::find(followupTargets, injury.stackAttacked,
				&FollowupTargetSnapshot::unitId);
			if(snapshot == followupTargets.end())
				continue;
			const auto existing = std::ranges::find(damagedTargets, &*snapshot,
				&FollowupTargetOutcome::snapshot);
			if(existing == damagedTargets.end())
			{
				damagedTargets.push_back({
					&*snapshot,
					injury.damageAmount,
					static_cast<int32_t>(injury.killedAmount)});
			}
			else
			{
				existing->damage += injury.damageAmount;
				existing->killed += static_cast<int32_t>(injury.killedAmount);
			}
		}

		BattleLogMessage metamagicDescription;
		metamagicDescription.battleID = battle()->getBattle()->getBattleID();
		MetaString line;
		line.appendTextID(caster->getCasterNameTextID());
		line.appendRawString(" casts a ");
		if(metamagicOrdinal == 2)
			line.appendRawString("second");
		else if(metamagicOrdinal == 3)
			line.appendRawString("third");
		else
		{
			line.appendRawString("#");
			line.appendNumber(metamagicOrdinal);
		}
		line.appendRawString(" ");
		line.appendTextID(owner->getNameTextID());
		line.appendRawString(" through Metamagic");

		if(isCounterspellNegated())
		{
			line.appendRawString(", but the spell was counterspelled");
		}
		else
		{
			bool wroteOutcome = false;
			bool wroteCreation = false;
			const auto addedUnits = effectRecorder.addedUnits();
			const bool phantomArmy = owner->getJsonKey() == newHorizonsSorcery::PHANTOM_ARMY_SPELL;
			const bool summonTrolls = owner->getJsonKey() == NEW_HORIZONS_SUMMON_TROLLS_SPELL;
			const bool verdantPrison = owner->getJsonKey() == NEW_HORIZONS_VERDANT_PRISON_SPELL;
			const bool ordinarySummon = getSpellId() == SpellID::SUMMON_FIRE_ELEMENTAL
				|| getSpellId() == SpellID::SUMMON_EARTH_ELEMENTAL
				|| getSpellId() == SpellID::SUMMON_WATER_ELEMENTAL
				|| getSpellId() == SpellID::SUMMON_AIR_ELEMENTAL;
			if(ordinarySummon || getSpellId() == SpellID::CLONE || phantomArmy || summonTrolls || verdantPrison)
			{
				bool wroteAddedUnit = false;
				int32_t verdantStackCount = 0;
				int32_t verdantCreatureCount = 0;
				int64_t verdantAggregateHealth = 0;
				std::optional<CreatureID> verdantCreature;
				for(const auto & added : addedUnits)
				{
					if(getSpellId() == SpellID::CLONE && !added.clone)
						continue;
					if(phantomArmy && added.phantomInitialIntegrity <= 0)
						continue;
					if((summonTrolls || verdantPrison) && !added.natureSummoned)
						continue;
					if(verdantPrison)
					{
						verdantCreature = added.creature;
						++verdantStackCount;
						verdantCreatureCount += added.count;
						verdantAggregateHealth += added.availableHealth;
						wroteAddedUnit = true;
						continue;
					}
					if(wroteAddedUnit)
						line.appendRawString("; ");
					else
						line.appendRawString(", ");
					if(phantomArmy)
						line.appendRawString("creating a phantom stack of ");
					else
						line.appendRawString(getSpellId() == SpellID::CLONE ? "creating a clone of " : "summoning ");
					line.appendNumber(added.count);
					line.appendRawString(" ");
					line.appendName(added.creature, added.count);
					if(summonTrolls)
					{
						line.appendRawString(" with ");
						line.appendNumber(added.availableHealth);
						line.appendRawString(" aggregate HP as a temporary Nature summon");
					}
					else if(phantomArmy)
					{
						line.appendRawString(" with ");
						line.appendNumber(added.phantomIntegrity);
						line.appendRawString("/");
						line.appendNumber(added.phantomInitialIntegrity);
						line.appendRawString(" integrity for ");
						line.appendNumber(added.phantomDuration);
						line.appendRawString(" rounds");
					}
					wroteAddedUnit = true;
				}
				if(verdantStackCount > 0)
				{
					line.appendRawString(", summoning ");
					line.appendNumber(verdantCreatureCount);
					line.appendRawString(" ");
					line.appendName(*verdantCreature, verdantCreatureCount);
					line.appendRawString(" across ");
					line.appendNumber(verdantStackCount);
					line.appendRawString(verdantStackCount == 1 ? " stack with " : " stacks with ");
					line.appendNumber(static_cast<int32_t>(verdantAggregateHealth));
					line.appendRawString(" aggregate HP as temporary Nature summons");
				}
				if(wroteAddedUnit)
				{
					wroteOutcome = true;
					wroteCreation = true;
				}
			}
			if(!damagedTargets.empty())
			{
				line.appendRawString(wroteOutcome ? ", and dealing " : ", dealing ");
				for(size_t index = 0; index < damagedTargets.size(); ++index)
				{
					const auto & outcome = damagedTargets[index];
					if(index > 0)
						line.appendRawString("; ");
					line.appendNumber(outcome.damage);
					line.appendRawString(" damage to ");
					line.appendName(outcome.snapshot->creature, outcome.snapshot->count);
					line.appendRawString(" (");
					line.appendNumber(outcome.killed);
					line.appendRawString(" killed)");
				}
				wroteOutcome = true;
			}
			else if(newHorizonsMagic::isCounterspell(owner))
			{
				line.appendRawString(", raising a counterspell ward");
				wroteOutcome = true;
			}

			bool wroteHealing = false;
			for(const auto & change : effectRecorder.healingChanges())
			{
				if(!wroteHealing)
					line.appendRawString(wroteOutcome ? ", and " : ", ");
				else
					line.appendRawString("; ");
				line.appendRawString("restoring ");
				line.appendNumber(change.healthRestored);
				line.appendRawString(" health to ");
				line.appendName(change.creature, change.countAfter);
				const auto resurrected = std::max(0, change.countAfter - change.countBefore);
				if(resurrected > 0)
				{
					line.appendRawString(" (");
					line.appendNumber(resurrected);
					line.appendRawString(" resurrected)");
				}
				wroteHealing = true;
			}
			if(wroteHealing)
				wroteOutcome = true;

			bool wroteStatusChange = false;
			for(const auto & change : effectRecorder.effectChanges())
			{
				const auto targetSnapshot = std::ranges::find(followupTargets, change.unitId,
					&FollowupTargetSnapshot::unitId);
				const auto * effectSpell = change.spell.toSpell();
				if(targetSnapshot == followupTargets.end() || !effectSpell)
					continue;

				if(!wroteStatusChange)
					line.appendRawString(wroteOutcome ? ", and " : ", ");
				else
					line.appendRawString("; ");
				switch(change.kind)
				{
				case EffectPacketRecorder::ChangeKind::ADDED:
					line.appendRawString("applying ");
					break;
				case EffectPacketRecorder::ChangeKind::UPDATED:
					line.appendRawString("refreshing ");
					break;
				case EffectPacketRecorder::ChangeKind::REMOVED:
					line.appendRawString("removing ");
					break;
				}
				line.appendTextID(effectSpell->getNameTextID());
				if(change.kind == EffectPacketRecorder::ChangeKind::REMOVED)
					line.appendRawString(" from ");
				else if(change.kind == EffectPacketRecorder::ChangeKind::UPDATED)
					line.appendRawString(" on ");
				else
					line.appendRawString(" to ");
				line.appendName(targetSnapshot->creature, targetSnapshot->count);
				if(change.turns)
				{
					line.appendRawString(change.kind == EffectPacketRecorder::ChangeKind::REMOVED ? " with " : " for ");
					line.appendNumber(*change.turns);
					line.appendRawString(*change.turns == 1 ? " turn" : " turns");
					if(change.kind == EffectPacketRecorder::ChangeKind::REMOVED)
						line.appendRawString(" remaining");
				}
				wroteStatusChange = true;
			}
			if(wroteStatusChange)
				wroteOutcome = true;
			else if(effectRecorder.touchedStackEffects() && !wroteCreation)
			{
				line.appendRawString(wroteOutcome ? ", and causing no status change" : ", causing no status change");
				wroteOutcome = true;
			}

			if(!sc.resistedCres.empty())
			{
				line.appendRawString(wroteOutcome ? ", " : ", resisted by ");
				line.appendNumber(sc.resistedCres.size());
				line.appendRawString(wroteOutcome ? " resisted" :
					(sc.resistedCres.size() == 1 ? " target" : " targets"));
				wroteOutcome = true;
			}
			if(!wroteOutcome && !affectedUnits.empty())
			{
				line.appendRawString(", affecting ");
				line.appendNumber(affectedUnits.size());
				line.appendRawString(affectedUnits.size() == 1 ? " target" : " targets");
				wroteOutcome = true;
			}
			if(!wroteOutcome)
			{
				// Location, obstacle and summon spells can resolve successfully without
				// populating affectedUnits.  Do not misreport those casts as failures.
				line.appendRawString(", resolving successfully");
			}
		}
		line.appendRawString(".");
		metamagicDescription.lines.push_back(std::move(line));
		server->apply(metamagicDescription);
	}
	else if((mode == Mode::HERO || mode == Mode::MAGIC_MIRROR)
		&& (owner->getJsonKey() == NEW_HORIZONS_SUMMON_TROLLS_SPELL
			|| owner->getJsonKey() == NEW_HORIZONS_VERDANT_PRISON_SPELL)
		&& !isCounterspellNegated())
	{
		const auto addedUnits = effectRecorder.addedUnits();
		BattleLogMessage summonDescription;
		summonDescription.battleID = battle()->getBattle()->getBattleID();
		MetaString line;
		bool wroteSummon = false;
		const bool verdantPrison = owner->getJsonKey() == NEW_HORIZONS_VERDANT_PRISON_SPELL;
		int32_t verdantStackCount = 0;
		int32_t verdantCreatureCount = 0;
		int64_t verdantAggregateHealth = 0;
		std::optional<CreatureID> verdantCreature;
		for(const auto & added : addedUnits)
		{
			if(!added.natureSummoned)
				continue;
			if(verdantPrison)
			{
				verdantCreature = added.creature;
				++verdantStackCount;
				verdantCreatureCount += added.count;
				verdantAggregateHealth += added.availableHealth;
				continue;
			}
			if(!wroteSummon)
			{
				line.appendTextID(caster->getCasterNameTextID());
				line.appendRawString(" summons ");
			}
			else
				line.appendRawString("; ");
			line.appendNumber(added.count);
			line.appendRawString(" ");
			line.appendName(added.creature, added.count);
			line.appendRawString(" with ");
			line.appendNumber(added.availableHealth);
			line.appendRawString(" aggregate HP as a temporary Nature summon");
			wroteSummon = true;
		}
		if(verdantStackCount > 0)
		{
			line.appendTextID(caster->getCasterNameTextID());
			line.appendRawString(" summons ");
			line.appendNumber(verdantCreatureCount);
			line.appendRawString(" ");
			line.appendName(*verdantCreature, verdantCreatureCount);
			line.appendRawString(" across ");
			line.appendNumber(verdantStackCount);
			line.appendRawString(verdantStackCount == 1 ? " stack with " : " stacks with ");
			line.appendNumber(static_cast<int32_t>(verdantAggregateHealth));
			line.appendRawString(" aggregate HP as temporary Nature summons");
			wroteSummon = true;
		}
		if(wroteSummon)
		{
			line.appendRawString(".");
			summonDescription.lines.push_back(std::move(line));
			server->apply(summonDescription);
		}
	}

	if(sc.activeCast)
	{
		caster->spendMana(server, spellCost);
		const auto divineMandatePairsAfterSpell = mode == Mode::HERO && casterHero && validHeroSide
			? battle()->battleGetDivineMandateStatus(casterSide).completedPairs : divineMandatePairsBeforeSpell;
		const auto chaplainReserve = newHorizonsDivineMandate::chaplainReserveRecovery(
			casterHero, divineMandatePairsBeforeSpell, divineMandatePairsAfterSpell);
		if(chaplainReserve > 0)
		{
			const auto normalBeforeRecovery = casterHero->getNormalSpellPoints();
			casterHero->spendMana(server, -chaplainReserve);
			const auto actualRecovery = std::max<int32_t>(0,
				casterHero->getNormalSpellPoints() - normalBeforeRecovery);
			if(actualRecovery > 0)
			{
				BattleLogMessage reserveDescription;
				reserveDescription.battleID = battle()->getBattle()->getBattleID();
				MetaString line = MetaString::createFromTextID(casterHero->getNameTextID());
				line.appendRawString(" recovers ");
				line.appendNumber(actualRecovery);
				line.appendRawString(" Mana from Chaplain's Reserve.");
				reserveDescription.lines.push_back(std::move(line));
				server->apply(reserveDescription);
			}
		}
		if(recoverBattleMeditation)
		{
			const auto manaBeforeRecovery = casterHero->getNormalSpellPoints();
			const auto recoveryCapacity = newHorizonsWarcasting::battleMeditationRecoveryAmount(manaBeforeRecovery);
			if(recoveryCapacity > 0)
				casterHero->spendMana(server, -recoveryCapacity);
			const auto actualRecovery = std::max<int64_t>(0, casterHero->getNormalSpellPoints() - manaBeforeRecovery);
			if(actualRecovery > 0)
			{
				BattleLogMessage meditationDescription;
				meditationDescription.battleID = battle()->getBattle()->getBattleID();
				MetaString line;
				line.appendTextID(casterHero->getCasterNameTextID());
				line.appendRawString(" recovers ");
				line.appendNumber(static_cast<int32_t>(actualRecovery));
				line.appendRawString(" Mana from Battle Meditation after casting ");
				line.appendTextID(owner->getNameTextID());
				line.appendRawString(".");
				meditationDescription.lines.push_back(std::move(line));
				server->apply(meditationDescription);
			}
		}
		if(getMetamagicManaRefund() > 0)
		{
			const int32_t normalBeforeRefund = casterHero ? casterHero->getNormalSpellPoints() : 0;
			caster->spendMana(server, -getMetamagicManaRefund());
			const int32_t restored = casterHero
				? std::max<int32_t>(0, casterHero->getNormalSpellPoints() - normalBeforeRefund)
				: 0;
			if(restored > 0)
			{
				BattleLogMessage formulaReserveDescription;
				formulaReserveDescription.battleID = battle()->getBattle()->getBattleID();
				MetaString line = MetaString::createFromTextID(casterHero->getNameTextID());
				line.appendRawString(": Formula Reserve restores ");
				line.appendNumber(restored);
				line.appendRawString(" Normal Spell Points as the Metamagic sequence ends.");
				formulaReserveDescription.lines.push_back(std::move(line));
				server->apply(formulaReserveDescription);
			}
		}

		if(!isCounterspellNegated() && sc.manaGained > 0)
		{
			assert(otherHero);
			otherHero->spendMana(server, -sc.manaGained);
		}
	}

	if(originalHeroCast && !sc.counterspellNegated && battleRound >= 0)
	{
		const auto victimSide = battle()->otherSide(originalCasterSide);
		if(hasCounterpressureEffect(effectRecorder, victimSide))
		{
			const auto * recipientHero = battleInfo->getSideHero(victimSide);
			if(hasCounterpressurePerk(recipientHero))
			{
				auto armed = battleInfo->getSpellResponseState(victimSide);
				const auto previous = armed;
				armed.armAt(battleRound);
				if(armed != previous)
				{
					SetSpellResponseState response;
					response.battleID = sc.battleID;
					response.side = victimSide;
					response.state = armed;
					server->apply(response);
				}
			}
		}
	}

	// send empty event to client
	// temporary(?) workaround to force animations to trigger
	StacksInjured fakeEvent;
	fakeEvent.battleID = battle()->getBattle()->getBattleID();
	server->apply(fakeEvent);
}

BattleSide BattleSpellMechanics::effectiveCasterSide() const
{
	return mode == Mode::MAGIC_MIRROR ? battle()->otherSide(casterSide) : casterSide;
}

void BattleSpellMechanics::beforeCast(ServerCallback * server, BattleSpellCast & sc, vstd::RNG & rng, const Target & target)
{
	const bool realityWarp = owner->getJsonKey() == newHorizonsRealityWarp::SPELL_KEY
		&& usesNewHorizonsMagicV3();
	// Countering cleanup and the first damage event may remove a later stack's
	// debuff source. Capture every count before either can change the battlefield.
	if(newHorizonsPandemonium::enabled(*this))
		capturePandemoniumDebuffs();
	affectedUnits.clear();
	const bool newHorizonsMassRegeneration = isNewHorizonsMassRegenerationSpell(owner,
		battle()->getBattle()->getMagicRules());
	const bool newHorizonsSoulChain = isNewHorizonsSoulChainSpell(owner,
		battle()->getBattle()->getMagicRules());

	Target spellTarget = transformSpellTarget(target);
	const bool naturesWrath = newHorizonsNaturesWrath::enabled(*this);
	// Unlike generic negative spells, this mixed current rolls only for hostile
	// members of its already captured route. Routing itself never queries RNG.
	if(naturesWrath)
		effectsToApply = effects->prepare(this, target, spellTarget);
	const auto wrathConductors = naturesWrath ? collectTargets() : battle::Units{};
	if(naturesWrath)
		for(const auto * unit : wrathConductors)
			sc.affectedCres.push_back(unit->unitId());

	std::vector <const battle::Unit *> resisted;

	// Keep the original eager draw order for every battlefield unit. Consumers
	// resolve this cached result lazily when they know the unit is an actual,
	// eligible recipient (including chain routing and scripted collateral).
	resistanceRolls.clear();
	resistantUnitIds.clear();
	if((isNegativeSpell() || naturesWrath || realityWarp) && isMagicalEffect())
	{
		//magic resistance
		const auto resistanceCandidates = naturesWrath ? wrathConductors : battle()->battleGetAllUnits(false);
		for(const auto * unit : resistanceCandidates)
		{
			if(realityWarp && (battle()->battleGetOwner(unit) == getCasterColor()
				|| std::ranges::none_of(spellTarget, [unit](const Destination & destination)
				{
					return destination.unitValue == unit;
				})))
				continue;
			if(isMagicalEffect() && isSpellLocked(unit))
			{
				resistantUnitIds.insert(unit->unitId());
				continue;
			}
			if(naturesWrath && (ownerMatches(unit, true) || unit->isInvincible() || !isReceptive(unit)))
				continue;
			// Life Drain has a hostile damage target and a friendly healing target.
			// The latter is not resisting a hostile effect; Spell Lock above still
			// blocks either half of the paired spell.
			if(isNewHorizonsLifeDrainSpell(owner, battle()->getBattle()->getMagicRules())
				&& unit->unitSide() == casterSide)
				continue;
			// Friendly Blink is ordinary negative magic for target immunity, but
			// an ally is not resisting a hostile magical effect.
			if(isNewHorizonsBlinkSpell(owner) && unit->unitSide() == casterSide)
				continue;
			const int prob = std::min(unit->magicResistance(), 100); //probability of resistance in %
			const bool didResist = rng.nextInt(0, 99) < prob;
			resistanceRolls.push_back({unit->unitId(), prob, didResist, false});
			if(didResist)
				resistantUnitIds.insert(unit->unitId());
		}
	}

	auto filterResisted = [this](const battle::Unit * unit) -> bool
	{
		return wouldResist(unit);
	};

	auto filterUnit = [&](const battle::Unit * unit)
	{
		// Spell Lock is absolute and polarity-independent: it blocks damage,
		// beneficial magic, hostile magic, dispels, and other combat spells.
		// Treat it as resistance here so every effect category uses the same
		// authoritative target filtering and existing resisted logging.
		if((isMagicalEffect() && isSpellLocked(unit)) || filterResisted(unit))
			resisted.push_back(unit);
		else
			affectedUnits.push_back(unit);
	};

	if (!target.empty() && !realityWarp)
	{
		const battle::Unit * targetedUnit = battle()->battleGetUnitByPos(target.front().hexValue, true);
		if ((!isMagicalEffect() || !isSpellLocked(targetedUnit)) && isReflected(server, targetedUnit, rng)) {
			reflect(server, sc, rng, targetedUnit);
			return;
			}
	}

	//prepare targets
	if(!naturesWrath)
		effectsToApply = effects->prepare(this, target, spellTarget);
	const auto spellLockedUnits = filterSpellLockedEffects(target);
	if(newHorizonsMassRegeneration)
		filterMassRegenerationTargets(effectsToApply);


	auto unitTargets = collectTargets();

	//process them
	for(const auto * unit : unitTargets)
	{
		filterUnit(unit);
	}
	for(const auto * unit : spellLockedUnits)
		if(!vstd::contains(resisted, unit))
			resisted.push_back(unit);

	//and update targets
	for(auto & p : effectsToApply)
	{
		if(naturesWrath)
		{
			for(auto & destination : p.second)
				if(destination.unitValue && vstd::contains(resisted, destination.unitValue))
					destination = Destination(destination.hexValue);
			continue;
		}
		vstd::erase_if(p.second, [&](const Destination & d)
		{
			if(!d.unitValue)
				return false;
			return vstd::contains(resisted, d.unitValue);
		});
	}
	// Soul Chain is an ordered relationship, not a generic area effect. If the
	// primary resists, discard the entire effect so a remaining secondary can
	// never slide into the primary slot. Resisted secondaries are simply omitted.
	if(newHorizonsSoulChain && !target.empty() && target.front().unitValue
		&& vstd::contains(resisted, target.front().unitValue))
		effectsToApply.clear();
	// An exchange is indivisible: resistance must never leave a one-endpoint
	// effect that accidentally mutates only half of the relationship.
	if(realityWarp && !resisted.empty())
		effectsToApply.clear();

	for(const auto * unit : resisted)
		sc.resistedCres.insert(unit->unitId());

	// Scripted secondary hits must observe these same rolls during application.
	// The cast clears this transient set after all effects have resolved.
}

battle::Units BattleSpellMechanics::filterSpellLockedEffects(const Target & aimPoint)
{
	battle::Units rejected;
	if(!isMagicalEffect())
		return rejected;
	if(newHorizonsNaturesWrath::enabled(*this))
	{
		for(auto & effect : effectsToApply)
			for(auto & destination : effect.second)
			{
				const auto * unit = destination.unitValue;
				if(unit && isSpellLocked(unit))
				{
					if(!vstd::contains(rejected, unit))
						rejected.push_back(unit);
					destination = Destination(destination.hexValue);
				}
			}
		return rejected;
	}

	const auto targetTypes = getTargetTypes();
	const bool compoundCreatureTarget = targetTypes == std::vector<AimType>{AimType::CREATURE, AimType::LOCATION}
		|| targetTypes == std::vector<AimType>{AimType::CREATURE, AimType::CREATURE};
	const bool compoundTargetLocked = compoundCreatureTarget && vstd::contains_if(aimPoint,
		[](const Destination & destination)
		{
			return destination.unitValue && isSpellLocked(destination.unitValue);
		});
	if(compoundTargetLocked)
	{
		for(const auto & destination : aimPoint)
			if(destination.unitValue && isSpellLocked(destination.unitValue)
				&& !vstd::contains(rejected, destination.unitValue))
				rejected.push_back(destination.unitValue);
		// Transforming a locked unit through a Lua effect may already have
		// removed it from the transformed destinations. Use the submitted aim
		// to identify rejection and drop the entire paired effect before apply.
		effectsToApply.clear();
		return rejected;
	}
	for(auto effect = effectsToApply.begin(); effect != effectsToApply.end();)
	{
		auto & destinations = effect->second;
		const bool compoundDestinationLocked = compoundCreatureTarget && vstd::contains_if(destinations,
			[](const Destination & destination)
			{
				return destination.unitValue && isSpellLocked(destination.unitValue);
			});
		if(compoundDestinationLocked)
		{
			for(const auto & destination : destinations)
				if(destination.unitValue && isSpellLocked(destination.unitValue)
					&& !vstd::contains(rejected, destination.unitValue))
					rejected.push_back(destination.unitValue);
			// Compound effects (notably Teleport and Sacrifice) consume a paired
			// target contract. Dropping only its destinations still invokes the
			// effect script with an empty target and can dereference target[1].
			effect = effectsToApply.erase(effect);
			continue;
		}
		vstd::erase_if(destinations, [&](const Destination & destination)
		{
			const auto * unit = destination.unitValue;
			if(!unit || !isSpellLocked(unit))
				return false;
			if(!vstd::contains(rejected, unit))
				rejected.push_back(unit);
			return true;
		});
		++effect;
	}
	return rejected;
}

bool BattleSpellMechanics::isReflected(ServerCallback * server, const battle::Unit * unit, vstd::RNG & rng)
{
	if (unit == nullptr)
		return false;
	const std::vector<int> directSpellRange = { 0 };
	bool isDirectSpell = !isMassive() && owner -> getLevelInfo(getRangeLevel()).range == directSpellRange;
	bool spellIsReflectable = isDirectSpell && (mode == Mode::HERO || mode == Mode::MAGIC_MIRROR) && isNegativeSpell();
	bool targetCanReflectSpell = spellIsReflectable && unit->getAllBonuses(Selector::type()(BonusType::MAGIC_MIRROR))->size()>0;
	const bool friendlyBlink = isNewHorizonsBlinkSpell(owner) && unit->unitSide() == casterSide;
	if(!targetCanReflectSpell || friendlyBlink)
		return false;

	const int chance = unit->valOfBonuses(BonusType::MAGIC_MIRROR);
	const auto draw = [&rng, chance]()
	{
		return rng.nextInt(0, 99) < chance;
	};
	const BattleSide targetControllerSide = battle()->playerToSide(battle()->battleGetOwner(unit));
	const BattleSide affectedSide = effectiveCasterSide();
	const bool hostileTarget = (targetControllerSide == BattleSide::ATTACKER || targetControllerSide == BattleSide::DEFENDER)
		&& (affectedSide == BattleSide::ATTACKER || affectedSide == BattleSide::DEFENDER)
		&& targetControllerSide != affectedSide;
	if(!server || !hostileTarget || chance <= 0 || chance >= 100)
		return draw();

	// A successful redirect harms the army currently casting the spell. A
	// reflected cast changes the effective side even though casterSide remains
	// the original hero's side.
	return server->resolveAdverseCombatRoll(battle()->getBattle()->getBattleID(), affectedSide,
		true, true, draw);
}

void BattleSpellMechanics::reflect(ServerCallback * server, BattleSpellCast & sc, vstd::RNG & rng, const battle::Unit * unit)
{
	auto otherSide = battle()->otherSide(unit->unitSide());
	auto newTarget = getRandomUnit(rng, otherSide);
	if (newTarget == nullptr)
		throw std::runtime_error("Failed to find random unit to reflect spell!");
	auto reflectedTo = newTarget->getPosition();

	mode = Mode::MAGIC_MIRROR;
	sc.reflectedCres.insert(unit->unitId());
	sc.tile = reflectedTo;

	if (!isReceptive(newTarget))
		sc.resistedCres.insert(newTarget->unitId());    //A spell can be reflected to then resisted by an immune unit. Consistent with the original game.

	beforeCast(server, sc, rng, { Destination(reflectedTo) });
}

const battle::Unit * BattleSpellMechanics::getRandomUnit(vstd::RNG & rng, const BattleSide & side)
{
	auto targets = battle()->getBattle()->getUnitsIf([&side](const battle::Unit * unit)
	{
		return unit->unitSide() == side && unit->isValidTarget(false) &&
			!unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
	});
	return !targets.empty() ? (*RandomGeneratorUtil::nextItem(targets, rng)) : nullptr;
}

void BattleSpellMechanics::castEval(ServerCallback * server, const Target & target)
{
	if(eventSnapshot && !eventSnapshot->hasSpellcraftTargetSnapshot()
		&& mode == Mode::HERO && getHeroCaster()
		&& getHeroCaster()->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsSpellcraft::CONCENTRATION)))
	{
		eventSnapshot->mechanicsForTarget(target)->castEval(server, target);
		return;
	}
	clearPandemoniumDebuffs();
	const auto clearPandemoniumSnapshot = vstd::makeScopeGuard([this]()
	{
		clearPandemoniumDebuffs();
	});
	// Evaluation may prepare the same scripted effects as a real cast, but it
	// must never spend an authoritative adverse-roll allowance or advance the
	// combat RNG through wouldResist.
	auto * previousResistanceServer = activeResistanceServer;
	auto * previousResistanceRng = activeResistanceRng;
	const auto restoreResistanceContext = vstd::makeScopeGuard([this, previousResistanceServer, previousResistanceRng]()
	{
		activeResistanceServer = previousResistanceServer;
		activeResistanceRng = previousResistanceRng;
	});
	activeResistanceServer = nullptr;
	activeResistanceRng = nullptr;

	const bool newHorizonsRegeneration = isNewHorizonsRegenerationSpell(owner,
		battle()->getBattle()->getMagicRules());
	const bool newHorizonsMassRegeneration = isNewHorizonsMassRegenerationSpell(owner,
		battle()->getBattle()->getMagicRules());
	affectedUnits.clear();
	//TODO: evaluate caster updates (mana usage etc.)
	//TODO: evaluate random values
	if(isNewHorizonsStormOfDaggers()
		&& (!setStormOfDaggersTargetCount(static_cast<int32_t>(target.size()))
			|| !canBeCastAt(target)))
		return;
	if(owner->getJsonKey() == newHorizonsRealityWarp::SPELL_KEY && !canBeCastAt(target))
		return;
	const bool completedHeroProjection = server && mode == Mode::HERO
		&& (casterSide == BattleSide::ATTACKER || casterSide == BattleSide::DEFENDER)
		&& getHeroCaster() && canBeCastAt(target);
	const auto * projectedHeroCaster = completedHeroProjection ? getHeroCaster() : nullptr;
	const auto projectedCasterSide = casterSide;
	const auto projectedBattleInfo = battle()->getBattle();
	const int32_t projectedRound = projectedBattleInfo->getRound();
	const bool projectedOriginalHeroCast = completedHeroProjection && projectedRound >= 0;
	SpellResponseState projectedResponseAfterCast;
	bool consumeProjectedResponse = false;
	if(projectedOriginalHeroCast && hasCounterpressurePerk(projectedHeroCaster))
	{
		projectedResponseAfterCast = projectedBattleInfo->getSpellResponseState(projectedCasterSide);
		consumeProjectedResponse = projectedResponseAfterCast.consumeAt(projectedRound);
	}
	if(completedHeroProjection)
	{
		registerOverwhelmingFormulaCast(server);
		if(projectedHeroCaster == projectedBattleInfo->getSideHero(projectedCasterSide))
			if(const auto receipt = newHorizonsCrossSchoolFormula::acceptedReceipt(*projectedBattleInfo, projectedCasterSide, getSpellId()))
				server->recordCrossSchoolFormulaCast(projectedCasterSide, *receipt);
	}
	if(newHorizonsPandemonium::enabled(*this))
		capturePandemoniumDebuffs();
	Target spellTarget = transformSpellTarget(target);

	effectsToApply = effects->prepare(this, target, spellTarget);
	filterSpellLockedEffects(target);
	if(newHorizonsMassRegeneration)
		filterMassRegenerationTargets(effectsToApply);

	if(completedHeroProjection
		&& newHorizonsSpellcraft::extendAvailable(*projectedBattleInfo, projectedCasterSide, *owner, getEffectLevel()))
		server->recordExtendSpellCast(projectedCasterSide);
	auto unitTargets = collectTargets();

	auto selector = std::bind(&BattleSpellMechanics::counteringSelector, this, _1);

	std::copy(std::begin(unitTargets), std::end(unitTargets), std::back_inserter(affectedUnits));

	std::optional<EffectPacketRecorder> effectRecorder;
	ServerCallback * effectServer = server;
	if(server)
	{
		const auto acceptedSpellId = owner->getId();
		const auto effectSpellId = newHorizonsMagic::spellVariantBase(
			battle()->getBattle()->getMagicRules(), acceptedSpellId);
		// Keep hypothetical/reflected casts aligned with the live effective source.
		const auto effectCasterOwner = battle()->sideToPlayer(effectiveCasterSide());
		effectRecorder.emplace(*server, *battle(), *this, effectCasterOwner,
			acceptedSpellId, effectSpellId,
			projectedOriginalHeroCast && !isCounterspellNegated()
				&& hasCounterpressurePerk(projectedBattleInfo->getSideHero(battle()->otherSide(projectedCasterSide))));
		effectServer = &*effectRecorder;
	}

	doRemoveEffects(effectServer, affectedUnits, selector);

	for(auto & p : effectsToApply)
		p.first->apply(effectServer, this, p.second);

	if(newHorizonsRegeneration && mode == Mode::HERO && getHeroCaster())
		applyRegenerationRateSnapshot(effectServer, battle(), affectedUnits, newHorizonsRegenerationRate(*this));

	if(completedHeroProjection)
	{
		server->recordCompletedHeroSpellCast(casterSide, battle()->battleGetSpellLevel(getSpellId()));
		if(consumeProjectedResponse)
		{
			SetSpellResponseState consumedResponse;
			consumedResponse.battleID = projectedBattleInfo->getBattleID();
			consumedResponse.side = projectedCasterSide;
			consumedResponse.state = projectedResponseAfterCast;
			server->apply(consumedResponse);
		}

		if(projectedOriginalHeroCast && !isCounterspellNegated() && effectRecorder)
		{
			const auto victimSide = battle()->otherSide(projectedCasterSide);
			if(hasCounterpressureEffect(*effectRecorder, victimSide))
			{
				const auto * recipientHero = projectedBattleInfo->getSideHero(victimSide);
				if(hasCounterpressurePerk(recipientHero))
				{
					auto armed = projectedBattleInfo->getSpellResponseState(victimSide);
					const auto previous = armed;
					armed.armAt(projectedRound);
					if(armed != previous)
					{
						SetSpellResponseState response;
						response.battleID = projectedBattleInfo->getBattleID();
						response.side = victimSide;
						response.state = armed;
						server->apply(response);
					}
				}
			}
		}
	}
}

battle::Units BattleSpellMechanics::collectTargets() const
{
	// preserve effect (e.g. chain-lightning hop) order while removing duplicates, so the client can
	// reconstruct the target sequence from BattleSpellCast::affectedCres
	battle::Units result;

	for(const auto & p : effectsToApply)
	{
		for(const Destination & d : p.second)
			if(d.unitValue && !vstd::contains(result, d.unitValue))
				result.push_back(d.unitValue);
	}

	return result;
}

void BattleSpellMechanics::doRemoveEffects(ServerCallback * server, const battle::Units & targets, const CSelector & selector)
{
	SetStackEffect sse;
	sse.battleID = battle()->getBattle()->getBattleID();

	for(const auto * unit : targets)
	{
		std::vector<Bonus> buffer;
		auto bl = unit->getBonuses(selector);

		for(const auto & item : *bl)
			buffer.emplace_back(*item);

		if(!buffer.empty())
			sse.toRemove.emplace_back(unit->unitId(), buffer);
	}

	if(!sse.toRemove.empty())
		server->apply(sse);
}

bool BattleSpellMechanics::counteringSelector(const Bonus * bonus) const
{
	// Opt-in Cure's Lua dispel removes only the chosen source group. Do not also
	// apply the old spell-counter table (notably Stone Gaze) before effects run.
	if(isNewHorizonsCure())
		return false;

	if(bonus->source != BonusSource::SPELL_EFFECT)
		return false;

	for(const SpellID & id : owner->counteredSpells)
	{
		if(bonus->sid.as<SpellID>() == id)
			return true;
	}

	return false;
}

BattleHexArray BattleSpellMechanics::spellRangeInHexes(const BattleHex & centralHex) const
{
	using namespace SRSLPraserHelpers;

	BattleHexArray ret;
	std::vector<int> rng = owner->getLevelInfo(getRangeLevel()).range;

	for(auto & elem : rng)
	{
		std::set<ui16> curLayer = getInRange(centralHex.toInt(), elem, elem);
		//adding obtained hexes
		for(const auto & curLayer_it : curLayer)
			ret.insert(curLayer_it);
	}

	return ret;
}

Target BattleSpellMechanics::transformSpellTarget(const Target & aimPoint) const
{
	if(usesNewHorizonsEarthquake())
		return aimPoint.size() == 1 && aimPoint.front().hexValue.isValid() ? aimPoint : Target{};
	Target spellTarget;
	if(battle() && battle()->getBattle()
		&& isNewHorizonsVengefulVinesSpell(owner, battle()->getBattle()->getMagicRules()))
	{
		const auto path = newHorizonsVengefulVines::footprint(aimPoint);
		if(path.size() != 3)
			return {};

		spellTarget.reserve(path.size());
		for(const auto & hex : path)
			spellTarget.emplace_back(hex);
		return spellTarget;
	}

	if(isNewHorizonsStormOfDaggers()
		|| isNewHorizonsLifeDrainSpell(owner, battle()->getBattle()->getMagicRules())
		|| isNewHorizonsSoulChainSpell(owner, battle()->getBattle()->getMagicRules())
		|| owner->getJsonKey() == newHorizonsRealityWarp::SPELL_KEY)
	{
		spellTarget.reserve(aimPoint.size());
		for(const auto & selected : aimPoint)
		{
			const auto * unit = selected.unitValue
				? battle()->battleGetUnitByID(selected.unitValue->unitId()) : nullptr;
			if(unit)
				spellTarget.emplace_back(unit);
			else
				spellTarget.emplace_back(BattleHex::INVALID);
		}
		return spellTarget;
	}

	if(aimPoint.empty())
	{
		logGlobal->error("Aimed spell cast with no destination.");
	}
	else
	{
		const Destination & primary = aimPoint.at(0);
		BattleHex aimPointHex = primary.hexValue;

		//transform primary spell target with spell range (if it`s valid), leave anything else to effects

		if(aimPointHex.isValid())
		{
			auto spellRange = spellRangeInHexes(aimPointHex);
			const auto targetTypes = getTargetTypes();
			// A single-hex creature spell must retain an explicitly selected unit.
			// Resolving it again by hex can redirect resurrection to another corpse.
			if(primary.unitValue && !targetTypes.empty() && targetTypes.front() == AimType::CREATURE
				&& spellRange.size() == 1 && spellRange.front() == aimPointHex)
			{
				const auto * selected = battle()->battleGetUnitByID(primary.unitValue->unitId());
				// Resolve through this battle: an AI aim may reference the live unit,
				// while its projected state has already changed. Never fall back by hex.
				spellTarget.push_back(selected ? Destination(selected) : Destination(BattleHex::INVALID));
			}
			else
				for(const auto & hex : spellRange)
					spellTarget.push_back(Destination(hex));
		}
	}

	if(spellTarget.empty())
		spellTarget.push_back(Destination(BattleHex::INVALID));

	return spellTarget;
}

std::vector<AimType> BattleSpellMechanics::getTargetTypes() const
{
	if(battle() && battle()->getBattle()
		&& isNewHorizonsVengefulVinesSpell(owner, battle()->getBattle()->getMagicRules()))
		return {AimType::LOCATION, AimType::LOCATION, AimType::LOCATION};

	auto ret = BaseMechanics::getTargetTypes();

	if(!ret.empty())
	{
		effects->forEachEffect(getEffectLevel(), [&](const effects::Effect * e, bool & stop)
		{
			e->adjustTargetTypes(ret, this);
			stop = ret.empty();
		});
	}

	return ret;
}

bool BattleSpellMechanics::isReceptive(const battle::Unit * target) const
{
	if(target && owner->getJsonKey() == newHorizonsRealityWarp::SPELL_KEY && usesNewHorizonsMagicV3())
	{
		if(isMagicalEffect() && isSpellLocked(target))
			return false;
		// The paired cast rolls hostile resistance once during beforeCast;
		// captured-effect recipient checks themselves are deterministic.
		if(const auto * conditions = dynamic_cast<const TargetCondition *>(targetCondition.get()))
			return conditions->isReceptiveIgnoringMagicResistance(this, target);
	}
	if(target && newHorizonsPandemonium::enabled(*this))
	{
		if(isMagicalEffect() && isSpellLocked(target))
			return false;
		// Preview and damage adjustment retain all immunity conditions, while
		// actual casts consume the existing cached resistance roll exactly once.
		if(const auto * conditions = dynamic_cast<const TargetCondition *>(targetCondition.get()))
			return conditions->isReceptiveIgnoringMagicResistance(this, target);
	}
	if(target && newHorizonsNaturesWrath::enabled(*this))
	{
		if(isMagicalEffect() && isSpellLocked(target))
			return false;
		// The ordered hostile-recipient consumer handles resistance, not the
		// routing or heal immunity check. All other immunity conditions remain.
		if(const auto * conditions = dynamic_cast<const TargetCondition *>(targetCondition.get()))
			return conditions->isReceptiveIgnoringMagicResistance(this, target);
	}
	if(target && newHorizonsPuppetMaster::isMentalControlSpell(owner->getJsonKey())
		&& newHorizonsPuppetMaster::hasLucidity(target))
		return false;

	if(target && isNewHorizonsBlinkSpell(owner) && usesNewHorizonsMagicV3()
		&& target->unitSide() == casterSide)
	{
		if(isMagicalEffect() && isSpellLocked(target))
			return false;

		if(const auto * conditions = dynamic_cast<const TargetCondition *>(targetCondition.get()))
			return conditions->isReceptiveIgnoringMagicResistance(this, target);
	}

	return target && (!isMagicalEffect() || !isSpellLocked(target))
		&& targetCondition->isReceptive(this, target);
}

bool BattleSpellMechanics::isSmart() const
{
	return mode != Mode::MAGIC_MIRROR && BaseMechanics::isSmart();
}

bool BattleSpellMechanics::wouldResist(const battle::Unit * unit) const
{
	if(!unit)
		return false;

	const auto unitId = unit->unitId();
	auto resistance = std::ranges::find(resistanceRolls, unitId, &ResistanceRoll::unitId);
	if(resistance == resistanceRolls.end())
		return resistantUnitIds.contains(unitId);
	if(resistance->resolved || !activeResistanceServer || !activeResistanceRng)
		return resistance->resisted;

	const BattleSide recipientControllerSide = battle()->playerToSide(battle()->battleGetOwner(unit));
	const BattleSide spellCasterSide = effectiveCasterSide();
	const bool validSides = (recipientControllerSide == BattleSide::ATTACKER || recipientControllerSide == BattleSide::DEFENDER)
		&& (spellCasterSide == BattleSide::ATTACKER || spellCasterSide == BattleSide::DEFENDER);
	if((!isNegativeSpell() && !newHorizonsNaturesWrath::enabled(*this)) || !isMagicalEffect() || !validSides
		|| recipientControllerSide == spellCasterSide
		|| !unit->isValidTarget(false) || !isReceptive(unit)
		|| resistance->probability <= 0 || resistance->probability >= 100)
		return resistance->resisted;

	// Chain Lightning and scripted collateral call this method at their actual
	// resistance-consumer point. Cache the result before entering the server
	// resolver so repeated queries (including the normal target filter below)
	// cannot spend the side's one-use reroll twice.
	const int probability = resistance->probability;
	const bool firstResult = resistance->resisted;
	resistance->resolved = true;
	auto * const server = activeResistanceServer;
	auto * const rng = activeResistanceRng;
	const auto draw = [rng, firstResult, probability, firstDraw = true]() mutable
	{
		if(firstDraw)
		{
			firstDraw = false;
			return firstResult;
		}
		return rng->nextInt(0, 99) < probability;
	};
	const bool finalResult = server->resolveAdverseCombatRoll(battle()->getBattle()->getBattleID(),
		recipientControllerSide, true, false, draw);
	resistance->resisted = finalResult;
	if(finalResult)
		resistantUnitIds.insert(unitId);
	else
		resistantUnitIds.erase(unitId);
	return finalResult;
}

BattleHexArray BattleSpellMechanics::rangeInHexes(const BattleHex & centralHex) const
{
	if(isMassive() || !centralHex.isValid())
		return BattleHexArray();

	Target aimPoint;
	aimPoint.push_back(Destination(centralHex));

	Target spellTarget = transformSpellTarget(aimPoint);

	BattleHexArray effectRange;

	effects->forEachEffect(getEffectLevel(), [&](const effects::Effect * effect, bool & stop)
	{
		if(!effect->indirect)
		{
			effect->adjustAffectedHexes(effectRange, this, spellTarget);
		}
	});

	return effectRange;
}

Target BattleSpellMechanics::canonicalizeTarget(const Target & aim) const
{
	return transformSpellTarget(aim);
}

const Spell * BattleSpellMechanics::getSpell() const
{
	return owner;
}


}
