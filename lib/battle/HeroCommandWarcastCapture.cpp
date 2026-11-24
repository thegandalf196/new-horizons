/*
 * HeroCommandWarcastCapture.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsWarcast.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"

void CBattleInfoCallback::battleCaptureWarcastOrder(BattleSide side, HeroOrderState & result) const
{
	const auto * hero = battleGetFightingHero(side);
	std::vector<uint32_t> recipients;
	if(result.command == HeroCommand::VANGUARD)
	{
		if(result.revision)
			recipients = result.revision->recipientUnitIds;
	}
	else if(result.command == HeroCommand::SECOND_WIND)
		recipients.push_back(result.primaryTargetUnitId);
	else if(result.command != HeroCommand::PROTECT && result.command != HeroCommand::FLANK)
		for(const auto * unit : battleAliveUnits())
		{
			if(battleOwnOrderUnitRejection(side, unit) != heroCommands::TargetRejection::NONE)
				continue;
			if(result.command == HeroCommand::FOCUS_FIRE && !battleIsFocusFireRecipient(unit, side)
				&& !(heroCommands::hasCombinedArms(hero) && unit->isMeleeAttacker()
					&& !unit->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)))
				continue;
			recipients.push_back(unit->unitId());
		}
	std::sort(recipients.begin(), recipients.end());
	newHorizonsWarcast::capture(*this, side, result, recipients);
	if(result.warcast.recipients.empty())
		return;
	if(hero->hasActivePerk(newHorizonsWarcast::SKILL, newHorizonsWarcast::COMMANDERS_MEDITATION))
	{
		// At most once per round, across every Order the side issues this round.
		const auto existing = battleGetHeroOrderStates(side);
		result.warcast.meditation = std::none_of(existing.begin(), existing.end(), [&](const HeroOrderState & order)
		{
			return order.warcast.meditation && order.issuedRound == result.issuedRound;
		});
	}
	if(result.command != HeroCommand::VANGUARD && result.command != HeroCommand::SECOND_WIND
		&& hero->hasActivePerk(newHorizonsWarcast::SKILL, newHorizonsWarcast::UNBROKEN_COMMAND))
	{
		// Same carry ledger as Divine Discipline and Iron Will: the Order stays on these
		// recipients through their first completed Creature Activation next round.
		std::vector<uint32_t> carried;
		for(const auto & entry : result.warcast.recipients)
			carried.push_back(entry.unitId);
		std::vector<uint32_t> merged;
		std::set_union(result.divineDisciplineRecipientUnitIds.begin(), result.divineDisciplineRecipientUnitIds.end(),
			carried.begin(), carried.end(), std::back_inserter(merged));
		result.divineDisciplineRecipientUnitIds = std::move(merged);
		result.warcast.unbroken = true;
	}
}
