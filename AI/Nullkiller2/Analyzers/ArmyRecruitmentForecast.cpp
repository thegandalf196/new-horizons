/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "../StdInc.h"
#include "ArmyManager.h"
#include "../Engine/Nullkiller.h"
#include "../Helpers/RecruitmentRowPlan.h"
#include "../../../lib/mapObjects/TownRecruitmentOffers.h"
#include "../../../lib/mapObjects/CGDwelling.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../../lib/mapObjects/EstatesRecruitmentQuote.h"
#include <limits>

namespace NK2AI
{
namespace
{
class EstatesRecruitmentArmy : public CArmedInstance
{
public:
	EstatesRecruitmentArmy() : CArmedInstance(nullptr, BonusNodeType::UNKNOWN, true) {}
	void armyChanged() override {}
};
}

std::shared_ptr<CCreatureSet> ArmyManager::getArmyAvailableToBuyAsCCreatureSet(
	const CGDwelling * dwelling,
	TResources availableRes,
	const CGHeroInstance * buyer) const
{
	std::vector<creInfo> creaturesInDwellings;
	auto army = std::make_shared<EstatesRecruitmentArmy>();

	newHorizonsEstates::RecruitmentQuoteCursor prices(buyer);
	for(int i = dwelling->creatures.size() - 1; i >= 0; i--)
	{
		auto ci = infoFromDC(dwelling->creatures[i]);

		if(!ci.count || ci.creID == CreatureID::NONE)
			continue;

		ci.count = dwelling->getRecruitmentAmount(ci.creID, availableRes, ci.count, buyer, &prices.current());

		if(!ci.count)
			continue;

		SlotID dst = army->getFreeSlot();

		if(!dst.validSlot())
			break;

		army->setCreature(dst, ci.creID, ci.count);
		availableRes -= dwelling->getTotalRecruitmentCost(ci.creID, ci.count, buyer, &prices.current());
		if(newHorizonsEstates::usesConscriptionLedger(buyer, ci.creID))
			prices.accept(newHorizonsEstates::quoteRecruitment(dwelling->getRecruitmentCost(ci.creID), ci.count,
				prices.current(), true));
	}

	return army;
}

std::vector<creInfo> ArmyManager::getArmyAvailableToBuy(const CCreatureSet * hero, const CGDwelling * dwelling) const
{
	return getArmyAvailableToBuy(hero, dwelling, aiNk->getFreeResources());
}

std::vector<creInfo> ArmyManager::getArmyAvailableToBuy(
	const CCreatureSet * hero,
	const CGDwelling * dwelling,
	TResources availableRes,
	uint8_t turn,
	const CGHeroInstance * carrier) const
{
	std::vector<creInfo> creaturesInDwellings;
	if(!carrier)
		carrier = dynamic_cast<const CGHeroInstance *>(hero);
	RecruitmentProjection projection{newHorizonsHeroes::commandArmySlots(*hero), {}, std::nullopt};

	auto calendar = cpsic->getCalendar();
	bool countGrowth = (calendar.getDayOfWeek() + turn) > calendar.getDaysInWeek();

	const CGTownInstance * town = dwelling->ID == Obj::TOWN
		? dynamic_cast<const CGTownInstance *>(dwelling)
		: nullptr;

	for(int i = dwelling->creatures.size() - 1; i >= 0; i--)
	{
		const auto offer = townRecruitmentOffers::query(*dwelling, i, aiNk->playerID);
		if(!offer || offer->choices.empty()) continue;
		int available = static_cast<int>(std::min<uint64_t>(offer->available, std::numeric_limits<int>::max()));
		if(i < GameConstants::CREATURES_PER_TOWN && countGrowth)
		{
			const int64_t growth = town ? town->creatureGrowth(i) : dwelling->normalWeeklyGrowth(i);
			const int64_t headroom = std::numeric_limits<int>::max() - int64_t(available);
			available += static_cast<int>(std::min(growth, headroom));
		}
		const auto plan = planRecruitmentRow(*hero, carrier, *dwelling, *offer, available, availableRes, projection);
		if(!plan) continue;
		creaturesInDwellings.push_back(plan->creature);
		projection = plan->projection;
		availableRes -= plan->cost;
	}

	return creaturesInDwellings;
}

}
