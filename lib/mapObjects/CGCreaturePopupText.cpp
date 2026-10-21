/*
 * CGCreaturePopupText.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CGCreature.h"

#include "../CCreatureHandler.h"
#include "../GameLibrary.h"
#include "../bonuses/BonusParameters.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../entities/faction/CTownHandler.h"

MetaString CGCreature::getMonsterLevelText() const
{
	const auto * creature = getCreature();
	const bool isRanged = creature->getBonusBearer()->hasBonusOfType(BonusType::SHOOTER);
	const std::string attackTypeKey = isRanged ? "vcmi.adventureMap.monsterRangedType" : "vcmi.adventureMap.monsterMeleeType";
	// The map object's callback owns this world's captured category rules.
	// Missing or unmapped rows retain the historical numeric presentation.
	const auto category = cb ? cb->getCreatureCategory(getCreatureID()) : std::nullopt;

	MetaString monsterLevel;
	if(category)
	{
		monsterLevel.appendTextID("vcmi.adventureMap.monsterCategory");
		monsterLevel.replaceTokenTextID("%CATEGORY", category->nameTextId);
	}
	else
	{
		monsterLevel.appendTextID("vcmi.adventureMap.monsterLevel");
		monsterLevel.replaceTokenNumber("%LEVEL", creature->getLevel());
	}
	monsterLevel.replaceTokenTextID("%TOWN", creature->getFactionID().toEntity(LIBRARY)->getNameTextID());
	monsterLevel.replaceTokenTextID("%ATTACK_TYPE", attackTypeKey);
	return monsterLevel;
}
