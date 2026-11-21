/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../ActivatedOrdinarySpecialties.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsAdditionalSpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../../lib/entities/hero/OrdinaryCreatureSpecialtyRegistry.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusList.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
using ActivatedSpecialty = ActivatedOrdinarySpecialty;

const newHorizonsHeroes::detail::OrdinaryCreatureSpecialty & registryRow(std::string_view hero)
{
	for(const auto & row : newHorizonsHeroes::detail::ordinaryCreatureSpecialtyRegistry())
		if(row.hero == hero)
			return row;
	throw std::runtime_error("Missing ordinary specialty registry row");
}

size_t creatureLineMarkers(const CGHeroInstance & hero)
{
	const auto & bonuses = hero.getExportedBonusList();
	return std::ranges::count_if(bonuses, [](const auto & bonus)
	{
		return bonus && bonus->stacking.starts_with("new-horizons:creature-line-specialty:");
	});
}

class ActivatedOrdinarySpecialtyFixture : public HeroCommandFixture
{
protected:
	bool historical = false;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		if(!historical)
			return;
		// A 0.93.0 capture: shipped rules without the coefficients this release adds.
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		omitActivatedOrdinaryCoefficients(rules);
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(const char * hero)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 10}});
		ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder)));
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroType()->getJsonKey(), hero);
	}
	std::shared_ptr<CGameState> reload()
	{
		auto restored = std::make_shared<CGameState>();
		restored->preInit(LIBRARY);
		restored->loadFromMemory(gameState()->saveToMemory());
		return restored;
	}
};
class NewHorizonsActivatedOrdinarySpecialtyTest : public ActivatedOrdinarySpecialtyFixture,
	public ::testing::WithParamInterface<ActivatedSpecialty>
{
};
class NewHorizonsDeferredOrdinarySpecialtyTest : public ActivatedOrdinarySpecialtyFixture,
	public ::testing::WithParamInterface<std::pair<const char *, const char *>>
{
};
}

TEST_P(NewHorizonsActivatedOrdinarySpecialtyTest, FreshCaptureHasCoefficientAndReplacesGenericRider)
{
	const auto & row = GetParam();
	ASSERT_NO_FATAL_FAILURE(prepare(row.hero));
	const auto & captured = attackerSideHero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	for(const auto & [key, value] : row.coefficients)
	{
		SCOPED_TRACE(key);
		ASSERT_TRUE(captured.Struct().contains(key));
		EXPECT_EQ(captured[key].Integer(), value);
	}
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_TRUE(newHorizonsHeroes::replacesGenericCreatureSpecialty(attackerSideHero));
	EXPECT_EQ(creatureLineMarkers(*attackerSideHero), 0u);
	const auto presentation = attackerSideHero->getSpecialtyPresentation();
	EXPECT_EQ(presentation.name, row.title);
	EXPECT_EQ(presentation.name, registryRow(row.hero).name);
	EXPECT_EQ(presentation.description.find("%VALUE%"), std::string::npos);
	EXPECT_EQ(presentation.description.rfind(std::string(registryRow(row.hero).description).substr(0, 20), 0), 0u);

	const auto id = attackerSideHero->id;
	const auto restored = reload();
	const auto * loaded = restored->getHero(id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(loaded));
	EXPECT_EQ(creatureLineMarkers(*loaded), 0u);
	EXPECT_EQ(loaded->getSpecialtyPresentation().name, row.title);
}

TEST_P(NewHorizonsActivatedOrdinarySpecialtyTest, HistoricalCaptureKeepsGenericRiderThroughReload)
{
	const auto & row = GetParam();
	historical = true;
	ASSERT_NO_FATAL_FAILURE(prepare(row.hero));
	const auto & captured = attackerSideHero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	for(const auto & [key, value] : row.coefficients)
		EXPECT_FALSE(captured.Struct().contains(key)) << key;
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_GT(creatureLineMarkers(*attackerSideHero), 0u);
	EXPECT_NE(attackerSideHero->getSpecialtyPresentation().name, row.title);

	const auto id = attackerSideHero->id;
	const auto restored = reload();
	const auto * loaded = restored->getHero(id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(loaded));
	EXPECT_GT(creatureLineMarkers(*loaded), 0u);
}

INSTANTIATE_TEST_SUITE_P(ActivatedExtendedOrdinary, NewHorizonsActivatedOrdinarySpecialtyTest,
	::testing::ValuesIn(activatedOrdinarySpecialties()),
	[](const ::testing::TestParamInfo<ActivatedSpecialty> & info) { return std::string(info.param.hero).substr(5); });

TEST(NewHorizonsActivatedOrdinarySpecialtyPolicy, ShippedRulesActivateExactlyTheUnchangedExtendedRows)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	for(const auto & row : activatedOrdinarySpecialties())
		for(const auto & [key, value] : row.coefficients)
		{
			SCOPED_TRACE(key);
			ASSERT_TRUE(policy.Struct().contains(key));
			EXPECT_EQ(policy[key].Integer(), value);
		}
	for(const auto & [hero, key] : deferredOrdinarySpecialties())
	{
		SCOPED_TRACE(hero);
		EXPECT_FALSE(policy.Struct().contains(key));
		EXPECT_EQ(registryRow(hero).coefficient, std::string(key));
	}
	// Every registry row is now either shipped, prepared, or one of the deferred rows.
	const auto & prepared = rules["additionalSpecialties"]["preparedCreatureSpecialties"]["heroes"];
	for(const auto & row : newHorizonsHeroes::detail::ordinaryCreatureSpecialtyRegistry())
	{
		SCOPED_TRACE(row.hero);
		const bool deferred = std::ranges::any_of(deferredOrdinarySpecialties(), [&row](const auto & entry)
		{ return std::string_view(entry.first) == row.hero; });
		EXPECT_NE(policy.Struct().contains(row.coefficient) || prepared.Struct().contains(row.hero), deferred);
	}
}

TEST_P(NewHorizonsDeferredOrdinarySpecialtyTest, ChangedRowsKeepTheirGenericRiderInFreshGames)
{
	const auto & [hero, key] = GetParam();
	ASSERT_NO_FATAL_FAILURE(prepare(hero));
	EXPECT_FALSE(attackerSideHero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"]
		.Struct().contains(key));
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_GT(creatureLineMarkers(*attackerSideHero), 0u);
	EXPECT_NE(attackerSideHero->getSpecialtyPresentation().name, registryRow(hero).name);
}

INSTANTIATE_TEST_SUITE_P(DeferredExtendedOrdinary, NewHorizonsDeferredOrdinarySpecialtyTest,
	::testing::ValuesIn(deferredOrdinarySpecialties()),
	[](const ::testing::TestParamInfo<std::pair<const char *, const char *>> & info)
	{ return std::string(info.param.first).substr(5); });
