/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsChestDonation.h"
#include "Configuration.h"
#include "../callback/IGameEventCallback.h"
#include "../entities/hero/NewHorizonsCastleAdventureSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/Component.h"
#include "../networkPacks/PacksForClient.h"

namespace newHorizonsRewardable
{
int32_t declinedChestGold(const Rewardable::Configuration & configuration, ui32 chosen,
	const CGHeroInstance & hero, MapObjectID object)
{
	if(object != Obj::TREASURE_CHEST || chosen >= configuration.info.size()
		|| configuration.info[chosen].reward.heroExperience <= 0)
		return 0;
	int32_t result = 0;
	for(ui32 index = 0; index < configuration.info.size(); ++index)
	{
		const auto & visit = configuration.info[index];
		if(index == chosen || visit.visitType != configuration.info[chosen].visitType
			|| visit.reward.resources[EGameResID::GOLD] <= 0)
			continue;
		result = std::max(result, visit.reward.visitResources(&hero, Obj(object))[EGameResID::GOLD]);
	}
	return result;
}

int32_t chestDonation(const Rewardable::Configuration & configuration, ui32 chosen,
	const CGHeroInstance & hero, MapObjectID object)
{
	const auto declined = declinedChestGold(configuration, chosen, hero, object);
	return declined > 0 ? newHorizonsHeroes::almsOfTheFaithfulDonation(hero, declined) : 0;
}

void grantChestDonation(IGameEventCallback & events, const Rewardable::Configuration & configuration,
	ui32 chosen, const CGHeroInstance & hero, MapObjectID object)
{
	const auto donation = chestDonation(configuration, chosen, hero, object);
	if(donation <= 0)
		return;
	ResourceSet gold;
	gold[EGameResID::GOLD] = donation;
	events.giveResources(hero.getOwner(), gold);
	InfoWindow notice;
	notice.player = hero.getOwner();
	notice.text.appendTextID("new-horizons.economy.almsOfTheFaithful.donation");
	notice.text.replaceNumber(donation);
	notice.components.emplace_back(ComponentType::RESOURCE, GameResID(EGameResID::GOLD), donation);
	events.showInfoDialog(&notice);
}
}
