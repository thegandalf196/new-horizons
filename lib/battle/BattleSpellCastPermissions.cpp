/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/ObstacleCasterProxy.h"
#include <algorithm>
#include <typeinfo>
namespace
{
bool visibleObstacleTrigger(const CBattleInfoCallback & battle,
	const spells::ObstacleCasterProxy & caster, SpellID requestedSpell)
{
	if(typeid(caster) != typeid(spells::ObstacleCasterProxy))
		return false;
	const auto & source = caster.sourceObstacle();
	if(!source.triggersEffects() || source.getTrigger() != requestedSpell)
		return false;
	const auto visible = battle.battleGetAllObstacles();
	if(std::none_of(visible.begin(), visible.end(), [&source](const auto & obstacle)
		{ return obstacle.get() == &source; }))
		return false;
	if(source.casterSide == BattleSide::ATTACKER || source.casterSide == BattleSide::DEFENDER)
	{
		const auto * visibleHero = battle.battleGetFightingHero(source.casterSide);
		return caster.delegatedCaster() == visibleHero
			&& caster.getCasterOwner() == battle.sideToPlayer(source.casterSide)
			&& caster.getHeroCaster() == visibleHero;
	}
	// Match the existing neutral-trigger surrogate side, without lending it a Hero.
	return !caster.delegatedCaster() && !caster.getHeroCaster() && (caster.getCasterOwner() == battle.sideToPlayer(BattleSide::ATTACKER)
		|| caster.getCasterOwner() == battle.sideToPlayer(BattleSide::DEFENDER));
}
}

ESpellCastProblem CBattleInfoCallback::battleCanCastSpell(const spells::Caster * caster, spells::Mode mode) const
{
	return battleCanCastSpell(caster, mode, SpellID::NONE);
}

ESpellCastProblem CBattleInfoCallback::battleCanCastSpell(const spells::Caster * caster,
	spells::Mode mode, SpellID requestedSpell) const
{
	RETURN_IF_NOT_BATTLE(ESpellCastProblem::INVALID);
	if(caster == nullptr)
	{
		logGlobal->error("CBattleInfoCallback::battleCanCastSpell: no spellcaster.");
		return ESpellCastProblem::INVALID;
	}
	const PlayerColor player = caster->getCasterOwner();
	const auto side = playerToSide(player);
	if(side == BattleSide::NONE)
		return ESpellCastProblem::INVALID;
	const auto * obstacle = requestedSpell.hasValue() && mode == spells::Mode::PASSIVE
		? dynamic_cast<const spells::ObstacleCasterProxy *>(caster) : nullptr;
	if(obstacle && !visibleObstacleTrigger(*this, *obstacle, requestedSpell))
		return ESpellCastProblem::INVALID;
	if(!battleDoWeKnowAbout(side) && !obstacle)
	{
		logGlobal->warn("You can't check if enemy can cast given spell!");
		return ESpellCastProblem::INVALID;
	}

	if(battleTacticDist())
		return ESpellCastProblem::ONGOING_TACTIC_PHASE;

	switch(mode)
	{
	case spells::Mode::HERO:
	{
		const auto * hero = caster->getHeroCaster();
		const bool metamagicFollowup = battleCanUseMetamagicFollowup(side);
		if(getBattle()->getPreCombatOrderState(side).isUnresolved())
			return ESpellCastProblem::CASTS_PER_TURN_LIMIT;

		if(!hero)
			return ESpellCastProblem::NO_HERO_TO_CAST_SPELL;
		if(battleUsesHeroCommands())
		{
			const auto round = battleGetRound();
			const auto & allowances = getBattle()->getHeroActionAllowances(side);
			if(round < 0 || allowances.currentRound != round || !allowances.eligibleAllowance(
				HeroActionAllowanceState::ActionKind::SPELL, round))
				return ESpellCastProblem::CASTS_PER_TURN_LIMIT;
		}
		if(!hero->hasSpellbook())
			return ESpellCastProblem::NO_SPELLBOOK;
		if(!battleUsesHeroCommands() && !metamagicFollowup
			&& battleCastSpells(side) >= hero->valOfBonuses(BonusType::HERO_SPELL_CASTS_PER_COMBAT_TURN))
			return ESpellCastProblem::CASTS_PER_TURN_LIMIT;
	}
		break;
	default:
		break;
	}

	//Orb of Inhibition blocks active spellcasting (hero and creature active abilities),
	//but not passive/triggered casts such as SPELL_BEFORE_ATTACK / SPELL_AFTER_ATTACK.
	//Level-0 creature abilities are excluded from this block in BattleSpellMechanics::canBeCast (spell level known there)
	if(mode == spells::Mode::HERO || mode == spells::Mode::CREATURE_ACTIVE)
	{
		const IBonusBearer * casterBonuses = caster->getHeroCaster();
		if(!casterBonuses)
			casterBonuses = battleGetUnitByID(caster->getCasterUnitId());
		if(casterBonuses && casterBonuses->hasBonusOfType(BonusType::BLOCK_ALL_MAGIC))
			return ESpellCastProblem::MAGIC_IS_BLOCKED;
	}

	return ESpellCastProblem::OK;
}
