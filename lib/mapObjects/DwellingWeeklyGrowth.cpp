/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CGDwelling.h"
#include "NewHorizonsRecruitmentGrowth.h"
#include "../GameLibrary.h"
#include "../CCreatureHandler.h"
#include "../IGameSettings.h"
#include "../callback/Calendar.h"
#include "../callback/IGameInfoCallback.h"
#include "../callback/IGameEventCallback.h"
#include "../callback/IGameRandomizer.h"
#include "../networkPacks/PacksForClient.h"

int64_t CGDwelling::normalWeeklyGrowth(size_t row) const
{
	if(!cb || row >= creatures.size() || creatures[row].second.empty())
		return 0;
	const auto creature = creatures[row].second.front();
	if(!creature.hasValue() || static_cast<size_t>(creature.getNum()) >= LIBRARY->creh->objects.size())
		return 0;
	const auto * definition = creature.toCreature();
	if(!definition)
		return 0;
	const int base = cb->getCreatureBaseGrowth(creature);
	// Preserve ordinary external growth's existing integer-percent semantics.
	const auto ordinary = static_cast<int64_t>(base) * (1 + definition->valOfBonuses(BonusType::CREATURE_GROWTH_PERCENT) / 100)
		+ definition->valOfBonuses(BonusType::CREATURE_GROWTH,
			BonusCustomSubtype::creatureLevel(definition->getLevel()));
	return newHorizonsRecruitmentGrowth::scale(ordinary,
		newHorizonsRecruitmentGrowth::percentage(cb->getSettings()));
}

void CGDwelling::newTurn(IGameEventCallback & gameEvents, IGameRandomizer & gameRandomizer) const
{
	if(cb->getCalendar().getDayOfWeek() != 1) //not first day of week
		return;

	//town growths and War Machines Factories are handled separately
	if(ID == Obj::TOWN  ||  ID == Obj::WAR_MACHINE_FACTORY)
		return;

	if(ID == Obj::REFUGEE_CAMP) //if it's a refugee camp, we need to pick an available creature
	{
		ChangeObjectVisitors cow;
		cow.mode = ChangeObjectVisitors::VISITOR_CLEAR;
		cow.object = id;
		gameEvents.sendAndApply(cow);

		gameEvents.setObjPropertyID(id, ObjProperty::AVAILABLE_CREATURE, gameRandomizer.rollCreature());
	}

	bool change = false;

	SetAvailableCreatures sac;
	sac.creatures = creatures;
	sac.tid = id;
	for (size_t i = 0; i < creatures.size(); i++)
	{
		if(!creatures[i].second.empty())
		{
			bool creaturesAccumulate = false;

			if (tempOwner.isValidPlayer())
				creaturesAccumulate = cb->getSettings().getBoolean(EGameSettings::DWELLINGS_ACCUMULATE_WHEN_OWNED);
			else
				creaturesAccumulate = cb->getSettings().getBoolean(EGameSettings::DWELLINGS_ACCUMULATE_WHEN_NEUTRAL);

			const auto amount = normalWeeklyGrowth(i);
			if (creaturesAccumulate && ID != Obj::REFUGEE_CAMP) //camp should not try to accumulate different kinds of creatures
				sac.creatures[i].first = newHorizonsRecruitmentGrowth::stockAfterWeek(
					sac.creatures[i].first, amount, 100, false, false);
			else
				sac.creatures[i].first = newHorizonsRecruitmentGrowth::stockAfterWeek(
					0, amount, 100, false, false);
			change = true;
		}
	}

	if(change)
		gameEvents.sendAndApply(sac);

	updateGuards(gameEvents);
}
