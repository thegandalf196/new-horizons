/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActivationEffects.h"
#include "BattleMaintenanceRecovery.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/HydrasVitalityIdentity.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
namespace battleActivationEffects
{
namespace
{
void applyExistingRecovery(CGameHandler * gameHandler, const CBattleInfoCallback & battle, const CStack * creatureStack,
	const CGHeroInstance * hero, std::shared_ptr<battle::CUnitState> & state)
{
	if(state)
	{
		const int64_t pendingPhysicalDamage = state->veteranPhysicalDamageSinceActivation;
		const int64_t healing = newHorizonsCombatSkills::applyVeteran(state.get(), hero);
		if(pendingPhysicalDamage > 0 || healing > 0)
		{
			UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
			update.data = state->save();
			update.healthDelta = healing;
			BattleUnitsChanged changed;
			changed.battleID = battle.getBattle()->getBattleID();
			changed.changedStacks.push_back(std::move(update));
			gameHandler->sendAndApply(changed);

			if(healing > 0)
			{
				BattleLogMessage message;
				message.battleID = battle.getBattle()->getBattleID();
				MetaString line;
				line.appendRawString("Veteran restores %s ");
				creatureStack->addNameReplacement(line, creatureStack->getCount());
				line.appendNumber(healing);
				line.appendRawString(" Health.");
				message.lines.push_back(std::move(line));
				gameHandler->sendAndApply(message);
			}
		}
	}
	if(creatureStack->alive() && !creatureStack->isTimeStopped() && state
		&& state->regenerationPendingMicroHealth > 0)
	{
		int64_t healing = state->consumeRegenerationMarks();
		if(healing > 0)
			healing = state->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints;

		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = healing;
		BattleUnitsChanged changed;
		changed.battleID = battle.getBattle()->getBattleID();
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);

		if(healing > 0)
		{
			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			line.appendRawString("Regeneration restores %s ");
			creatureStack->addNameReplacement(line, creatureStack->getCount());
			line.appendNumber(healing);
			line.appendRawString(" Health.");
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
	}
	const auto hydrasVitalityMarker = newHorizonsMagic::hydrasVitalitySource(battle.getBattle()->getMagicRules())
		.And(Selector::type()(BonusType::HP_REGENERATION));
	if(creatureStack->alive() && !creatureStack->isTimeStopped() && state
		&& state->health.isCapacityHealthTracking()
		&& creatureStack->hasBonus(hydrasVitalityMarker))
	{
		const int64_t healing = state->consumeCapacityRegeneration();
		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = healing;
		BattleUnitsChanged changed;
		changed.battleID = battle.getBattle()->getBattleID();
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);

		if(healing > 0)
		{
			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			line.appendRawString("Hydra's Vitality restores %s ");
			creatureStack->addNameReplacement(line, creatureStack->getCount());
			line.appendNumber(healing);
			line.appendRawString(" Health.");
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
	}
}
void applyPhysicalPoison(CGameHandler * gameHandler, const CBattleInfoCallback & battle, const CStack * creatureStack,
	std::shared_ptr<battle::CUnitState> & state)
{
	const int64_t poisonTick = newHorizonsBulwark::physicalPoisonTickDamage(state.get());
	if(creatureStack->alive() && state && poisonTick > 0)
	{
		const int64_t guardianSpiritBefore = state->guardianSpiritHitPoints;
		const auto poisonSourceStackId = state->physicalPoisonSourceStackId;
		newHorizonsBulwark::advancePhysicalPoison(state.get());
		BattleStackAttacked hit;
		hit.attackerID = poisonSourceStackId >= 0
			? static_cast<ui32>(poisonSourceStackId) : creatureStack->unitId();
		hit.stackAttacked = creatureStack->unitId();
		hit.damageAmount = poisonTick;
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), state,
			false, false, battle::DamageProvenance::PHYSICAL_CREATURE);
		const int64_t guardianSpiritAbsorbed = std::max<int64_t>(
			0, guardianSpiritBefore - state->guardianSpiritHitPoints);
		const int64_t guardianSpiritOverflow = guardianSpiritAbsorbed > 0
			? std::max<int64_t>(0, poisonTick - guardianSpiritAbsorbed) : 0;
		if(!state->alive())
		{
			newHorizonsBulwark::clearPhysicalPoison(state.get());
			hit.newState.data = state->save();
		}
		StacksInjured injury;
		injury.battleID = battle.getBattle()->getBattleID();
		injury.stacks.push_back(hit);
		gameHandler->sendAndApply(injury);

