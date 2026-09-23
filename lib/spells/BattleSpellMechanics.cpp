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

#include "Problem.h"
#include "CSpell.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSorcery.h"

#include "../battle/IBattleState.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/Unit.h"
#include "../bonuses/Updaters.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../networkPacks/SetStackEffect.h"
#include "../CStack.h"

#include <vstd/RNG.h>

namespace spells
{

namespace
{

bool isLivingCureTarget(const battle::Unit * unit)
{
	return unit && unit->isValidTarget(false) && unit->alive()
		&& !unit->hasBonusOfType(BonusType::UNDEAD)
		&& !unit->hasBonusOfType(BonusType::NON_LIVING)
		&& !unit->hasBonusOfType(BonusType::MECHANICAL)
		&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
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
		bool clone;
		int64_t phantomInitialIntegrity;
		int64_t phantomIntegrity;
	};

	EffectPacketRecorder(ServerCallback & delegate, const IBattleInfoCallback & battle)
		: delegate(delegate)
		, battle(battle)
	{
	}

	void complain(const std::string & problem) override { delegate.complain(problem); }
	bool describeChanges() const override { return delegate.describeChanges(); }
	vstd::RNG * getRNG() override { return delegate.getRNG(); }
	bool rollCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor, int percentageChance) override
	{
		return delegate.rollCombatAbility(battle, actor, percentageChance);
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
		if(const auto * injured = dynamic_cast<const StacksInjured *>(&pack))
			record(*injured);
		delegate.apply(pack);
	}
	void apply(BattleLogMessage & pack) override { delegate.apply(pack); }
	void apply(BattleStackMoved & pack) override { delegate.apply(pack); }
	void apply(BattleUnitsChanged & pack) override { record(pack); }
	void apply(SetStackEffect & pack) override { record(pack); }
	void apply(StacksInjured & pack) override
	{
		record(pack);
		delegate.apply(pack);
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
			result.push_back({unitId, unit->creatureId(), unit->getCount(), unit->isClone(),
				unit->getPhantomInitialIntegrity(), unit->getPhantomIntegrity()});
		}
		return result;
	}
	bool touchedStackEffects() const { return stackEffectsTouched; }
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
	const IBattleInfoCallback & battle;
	std::vector<BattleStackAttacked> recordedInjuries;
	std::vector<HealingChange> recordedHealingChanges;
	std::vector<uint32_t> addedUnitIds;
	std::vector<EffectChange> recordedEffectChanges;
	std::vector<std::pair<uint32_t, std::vector<EffectState>>> initialEffectStates;
	bool effectChangesFinalized = false;
	bool stackEffectsTouched = false;

	void record(const StacksInjured & pack)
	{
		recordedInjuries.insert(recordedInjuries.end(), pack.stacks.begin(), pack.stacks.end());
	}

	void record(BattleUnitsChanged & pack)
	{
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
				addedUnitIds.push_back(change.id);
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
			&& left.bonusOwner == right.bonusOwner;
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

		for(const auto & bonus : *unit->getBonuses(Selector::sourceType()(BonusSource::SPELL_EFFECT)))
		{
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

		for(const auto unitId : touched)
		{
			if(std::ranges::none_of(initialEffectStates, [unitId](const auto & entry)
			{
				return entry.first == unitId;
			}))
				initialEffectStates.emplace_back(unitId, snapshot(unitId));
		}

		delegate.apply(pack);
		effectChangesFinalized = false;
	}
};

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

