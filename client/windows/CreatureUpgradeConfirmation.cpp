/*
 * CreatureUpgradeConfirmation.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CreatureUpgradeConfirmation.h"

#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../widgets/CComponent.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/MetaString.h"

namespace creatureUpgradeConfirmation
{
void show(const TResources & totalCost, CreatureID target, unsigned int count,
	const std::function<void()> & onUpgrade)
{
	std::vector<std::shared_ptr<CComponent>> resComps;
	for(TResources::nziterator i(totalCost); i.valid(); i++)
		resComps.push_back(std::make_shared<CComponent>(ComponentType::RESOURCE, i->resType, i->resVal));

	if(GAME->interface()->cb->getResourceAmount().canAfford(totalCost))
	{
		std::string message = LIBRARY->generaltexth->allTexts[207];
		if(totalCost.empty())
		{
			const auto * creature = LIBRARY->creh->getById(target);
			auto freeOffer = MetaString::createFromTextID("vcmi.creatureWindow.upgrade.free");
			freeOffer.replaceRawString(count == 1 ? creature->getNameSingularTranslated() : creature->getNamePluralTranslated());
			message = freeOffer.toString(&GAME->translator());
			resComps.push_back(std::make_shared<CComponent>(ComponentType::RESOURCE, static_cast<GameResID>(GameResID::GOLD), 0));
			resComps.back()->newLine = true;
			resComps.push_back(std::make_shared<CComponent>(ComponentType::CREATURE, target, static_cast<int32_t>(count)));
		}
		GAME->interface()->showYesNoDialog(message, onUpgrade, nullptr, resComps);
	}
	else
		GAME->interface()->showInfoDialog(LIBRARY->generaltexth->allTexts[314], resComps);
}
}
