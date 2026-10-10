/*
 * NewHorizonsRecruitersContacts.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsRecruitersContacts.h"
#include "CGDwelling.h"
#include "CGHeroInstance.h"
#include "../spells/NewHorizonsMagic.h"
#include "../GameLibrary.h"
#include "../CCreatureHandler.h"
#include <limits>

namespace newHorizonsRecruitment
{
std::optional<ContactsAward> recruitersContactsAward(const CGHeroInstance & hero,
	const CGDwelling & dwelling, int32_t absoluteWeek)
{
	if(absoluteWeek < 0 || hero.getNewHorizonsRecruitersContactsLastWeek() < -1
		|| hero.getNewHorizonsRecruitersContactsLastWeek() >= absoluteWeek
		|| !hero.id.hasValue() || !dwelling.id.hasValue()
		|| !hero.getOwner().isValidPlayer() || dwelling.getOwner() != hero.getOwner()
		|| (dwelling.ID != Obj::CREATURE_GENERATOR1 && dwelling.ID != Obj::CREATURE_GENERATOR4)
		|| !newHorizonsMagic::rulesActive(hero.getMagicRules())
		|| !hero.hasActivePerk("new-horizons:recruitment", "new-horizons:recruitment.recruiterSContacts"))
		return std::nullopt;
	for(size_t row = 0; row < dwelling.creatures.size(); ++row)
	{
		if(dwelling.creatures[row].first != 0 || dwelling.creatures[row].second.empty())
			continue;
		const bool validAlternatives = std::all_of(dwelling.creatures[row].second.begin(), dwelling.creatures[row].second.end(),
			[](CreatureID id)
			{
				return id.hasValue() && static_cast<size_t>(id.getNum()) < LIBRARY->creh->objects.size()
					&& LIBRARY->creh->objects.at(id.getNum());
			});
		if(!validAlternatives)
			continue;
		const auto growth = dwelling.normalWeeklyGrowth(row);
		if(growth > 0 && growth <= std::numeric_limits<TQuantity>::max()
			&& row <= std::numeric_limits<uint32_t>::max())
			return ContactsAward{static_cast<uint32_t>(row), static_cast<uint32_t>(growth)};
	}
	return std::nullopt;
}
}