BattleSpellMechanics::BattleSpellMechanics(const IBattleCast * event,
										   std::shared_ptr<effects::Effects> effects_,
										   std::shared_ptr<IReceptiveCheck> targetCondition_):
	BaseMechanics(event),
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
	auto callback = [&](const effects::Effect * effect, bool & stop)
	{
		if(indirect == effect->indirect)
		{
			if(ignoreImmunity)
			{
				effect->apply(server, this, targets);
			}
			else
			{
				Target filtered = effect->filterTarget(this, targets);
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
	if(getCureAffliction() != SpellID::NONE && !isNewHorizonsCure())
		return adaptGenericProblem(problem);

	if(mode == Mode::HERO && isMetamagicFollowup()
		&& !battle()->battleCanUseMetamagicSpell(casterSide, owner->getId(), isMetamagicGrand()))
		return adaptGenericProblem(problem);

	if(!newHorizonsMagic::spellAllowedByBattleRoster(*battle(), owner->getId()))
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
	if(selectiveDispel && (mode != Mode::HERO || owner->getId() != SpellID::DISPEL || !castingHero
		|| !castingHero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel")))
		return adaptGenericProblem(problem);
	if(massSlow && (mode != Mode::HERO || owner->getId() != SpellID::SLOW || !castingHero
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
				int requiredMana = battle()->battleGetSpellCost(owner, castingHero, massSlow ? 3 : 1);
				if(isMetamagicFollowup()
					&& newHorizonsMagic::hasMetamagicPerk(castingHero, newHorizonsMagic::METAMAGIC_ARCANE_ECONOMY))
					requiredMana = std::max(1, requiredMana - 2);
				if(adjustableMagicArrow)
					requiredMana += selectedOvercharge;
				if(castingHero->mana < requiredMana) //not enough mana
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

			if(!newHorizonsMagic::cureAfflictions(rules, unit).empty()
				|| unit->getAvailableHealth() < unit->getTotalHealth())
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

	Target spellTarget = transformSpellTarget(target);
	if(isNewHorizonsCure())
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
		if(selected == SpellID::NONE)
		{
			if(!afflictions.empty())
				return false;
		}
		else if(!vstd::contains(afflictions, selected))
		{
			return false;
		}
	}

	const battle::Unit * mainTarget = nullptr;

	if(spellTarget.front().unitValue)
	{
		mainTarget = spellTarget.front().unitValue;
	}
	else if(spellTarget.front().hexValue.isValid())
	{
		mainTarget = battle()->battleGetUnitByPos(target.front().hexValue, true);
	}

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
	if(newHorizonsMagic::isCounterspell(owner))
		return true;

	return effects->applicable(problem, this, target, spellTarget);
}

std::vector<const CStack *> BattleSpellMechanics::getAffectedStacks(const Target & target) const
{
	Target spellTarget = transformSpellTarget(target);

	Target all;

	effects->forEachEffect(getEffectLevel(), [&all, &target, &spellTarget, this](const effects::Effect * e, bool & stop)
	{
		Target one = e->transformTarget(this, target, spellTarget);
		vstd::concatenate(all, one);
	});

	std::set<const CStack *> stacks;

	for(const Destination & dest : all)
	{
		if(dest.unitValue && !dest.unitValue->isInvincible())
		{
			//FIXME: remove and return battle::Unit
			stacks.insert(battle()->battleGetStackByID(dest.unitValue->unitId(), false));
		}
	}

	std::vector<const CStack *> res;
	std::copy(stacks.begin(), stacks.end(), std::back_inserter(res));
	return res;
}

void BattleSpellMechanics::cast(ServerCallback * server, const Target & target)
{
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
		spellCost = battle()->battleGetSpellCost(owner, casterHero, isMassSlow() ? 3 : 1);
		if(isMetamagicFollowup()
			&& newHorizonsMagic::hasMetamagicPerk(casterHero, newHorizonsMagic::METAMAGIC_ARCANE_ECONOMY))
			spellCost = std::max(1, spellCost - 2);
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

	if(!isCounterspellNegated())
		beforeCast(sc, *server->getRNG(), target);

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

	EffectPacketRecorder effectRecorder(*server, *battle());
	if(!isCounterspellNegated())
		doRemoveEffects(&effectRecorder, affectedUnits, std::bind(&BattleSpellMechanics::counteringSelector, this, _1));

	for(auto & unit : affectedUnits)
		sc.affectedCres.push_back(unit->unitId());

	if(!castDescription.lines.empty())
		server->apply(castDescription);

	server->apply(sc);

	if(!isCounterspellNegated())
	{
		for(auto & p : effectsToApply)
			p.first->apply(&effectRecorder, this, p.second);
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
			const bool ordinarySummon = getSpellId() == SpellID::SUMMON_FIRE_ELEMENTAL
				|| getSpellId() == SpellID::SUMMON_EARTH_ELEMENTAL
				|| getSpellId() == SpellID::SUMMON_WATER_ELEMENTAL
				|| getSpellId() == SpellID::SUMMON_AIR_ELEMENTAL;
			if(ordinarySummon || getSpellId() == SpellID::CLONE || phantomArmy)
			{
				bool wroteAddedUnit = false;
				for(const auto & added : addedUnits)
				{
					if(getSpellId() == SpellID::CLONE && !added.clone)
						continue;
					if(phantomArmy && added.phantomInitialIntegrity <= 0)
						continue;
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
					if(phantomArmy)
					{
						line.appendRawString(" with ");
						line.appendNumber(added.phantomIntegrity);
						line.appendRawString("/");
						line.appendNumber(added.phantomInitialIntegrity);
						line.appendRawString(" integrity for ");
						line.appendNumber(newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS);
						line.appendRawString(" rounds");
					}
					wroteAddedUnit = true;
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

	if(sc.activeCast)
	{
		caster->spendMana(server, spellCost);
		if(getMetamagicManaRefund() > 0)
			caster->spendMana(server, -getMetamagicManaRefund());

		if(!isCounterspellNegated() && sc.manaGained > 0)
		{
			assert(otherHero);
			otherHero->spendMana(server, -sc.manaGained);
		}
	}

	// send empty event to client
	// temporary(?) workaround to force animations to trigger
	StacksInjured fakeEvent;
	fakeEvent.battleID = battle()->getBattle()->getBattleID();
	server->apply(fakeEvent);
}

void BattleSpellMechanics::beforeCast(BattleSpellCast & sc, vstd::RNG & rng, const Target & target)
{
	affectedUnits.clear();

	Target spellTarget = transformSpellTarget(target);

	std::vector <const battle::Unit *> resisted;

	resistantUnitIds.clear();
	if(isNegativeSpell() && isMagicalEffect())
	{
		//magic resistance
		for (const auto * unit : battle()->battleGetAllUnits(false))
		{
			const int prob = std::min(unit->magicResistance(), 100); //probability of resistance in %
			if(rng.nextInt(0, 99) < prob)
				resistantUnitIds.insert(unit->unitId());
		}
	}

	auto filterResisted = [&, this](const battle::Unit * unit) -> bool
	{
		return resistantUnitIds.contains(unit->unitId());
	};

	auto filterUnit = [&](const battle::Unit * unit)
	{
		if(filterResisted(unit))
			resisted.push_back(unit);
		else
			affectedUnits.push_back(unit);
	};

	if (!target.empty())
	{
		const battle::Unit * targetedUnit = battle()->battleGetUnitByPos(target.front().hexValue, true);
		if (isReflected(targetedUnit, rng)) {
			reflect(sc, rng, targetedUnit);
			return;
			}
	}

	//prepare targets
	effectsToApply = effects->prepare(this, target, spellTarget);

	auto unitTargets = collectTargets();

	//process them
	for(const auto * unit : unitTargets)
		filterUnit(unit);

	//and update targets
	for(auto & p : effectsToApply)
	{
		vstd::erase_if(p.second, [&](const Destination & d)
		{
			if(!d.unitValue)
				return false;
			return vstd::contains(resisted, d.unitValue);
		});
	}

	for(const auto * unit : resisted)
		sc.resistedCres.insert(unit->unitId());

	resistantUnitIds.clear();
}

bool BattleSpellMechanics::isReflected(const battle::Unit * unit, vstd::RNG & rng)
{
	if (unit == nullptr)
		return false;
	const std::vector<int> directSpellRange = { 0 };
	bool isDirectSpell = !isMassive() && owner -> getLevelInfo(getRangeLevel()).range == directSpellRange;
	bool spellIsReflectable = isDirectSpell && (mode == Mode::HERO || mode == Mode::MAGIC_MIRROR) && isNegativeSpell();
	bool targetCanReflectSpell = spellIsReflectable && unit->getAllBonuses(Selector::type()(BonusType::MAGIC_MIRROR))->size()>0;
	return targetCanReflectSpell && rng.nextInt(0, 99) < unit->valOfBonuses(BonusType::MAGIC_MIRROR);
}

void BattleSpellMechanics::reflect(BattleSpellCast & sc, vstd::RNG & rng, const battle::Unit * unit)
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

	beforeCast(sc, rng, { Destination(reflectedTo) });
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
	affectedUnits.clear();
	//TODO: evaluate caster updates (mana usage etc.)
	//TODO: evaluate random values

	Target spellTarget = transformSpellTarget(target);

	effectsToApply = effects->prepare(this, target, spellTarget);

	auto unitTargets = collectTargets();

	auto selector = std::bind(&BattleSpellMechanics::counteringSelector, this, _1);

	std::copy(std::begin(unitTargets), std::end(unitTargets), std::back_inserter(affectedUnits));
	doRemoveEffects(server, affectedUnits, selector);

	for(auto & p : effectsToApply)
		p.first->apply(server, this, p.second);
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
	Target spellTarget;

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
	return targetCondition->isReceptive(this, target);
}

bool BattleSpellMechanics::isSmart() const
{
	return mode != Mode::MAGIC_MIRROR && BaseMechanics::isSmart();
}

bool BattleSpellMechanics::wouldResist(const battle::Unit * unit) const
{
	return resistantUnitIds.contains(unit->unitId());
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
