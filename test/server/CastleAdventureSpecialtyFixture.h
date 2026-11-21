/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "RevisedEstatesFixture.h"
#include "../../lib/entities/hero/NewHorizonsCastleAdventureSpecialties.h"

namespace castleSpecialtyTest
{
using namespace estatesTest;

/// One Castle specialty hero and one ordinary hero on the shipped (revised) or a
/// historical capture without the revised specialty row.
class CastleSpecialtyFixture : public RevisedFixture
{
protected:
	bool historicalSpecialties = false;
	CGHeroInstance * specialist = nullptr;
	CGHeroInstance * ordinary = nullptr;

	CastleSpecialtyFixture()
	{
		estatesPolicyVersion = 2;
	}

	void mapLoaded(CMap * loaded) override
	{
		RevisedFixture::mapLoaded(loaded);
		if(!historicalSpecialties)
			return;
		JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
		heroes.Struct().erase("revisedSpecialties");
		heroes.setOverrideFlag(true); // Replace, rather than merge over, the installed rules.
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(heroes));
	}

	void startWith(const char * hero)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PLAYER)
			.town({7, 7, 0}, FactionID::CASTLE, PLAYER).townGarrison({})
			.hero({30, 30, 0}, HeroTypeID(HeroTypeID::decode(hero)), PLAYER)
				.heroGarrison({{creature("core:pikeman"), 1}})
			.hero({30, 26, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PLAYER)
				.heroGarrison({{creature("core:pikeman"), 1}})
			.dwelling({24, 20, 0}, MapObjectSubID(56), PLAYER);
		startWithMap(std::move(builder));
		specialist = findHeroAt({30, 30, 0});
		ordinary = findHeroAt({30, 26, 0});
		town = expectAt<CGTownInstance>({7, 7, 0});
		dwelling = expectAt<CGDwelling>({24, 20, 0});
		ASSERT_TRUE(specialist && ordinary && town && dwelling);
	}
};
}
