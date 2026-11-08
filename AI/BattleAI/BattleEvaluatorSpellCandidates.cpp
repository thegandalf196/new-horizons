/*
 * BattleEvaluatorSpellCandidates.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "BattleEvaluatorSpellCandidates.h"
#include "HeroOrderCandidateRules.h"
#include "../../lib/battle/NewHorizonsCommandResources.h"
#include "BattleAIDiagnosticAdmission.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/NewHorizonsMagic.h"

std::vector<BattleEvaluator::SpellOption> BattleEvaluator::collectCastableHeroSpellOptions(
	const std::shared_ptr<CPlayerBattleCallback> & battleView, const CGHeroInstance * hero,
	bool allowSpells, bool mandatoryOrder, bool legacyMetamagicFollowup,
	const DivineMandateStatus & divineMandateStatus,
	const battleAIDiagnostic::Admission * admission) const
{
	std::vector<SpellOption> possibleSpells;
	if(admission) admission->requireSupportedContext(mandatoryOrder, legacyMetamagicFollowup
		|| divineMandateStatus.pendingFollowup.has_value());

	for(auto const & s : LIBRARY->spellh->objects)
	{
		if(!allowSpells || mandatoryOrder)
			continue;
		if(admission && !admission->permits(s->getId())) continue;

		bool candidateMetamagicFollowup = legacyMetamagicFollowup;
		bool divineMandateFollowup = false;
		if(divineMandateStatus.active)
		{
			const auto selection = battleView->battleGetSpellActionAllowance(side, s->getId());
			if(!selection)
				continue;
			candidateMetamagicFollowup = battleView->battleCanUseMetamagicFollowup(side, s->getId());
			divineMandateFollowup = divineMandateStatus.pendingFollowup
				&& selection->grantId == divineMandateStatus.pendingFollowup->id;
		}

		const bool activatesGrand = HeroSpellAllowanceTransition::activatesGrand(candidateMetamagicFollowup,
			cb->getBattle(battleID)->battleMetamagicPendingCount(side),
			cb->getBattle(battleID)->battleMetamagicSequenceSpells(side).size(),
			cb->getBattle(battleID)->battleMetamagicUsesConsumed(side),
			newHorizonsMagic::metamagicRank(hero),
			newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND),
			cb->getBattle(battleID)->battleMetamagicGrandUsed(side));
		if(s->canBeCast(battleView.get(), spells::Mode::HERO, hero, activatesGrand))
			possibleSpells.push_back({s.get(), candidateMetamagicFollowup,
				divineMandateFollowup, activatesGrand});
	}
	return possibleSpells;
}

bool BattleEvaluator::canCastSpell()
{
	const auto battleView = cb->getBattle(battleID);
	const auto mandate = battleView->battleGetDivineMandateStatus(side);
	if(mandate.active)
	{
		const auto * hero = battleView->battleGetMyHero();
		if(hero && hero->hasSpellbook())
			for(const auto & spell : LIBRARY->spellh->objects)
			{
				const auto selection = battleView->battleGetSpellActionAllowance(side, spell->getId());
				const bool metamagicFollowup = selection
					&& battleView->battleCanUseMetamagicFollowup(side, spell->getId());
				const bool grand = HeroSpellAllowanceTransition::activatesGrand(metamagicFollowup,
					battleView->battleMetamagicPendingCount(side),
					battleView->battleMetamagicSequenceSpells(side).size(),
					battleView->battleMetamagicUsesConsumed(side),
					newHorizonsMagic::metamagicRank(hero),
					newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND),
					battleView->battleMetamagicGrandUsed(side));
				if(selection && (battleSpellCandidates::isBattleSpell(spell.get()) || newHorizonsMagic::isCounterspell(spell.get()))
					&& spell->canBeCast(battleView.get(), spells::Mode::HERO, hero, grand))
					return true;
			}
	}
	auto hero = battleView->battleGetMyHero();
	if(!hero)
		return false;

	if(!mandate.active && battleView->battleCanCastSpell(hero, spells::Mode::HERO) == ESpellCastProblem::OK)
		return true;
	if(!newHorizonsCommand::available(*battleView, side))
		return false;
	for(auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
		heroOrderCandidates::riposteCommand(), heroOrderCandidates::braceCommand()})
	{
		if(battleView->battleCanUseHeroCommand(side, command))
			return true;
	}
	for(auto command : {HeroCommand::FOCUS_FIRE, heroOrderCandidates::protectCommand(), heroOrderCandidates::flankCommand(), heroOrderCandidates::secondWindCommand()})
		if(battleView->battleCanBeginHeroCommand(side, command))
			return true;
	return false;
}


bool battleSpellCandidates::isBattleSpell(const CSpell * spell)
{
	if(!spell->isCombat() || spell->isCreatureAbility()) return false;
	return spell->isOffensive() || spell->hasEffects() || spell->hasBattleEffects();
}
