/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsMaintenanceFixture.h"
#include <algorithm>
class NewHorizonsMachineSpecialtyFixture : public NewHorizonsMaintenanceFixture
{
protected:
	bool keepMasterGunner = true;
	std::string preparingHero;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		for(const char * key : {"gunneryProfessorInitiativeFlat", "gunneryProfessorFollowUpBonusPoints", "heavyBatterySiegeTermBonusPercent"})
		{
			if(captureMode == 1) policy.Struct().erase(key);
			if(captureMode == 2) policy[key].Integer() = 0;
		}
		if(!keepMasterGunner)
		{
			auto & perks = rules["startingSkills"]["startingDevelopmentProfiles"][preparingHero]["startingPerks"].Vector();
			for(auto & row : perks)
				if(row["perk"].String() == "new-horizons:warMachines.masterGunner")
					row["perk"].String() = "new-horizons:warMachines.precisionBombardment";
		}
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(const char * hero, int mode = 0, bool masterGunner = true)
	{
		preparingHero = hero;
		keepMasterGunner = masterGunner;
		NewHorizonsMaintenanceFixture::prepare(hero, mode);
	}
};