		if(hit.damageAmount > 0 || guardianSpiritAbsorbed > 0)
		{
			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			if(guardianSpiritAbsorbed > 0)
			{
				line.appendRawString("Physical Poison hits %s; Guardian Spirit absorbs ");
				creatureStack->addNameReplacement(line, creatureStack->getCount());
				line.appendNumber(guardianSpiritAbsorbed);
				line.appendRawString(" physical damage");
				if(guardianSpiritOverflow > 0)
				{
					line.appendRawString(" and ");
					line.appendNumber(guardianSpiritOverflow);
					line.appendRawString(" damage passes through.");
				}
				else
					line.appendRawString("; no damage passes through.");
			}
			else
			{
				line.appendRawString("%s suffers ");
				creatureStack->addNameReplacement(line, creatureStack->getCount());
				line.appendNumber(hit.damageAmount);
				line.appendRawString(" physical Poison damage.");
			}
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
		state = creatureStack->acquireState();
	}
}
void applyBulwarkCleanup(CGameHandler * gameHandler, const CBattleInfoCallback & battle, const CStack * creatureStack,
	const CGHeroInstance * hero, std::shared_ptr<battle::CUnitState> & state)
{
	if(state && state->bulwarkMireGripApplied)
	{
		const int skillId = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
		const auto sourceId = BonusSourceID(SecondarySkill(skillId));
		const auto penalties = creatureStack->getAllBonuses(Selector::source(BonusSource::OTHER, sourceId));
		if(penalties && !penalties->empty())
		{
			std::vector<Bonus> toRemove;
			toRemove.reserve(penalties->size());
			for(const auto & bonus : *penalties)
				toRemove.push_back(*bonus);
			SetStackEffect remove;
			remove.battleID = battle.getBattle()->getBattleID();
			remove.toRemove.emplace_back(creatureStack->unitId(), std::move(toRemove));
			gameHandler->sendAndApply(remove);
		}
		state->bulwarkMireGripApplied = false;
		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		BattleUnitsChanged changed;
		changed.battleID = battle.getBattle()->getBattleID();
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}
	if(creatureStack->alive() && state && state->bulwarkDefendPhysicalDamage > 0
		&& newHorizonsBulwark::hasSwampRenewal(hero))
	{
		const int64_t healing = newHorizonsBulwark::applySwampRenewal(state.get(), hero);

		UnitChanges update(state->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = healing;
		BattleUnitsChanged changed;
		changed.battleID = battle.getBattle()->getBattleID();
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);

		if(healing > 0)
		{
			BattleLogMessage message;
			message.battleID = battle.getBattle()->getBattleID();
			MetaString line;
			line.appendRawString("Swamp Renewal restores %s ");
			creatureStack->addNameReplacement(line, creatureStack->getCount());
			line.appendNumber(healing);
			line.appendRawString(" Health.");
			message.lines.push_back(std::move(line));
			gameHandler->sendAndApply(message);
		}
	}
}
}
void apply(CGameHandler * gameHandler, const CBattleInfoCallback & battle, const battle::Unit * stack, BattleUnitTurnReason reason)
{
	const auto * creatureStack = dynamic_cast<const CStack *>(stack);
	if(!creatureStack)
		return;
	const auto * hero = battle.battleGetOwnerHero(creatureStack);
	auto state = creatureStack->acquireState();
	applyExistingRecovery(gameHandler, battle, creatureStack, hero, state);
	if(battleMaintenanceRecovery::onActivation(*gameHandler, battle, *creatureStack, reason))
		state = creatureStack->acquireState();
	applyPhysicalPoison(gameHandler, battle, creatureStack, state);
	applyBulwarkCleanup(gameHandler, battle, creatureStack, hero, state);
}
}
