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
#include "DoubleCastFollowUp.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/NewHorizonsMagic.h"

std::vector<BattleEvaluator::SpellOption> BattleEvaluator::collectCastableHeroSpellOptions(
	const std::shared_ptr<CPlayerBattleCallback> & battleView, const CGHeroInstance * hero,
	bool allowSpells, bool mandatoryOrder,
	const DivineMandateStatus & divineMandateStatus,
	const battleAIDiagnostic::Admission * admission) const
{
	std::vector<SpellOption> possibleSpells;
	if(admission) admission->requireSupportedContext(mandatoryOrder, divineMandateStatus.pendingFollowup.has_value());
	// An open Double Cast offer is scored like any hero spell; declining stays free.
	const doubleCastAI::FollowUpFilter followUp(*battleView, side, hero);

	for(auto const & s : LIBRARY->spellh->objects)
	{
		if(!allowSpells || mandatoryOrder)
			continue;
		if(admission && !admission->permits(s->getId())) continue;

		bool divineMandateFollowup = false;
		if(divineMandateStatus.active)
		{
			const auto selection = battleView->battleGetSpellActionAllowance(side, s->getId());
			if(!selection) continue;
			divineMandateFollowup = divineMandateStatus.pendingFollowup
				&& selection->grantId == divineMandateStatus.pendingFollowup->id;
		}
		if(followUp.refuses(s->getId())) continue;
		if(s->canBeCast(battleView.get(), spells::Mode::HERO, hero))
			possibleSpells.push_back({s.get(), divineMandateFollowup});
	}
	if(followUp.offerOpen())
	{
		std::vector<const CSpell *> spells;
		for(const auto & option : possibleSpells) spells.push_back(option.spell);
		const auto dropped = followUp.overCap(spells);
		std::erase_if(possibleSpells, [&](const SpellOption & option)
		{ return std::ranges::find(dropped, option.spell->getId()) != dropped.end(); });
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
				if(selection && battleSpellCandidates::isBattleSpell(spell.get())
					&& spell->canBeCast(battleView.get(), spells::Mode::HERO, hero))
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
