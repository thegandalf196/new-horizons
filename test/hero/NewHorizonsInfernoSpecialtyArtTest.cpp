/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsInfernoSpecialtyPresentation.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../client/windows/HeroSpecialtyPresentation.h"

TEST(InfernoSpecialtyArtwork, CapturedRowsUseSelectedNativeFamiliesAndAbsentRowsStayUnclaimed)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const JsonNode current(JsonPath::builtin("config/newHorizonsHeroes"));
	struct Row { const char * hero; const char * policy; const char * artwork; };
	for(const auto & row : {
		Row{"core:octavia", "cutpurse", "OctaviaGoldIncome"},
		Row{"core:calid", "sulfurSense", "CalidSulfurIncome"},
		Row{"core:xyron", "kindling", "XyronInferno"},
		Row{"core:olema", "markedForTheKill", "OlemaFrailty"},
		Row{"core:ash", "bloodfire", "AshFireball"},
		Row{"core:xarfax", "burnAway", "XarfaxFireball"},
		Row{"core:pyre", "hellfireBattery", "PyreBallistaLine"}})
	{
		SCOPED_TRACE(row.hero);
		const auto * hero = HeroTypeID(HeroTypeID::decode(row.hero)).toHeroType();
		ASSERT_TRUE(hero);
		newHorizonsHeroes::SpecialtyPresentation result;
		ASSERT_TRUE(newHorizonsHeroes::resolveInfernoSpecialtyPresentation(*hero, current, nullptr, result));
		const std::string base = "newHorizons/heroSpecialties/" + std::string(row.artwork);
		EXPECT_EQ(result.artwork, base);
		const auto card = heroSpecialtyPresentation(*hero, result);
		EXPECT_EQ(card.animation().getOriginalName(), base + "/44");
		EXPECT_EQ(card.animation(true).getOriginalName(), base + "/32");
		EXPECT_EQ(card.frame(), 0u);
		JsonNode previous = current;
		previous["revisedSpecialties"].Struct().erase(row.policy);
		newHorizonsHeroes::SpecialtyPresentation historical;
		EXPECT_FALSE(newHorizonsHeroes::resolveInfernoSpecialtyPresentation(*hero, previous, nullptr, historical));
		EXPECT_FALSE(historical.artwork);
	}
}
