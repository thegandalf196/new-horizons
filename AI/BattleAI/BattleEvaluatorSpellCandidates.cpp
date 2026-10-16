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
	const DivineMandateStatus & divineMandateStatus) const
{
	std::vector<SpellOption> possibleSpells;

	for(auto const & s : LIBRARY->spellh->objects)
	{
		if(!allowSpells || mandatoryOrder)
			continue;

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
