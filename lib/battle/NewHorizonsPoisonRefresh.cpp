/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSpecialtyProcs.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
namespace newHorizonsSpecialtyProcs
{
PoisonRefresh poisonRefreshPlan(const battle::Unit & unit, std::vector<Bonus> incoming)
{
	PoisonRefresh result;
	bool stronger = false;
	int cap = 0;
	int reduction = 0;
	const auto inspect = [&](const Bonus & bonus)
	{
		stronger |= isStrongWyvernPoison(&bonus);
		if(bonus.type == BonusType::POISON) cap = std::max(cap, bonus.val);
		if(bonus.type == BonusType::STACK_HEALTH) reduction = std::min(reduction, bonus.val);
	};
	const auto previous = unit.getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::POISON))));
	for(const auto & bonus : *previous)
		if(bonus && (bonus->type == BonusType::POISON || bonus->type == BonusType::STACK_HEALTH))
		{
			inspect(*bonus);
			result.remove.push_back(*bonus);
		}
	for(const auto & bonus : incoming) inspect(bonus);
	for(auto & bonus : incoming)
	{
		if(bonus.type == BonusType::POISON) bonus.val = cap;
		if(bonus.type == BonusType::STACK_HEALTH) bonus.val = std::max(reduction, -cap);
		for(const auto & previous : result.remove)
			if(previous.type == bonus.type) bonus.turnsRemain = std::max(bonus.turnsRemain, previous.turnsRemain);
		if(stronger && (bonus.type == BonusType::POISON || bonus.type == BonusType::STACK_HEALTH))
			bonus.stacking = "new-horizons:broghild:wyvernVenom";
		result.add.push_back(std::move(bonus));
	}
	return result;
}
}
