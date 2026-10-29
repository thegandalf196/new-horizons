/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewTurnProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/entities/faction/CTownHandler.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/NewHorizonsRecruitmentGrowth.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/callback/IGameInfoCallback.h"

SetAvailableCreatures NewTurnProcessor::generateTownGrowth(const CGTownInstance * t, EWeekType weekType, CreatureID creatureWeek, bool firstDay, int additionalGrowth)
{
	SetAvailableCreatures sac;
	PlayerColor player = t->tempOwner;

	sac.tid = t->id;
	sac.creatures = t->creatures;

	for (int k=0; k < t->getTown()->creatures.size(); k++)
	{
		if (t->creatures.at(k).second.empty())
			continue;

		uint32_t creaturesBefore = t->creatures.at(k).first;
		uint32_t creatureGrowth = 0;
		const CCreature *cre = t->creatures.at(k).second.back().toCreature();

		if (firstDay)
		{
			creatureGrowth = t->creatureBaseGrowth(cre->getId());
		}
		else
		{
			creatureGrowth = t->getGrowthInfo(k).unscaledGrowth();

			//Deity of fire week - upgrade both imps and upgrades
			if (weekType == EWeekType::DEITYOFFIRE && vstd::contains(t->creatures.at(k).second, creatureWeek))
				creatureGrowth += additionalGrowth;

			//bonus week, effect applies only to identical creatures
			if (weekType == EWeekType::BONUS_GROWTH && cre->getId() == creatureWeek)
				creatureGrowth += additionalGrowth;
		}

		// Neutral towns have halved creature growth
		if (!player.isValidPlayer())
			creatureGrowth /= 2;

		const bool doubles = weekType == EWeekType::DOUBLE_GROWTH
			&& vstd::contains(t->creatures.at(k).second, creatureWeek);
		const auto resultingCreatures = newHorizonsRecruitmentGrowth::stockAfterWeek(
			creaturesBefore, creatureGrowth,
			newHorizonsRecruitmentGrowth::percentage(gameHandler->gameInfo().getSettings()),
			doubles, weekType == EWeekType::PLAGUE);

		sac.creatures.at(k).first = resultingCreatures;
	}

	return sac;
}
