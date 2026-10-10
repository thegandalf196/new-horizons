/*
 * NewHorizonsRecruitmentTraining.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsRecruitmentTraining.h"
#include "NewHorizonsCreatureCategoryRules.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../callback/IGameInfoCallback.h"
#include "../../battle/BattleInfo.h"
#include "../../CStack.h"
#include "../../gameState/CGameState.h"
#include "../../mapObjects/army/CStackInstance.h"
#include "NewHorizonsMusterRules.h"
#include "../../GameLibrary.h"
#include "../../CCreatureHandler.h"

namespace newHorizonsTraining
{
bool containsTrainingBonus(const JsonNode & node)
{
	if(node.isStruct())
	{
		const auto & identity = node["stacking"];
		if(identity.isString() && (identity.String() == "new-horizons:fieldInstructor"
			|| identity.String() == "new-horizons:drillSergeant"
			|| identity.String() == "new-horizons:reinforcementDrill"))
			return true;
		for(const auto & [key, child] : node.Struct())
			if(containsTrainingBonus(child))
				return true;
	}
	else if(node.isVector())
		for(const auto & child : node.Vector())
			if(containsTrainingBonus(child))
				return true;
	return false;
}

bool isTrainingBonus(const Bonus * bonus)
{
	if(!bonus || bonus->source != BonusSource::SECONDARY_SKILL || bonus->sid.toString() != SKILL
		|| bonus->valType != BonusValueType::ADDITIVE_VALUE)
		return false;
	if(bonus->stacking == "new-horizons:fieldInstructor")
		return bonus->type == BonusType::PRIMARY_SKILL && bonus->subtype == BonusSubtypeID(PrimarySkill::ATTACK)
			&& bonus->val == 1 && bonus->duration == BonusDuration::PERMANENT;
	if(bonus->stacking == "new-horizons:drillSergeant")
		return bonus->type == BonusType::MORALE && bonus->val == 1 && bonus->duration == BonusDuration::ONE_BATTLE;
	return bonus->stacking == "new-horizons:reinforcementDrill"
		&& bonus->type == BonusType::STACKS_INITIATIVE_FLAT && bonus->val == 2
		&& bonus->duration == BonusDuration::N_TURNS && bonus->turnsRemain >= 0 && bonus->turnsRemain <= 1;
}

Batch captureEntry(const BattleInfo & battle, int32_t day, int32_t week)
{
	if(day < 0 || week < 0)
		throw std::runtime_error("Invalid training combat calendar");
	Batch batch;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * army = battle.battleGetArmyObject(side);
		const auto * hero = battle.getSideHero(side);
		bool reinforcementAvailable = hero && hero->getTrainingDrillLastWeek() < week
			&& hero->hasActivePerk(SKILL, REINFORCEMENT_DRILL);
		if(!army)
			continue;
		// Strategic slot order, never initiative/unit-ID order.
		for(const auto & [slot, stack] : army->stacks)
		{
			if(!stack || stack->getCount() <= 0)
				continue;
			const auto & previous = stack->getTrainingReceipt();
			const bool reinforcement = reinforcementAvailable && previous.reinforcementPending
				&& previous.residentRecruiter == hero->id;
			const auto next = afterBattleEntry(previous, day, reinforcement);
			if(next != previous)
				batch.stacks.push_back({army->id, slot, stack->getCreatureID(),
					stack->getCount(), 0, previous, next});
			if(reinforcement)
			{
				batch.weeks.push_back({hero->id, hero->getTrainingDrillLastWeek(), week});
				reinforcementAvailable = false;
			}
		}
	}
	batch.validate();
	return batch;
}

void addEntryBonuses(BattleInfo & battle, int32_t day, int32_t week)
{
	(void)week;
	const auto & batch = battle.trainingEntrySnapshot;
	for(const auto * original : battle.getStacksIf([](const CStack * stack) { return stack->base && stack->unitSlot().validSlot(); }))
	{
		auto * unit = battle.getStack(original->unitId(), false);
		const auto prior = std::find_if(batch.stacks.begin(), batch.stacks.end(), [unit](const auto & change)
		{
			return change.army == unit->base->getArmy()->id && change.slot == unit->unitSlot();
		});
		const auto & receipt = prior == batch.stacks.end()
			? unit->base->getTrainingReceipt() : prior->previous;
		unit->removeBonuses(CSelector([](const Bonus * bonus)
		{
			return isTrainingBonus(bonus) && bonus->stacking != "new-horizons:fieldInstructor";
		}));
		const auto add = [unit](BonusType type, int value, const char * stacking, bool oneRound)
		{
			auto bonus = std::make_shared<Bonus>(oneRound ? BonusDuration::N_TURNS : BonusDuration::ONE_BATTLE,
				type, BonusSource::SECONDARY_SKILL, value,
				BonusSourceID(SecondarySkill(SecondarySkill::decode(SKILL))));
			bonus->turnsRemain = oneRound ? 1 : 0;
			bonus->stacking = stacking;
			unit->addNewBonus(bonus);
		};
		if(receipt.drillDeadline >= day)
			add(BonusType::MORALE, 1, "new-horizons:drillSergeant", false);
		const auto found = std::find_if(batch.stacks.begin(), batch.stacks.end(), [unit](const auto & change)
		{
			return change.army == unit->base->getArmy()->id && change.slot == unit->unitSlot()
				&& change.previous.reinforcementPending && !change.next.reinforcementPending;
		});
		if(found != batch.stacks.end())
			add(BonusType::STACKS_INITIATIVE_FLAT, 2, "new-horizons:reinforcementDrill", true);
	}
}

Batch captureCompletion(const BattleInfo & battle, const std::set<ObjectInstanceID> & retainedHeroes)
{
	Batch batch;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = battle.getSideHero(side);
		if(!hero || !retainedHeroes.count(hero->id))
			continue;
		for(const auto * unit : battle.getStacksIf([side](const CStack * stack)
			{ return stack->unitSide() == side && stack->base && stack->unitSlot().validSlot(); }))
		{
			const auto * stack = hero->getStackPtr(unit->unitSlot());
			// Result casualties/restoration already update the original army. New
			// summons never have this original strategic base identity.
			if(!stack || stack != unit->base || stack->getCount() <= 0)
				continue;
			const auto next = afterCompletedBattle(stack->getTrainingReceipt(), hero->id);
			if(next != stack->getTrainingReceipt())
				batch.stacks.push_back({hero->id, unit->unitSlot(), stack->getCreatureID(),
					stack->getCount(), 0, stack->getTrainingReceipt(), next});
		}
	}
	batch.validate();
	return batch;
}

void validateBatch(const CGameState & state, const Batch & batch)
{
	batch.validate();
	for(const auto & change : batch.stacks)
	{
		if(!change.creature.hasValue() || static_cast<size_t>(change.creature.getNum()) >= LIBRARY->creh->objects.size()
			|| !LIBRARY->creh->objects[change.creature.getNum()])
			throw std::runtime_error("Training transaction has an invalid installed creature");
		const auto * army = dynamic_cast<const CArmedInstance *>(state.getObj(change.army));
		const auto * stack = army ? army->getStackPtr(change.slot) : nullptr;
		if(!army || (stack ? stack->getCreatureID() != change.creature
			|| stack->getCount() != change.expectedCount || stack->getTrainingReceipt() != change.previous
			: change.expectedCount != 0 || !change.previous.empty()))
			throw std::runtime_error("Stale training stack transaction");
		if(change.next.residentRecruiter.hasValue() && change.next.residentRecruiter != change.army)
			throw std::runtime_error("Training transaction has a foreign resident recruiter");
	}
	for(const auto & change : batch.weeks)
	{
		const auto * hero = state.getHero(change.hero);
		if(!hero || hero->getTrainingDrillLastWeek() != change.previous)
			throw std::runtime_error("Stale Reinforcement Drill weekly transaction");
	}
}

void applyValidatedBatch(CGameState & state, const Batch & batch)
{
	// Call only after complete context and expected-state validation. Battle
	// batches never change counts or create units.
	for(const auto & change : batch.stacks)
	{
		auto * army = state.getArmyInstance(change.army);
		auto * stack = army->getStackPtr(change.slot);
		if(change.recruitedCount)
		{
			if(stack)
				stack->setCount(change.expectedCount + change.recruitedCount);
			else
			{
				army->putStack(change.slot, std::make_unique<CStackInstance>(&state, change.creature, change.recruitedCount));
				stack = army->getStackPtr(change.slot);
			}
		}
		stack->setTrainingReceipt(change.next);
		army->armyChanged();
	}
	for(const auto & change : batch.weeks)
		state.getHero(change.hero)->setTrainingDrillLastWeek(change.next);
}

Receipt afterRecruitment(const CGHeroInstance & hero, CreatureID creature,
	int32_t day, const Receipt & previous)
{
	previous.validate();
	if(day < 0 || day > std::numeric_limits<int32_t>::max() - 6)
		throw std::runtime_error("Invalid recruitment day");
	Receipt result = previous;
	const auto category = newHorizonsCreatures::creatureCategoryView(hero.cb->getCreatureCategoryRules(), creature);
	if(!category)
		return result;
	const bool coreOrElite = category->category != newHorizonsCreatures::CreatureCategory::CHAMPION;
	if(coreOrElite && hero.hasActivePerk(SKILL, DRILL_SERGEANT))
		result.drillDeadline = std::max(result.drillDeadline, day + 6);
	if(coreOrElite && hero.hasActivePerk(SKILL, FIELD_INSTRUCTOR))
	{
		result.residentRecruiter = hero.id;
		result.fieldPending = true;
	}
	if(hero.hasActivePerk(SKILL, REINFORCEMENT_DRILL))
	{
		result.residentRecruiter = hero.id;
		result.reinforcementPending = true;
	}
	result.validate();
	return result;
}
Receipt afterBattleEntry(const Receipt & previous, int32_t day, bool reinforcement)
{
	previous.validate();
	if(day < 0)
		throw std::runtime_error("Invalid battle entry day");
	Receipt result = previous;
	// Immunity/caps never defer consumption at actual combat entry.
	result.drillDeadline = -1;
	if(reinforcement)
		result.reinforcementPending = false;
	if(!result.fieldPending && !result.fieldTrained && !result.reinforcementPending)
		result.residentRecruiter = ObjectInstanceID::NONE;
	result.validate();
	return result;
}
Receipt afterCompletedBattle(const Receipt & previous, ObjectInstanceID recruiter)
{
	previous.validate();
	Receipt result = previous;
	if(result.residentRecruiter == recruiter && result.fieldPending)
	{
		result.fieldPending = false;
		result.fieldTrained = true;
	}
	result.validate();
	return result;
}
}
