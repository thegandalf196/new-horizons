/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "army/CStackInstance.h"
#include "../gameState/UpgradeInfo.h"
#include "../entities/hero/NewHorizonsGhostSpecialty.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusList.h"
void CGHeroInstance::fillUpgradeInfo(UpgradeInfo & info, const CStackInstance & stack) const
{
	if(stack.getId() == CreatureID(CreatureID::decode("core:wraith")) && newHorizonsHeroes::hasGhostUpgrade(*this))
	{
		const CreatureID ghost(CreatureID::decode("new-horizons:ghost"));
		if(ghost.hasValue())
			info.addUpgrade(ghost, stack.getType());
	}
	TConstBonusListPtr lista = stack.getBonusesOfType(BonusType::SPECIAL_UPGRADE, BonusSubtypeID(stack.getId()));
	for(const auto & it : *lista)
	{
		if (it->parameters)
		{
			auto nid = it->parameters->toCreature();
			if (nid != stack.getId()) //in very specific case the upgrade is available by default (?)
				info.addUpgrade(nid, stack.getType());
		}
	}
}
