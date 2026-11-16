/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsEstatesFixture.h"
#include "../../lib/mapObjects/NewHorizonsEstatesPolicy.h"

namespace estatesTest
{
inline void prepareRevisedEstatesCatalogue(JsonNode & perks)
{
	if(newHorizonsEstates::usesRevisedPolicy(perks))
		return;
	perks["estatesPolicyVersion"].Integer() = 1;
	for(auto & row : perks["skills"][ESTATES]["perks"].Vector())
	{
		auto & identity = row["id"].String();
		if(identity == ESTATES + ".landSurveyor") identity = ESTATES + ".supervisor";
		else if(identity == ESTATES + ".prosperity") identity = ESTATES + ".mercenaryCompany";
		else if(identity == ESTATES + ".mercenaryCompany") identity = ESTATES + ".hoarder";
		else if(identity == ESTATES + ".banking") identity = ESTATES + ".decentralization";
	}
}

class RevisedFixture : public Fixture
{
protected:
	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER)
			.town({7, 7, 0}, FactionID::CASTLE, PLAYER).townGarrison({})
			.hero({30, 30, 0}, HeroTypeID(0), PLAYER).heroGarrison({{creature("core:pikeman"), 1}})
			.hero({30, 26, 0}, HeroTypeID(1), PLAYER).heroGarrison({{creature("core:pikeman"), 1}})
			.mine({15, 15, 0}, MapObjectSubID(0), PLAYER)
			.mine({19, 15, 0}, MapObjectSubID(0), PLAYER)
			.dwelling({24, 20, 0}, MapObjectSubID(56), PLAYER)
			.resource({24, 24, 0}, GameResID::GOLD, 10); // H3M Gold units are multiplied by 100.
		startWithMap(std::move(builder));
		first = findHeroAt({30, 30, 0});
		second = findHeroAt({30, 26, 0});
		town = expectAt<CGTownInstance>({7, 7, 0});
		mine = expectAt<CGMine>({15, 15, 0});
		otherMine = expectAt<CGMine>({19, 15, 0});
		dwelling = expectAt<CGDwelling>({24, 20, 0});
		ASSERT_TRUE(first && second && town && mine && otherMine && dwelling);
	}

	void mapLoaded(CMap * loaded) override
	{
		Fixture::mapLoaded(loaded);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		prepareRevisedEstatesCatalogue(perks);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
		JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
		auto replace = [](JsonNode & rows)
		{
			for(auto & row : rows.Vector())
				if(row["perk"].String() == ESTATES + ".prosperity"
					|| row["perk"].String() == ESTATES + ".landSurveyor")
					row["perk"].String() = ESTATES + ".supervisor";
		};
		for(auto & [hero, rows] : heroes["startingSkills"]["startingPerkProfiles"].Struct())
			replace(rows);
		for(auto & [hero, profile] : heroes["startingSkills"]["startingDevelopmentProfiles"].Struct())
			replace(profile["startingPerks"]);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(heroes));
	}
};

}
