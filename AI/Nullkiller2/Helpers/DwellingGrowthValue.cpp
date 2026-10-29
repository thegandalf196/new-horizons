/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "../StdInc.h"
#include "DwellingGrowthValue.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/mapObjects/CGDwelling.h"

namespace NK2AI
{
uint64_t getDwellingArmyGrowth(CCallback * cb, const CGObjectInstance * target, PlayerColor myColor)
{
	auto dwelling = dynamic_cast<const CGDwelling *>(target);
	uint64_t score = 0;

	if(dwelling->getOwner() == myColor)
		return 0;

	for(size_t row = 0; row < dwelling->creatures.size(); ++row)
	{
		const auto & creLevel = dwelling->creatures[row];
		if(creLevel.second.size())
		{
			auto creature = creLevel.second.back().toCreature();
			score += creature->getAIValue() * dwelling->normalWeeklyGrowth(row);

			// Increase priority towards the end of the week if units are lost afterwards
			if(!cb->getSettings().getBoolean(EGameSettings::DWELLINGS_ACCUMULATE_WHEN_OWNED))
			{
				const auto dayOfWeek = cb->getCalendar().getDayOfWeek();
				score *= dayOfWeek;
			}
		}
	}

	return score;
}

}
