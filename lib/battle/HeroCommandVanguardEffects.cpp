/*
 * HeroCommandVanguardEffects.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroOrderEffects.h"
#include "HeroCommandRoyalStandard.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"

namespace heroCommands
{
bool hasVanguardRoyalStandardEntitlement(const battle::Unit * unit, BattleSide currentOwnerSide)
{
	if(!unit || (currentOwnerSide != BattleSide::ATTACKER && currentOwnerSide != BattleSide::DEFENDER))
		return false;
	const int capturedSide = static_cast<int>(currentOwnerSide) + 1;
	return unit->hasBonus(CSelector([capturedSide](const Bonus * bonus)
	{
		return isVanguardBonus(bonus) && bonus->type == BonusType::NONE
			&& bonus->val == capturedSide && bonus->turnsRemain > 0
			&& bonus->valType == BonusValueType::ADDITIVE_VALUE;
	}));
}


void appendVanguardGrants(IssuedOrderGrants & result, const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order)
{
	if(order.command == HeroCommand::VANGUARD && order.revision)
	{
		IssuedOrderGrants::Batch removals;
		IssuedOrderGrants::Batch additions;
		for(const auto * unit : battle.battleGetAllUnits(false))
		{
			if(!unit || unit->isTimeStopped() || battle.battleGetOwner(unit) != battle.sideToPlayer(side))
				continue;
			std::vector<Bonus> obsolete;
			for(const auto & bonus : *unit->getBonuses(CSelector(isVanguardBonus)))
				obsolete.push_back(*bonus);
			if(!obsolete.empty())
				removals.emplace_back(unit->unitId(), std::move(obsolete));
			if(!order.revision->contains(unit->unitId()))
				continue;
			const bool crowned = order.revision->crown && order.crownAndAltarAppliesTo(unit->unitId());
			const int statBonus = crowned ? order.revision->crown->vanguardStatBonus : order.revision->vanguardStatBonus;
			const int initiative = crowned ? order.revision->crown->vanguardInitiativePercent : order.revision->vanguardInitiativePercent;
			std::vector<Bonus> bonuses;
			const auto add = [&](BonusType type, BonusSubtypeID subtype, int value, BonusValueType valueType)
			{
				Bonus bonus;
				bonus.source = BonusSource::HERO_COMMAND;
				bonus.duration = BonusDuration::N_TURNS;
				bonus.turnsRemain = order.revision->expiresAfterRound - order.issuedRound + 1;
				bonus.type = type;
				bonus.subtype = subtype;
				bonus.val = value;
				bonus.valType = valueType;
				bonus.stacking = "vanguard";
				if(type != BonusType::NONE)
					bonus.description.appendRawString("Vanguard");
				bonuses.push_back(bonus);
			};
			add(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK),
				statBonus, BonusValueType::ADDITIVE_VALUE);
			add(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE),
				statBonus, BonusValueType::ADDITIVE_VALUE);
			add(BonusType::MORALE, BonusSubtypeID(), statBonus, BonusValueType::ADDITIVE_VALUE);
			add(BonusType::STACKS_INITIATIVE, BonusSubtypeID(),
				initiative, BonusValueType::ADDITIVE_VALUE);
			if(std::binary_search(order.royalStandardRecipientUnitIds.begin(),
				order.royalStandardRecipientUnitIds.end(), unit->unitId()))
				add(BonusType::NONE, BonusSubtypeID(), static_cast<int>(side) + 1, BonusValueType::ADDITIVE_VALUE);
			additions.emplace_back(unit->unitId(), std::move(bonuses));
		}
		if(!removals.empty())
			result.removals.push_back(std::move(removals));
		if(!additions.empty())
			result.batches.push_back(std::move(additions));
	}
}
}
