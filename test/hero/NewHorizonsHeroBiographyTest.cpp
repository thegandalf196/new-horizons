/*
 * NewHorizonsHeroBiographyTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"

#include "../../lib/GameConstants.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/CompositeTranslator.h"
#include "../../lib/texts/TextIdentifier.h"
#include "../../client/windows/HeroStartingPreview.h"

#include <array>
#include <map>
#include <map>
#include <set>
#include <string>

namespace
{
const std::array<const char *, 9> BIOGRAPHY_FACTIONS = {
	"castle", "rampart", "tower", "inferno", "necropolis", "dungeon", "stronghold", "fortress", "conflux"
};

bool newHorizonsModuleActive()
{
	return vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE);
}
}

class NewHorizonsHeroBiographyTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!newHorizonsModuleActive())
			GTEST_SKIP() << "Requires the New Horizons module for its loaded biography overlay";
	}
};

TEST_F(NewHorizonsHeroBiographyTest, LoadedHeroTypesUseThe52ReviewedOverridesAndKeep92Inheritances)
{
	std::map<std::string, ExportedStrings> registeredTexts;
	LIBRARY->generaltexth->exportAllTexts(registeredTexts, false);
	const auto newHorizonsTexts = registeredTexts.find(GameConstants::NEW_HORIZONS_MOD_SCOPE);

	std::set<std::string> seenHeroKeys;
	size_t overrideCount = 0;
	size_t inheritanceCount = 0;

	for(const auto * faction : BIOGRAPHY_FACTIONS)
	{
		const JsonNode decisions(JsonPath::builtin(
			std::string("config/newHorizonsHeroBiographies/") + faction + ".json"));
		ASSERT_TRUE(decisions.isStruct());
		ASSERT_EQ(decisions.Struct().size(), 16u) << faction;

		for(const auto & [heroName, decision] : decisions.Struct())
		{
			const std::string heroKey = std::string("core:") + heroName;
			SCOPED_TRACE(heroKey);
			ASSERT_TRUE(seenHeroKeys.insert(heroKey).second);

			const HeroTypeID heroTypeId(HeroTypeID::decode(heroKey));
			ASSERT_TRUE(heroTypeId.hasValue());
			const auto * heroType = heroTypeId.toHeroType();
			ASSERT_NE(heroType, nullptr);

			const bool registeredAsNewHorizonsOverride = newHorizonsTexts != registeredTexts.end()
				&& newHorizonsTexts->second.strings.count(heroType->getBiographyTextID()) != 0;
			if(decision.isNull())
			{
				++inheritanceCount;
				EXPECT_FALSE(registeredAsNewHorizonsOverride);
				// Never print inherited text in a failure: this resolves from the
				// purchaser-installed original biography.
				EXPECT_FALSE(heroType->getBiographyTranslated().empty());
				continue;
			}

			ASSERT_TRUE(decision.isString());
			++overrideCount;
			EXPECT_TRUE(registeredAsNewHorizonsOverride);
			// This calls the production CHero text accessor, backed by the
			// loaded CGeneralTextHandler and active module content.
			EXPECT_TRUE(heroType->getBiographyTranslated() == decision.String());
		}
	}

	EXPECT_EQ(seenHeroKeys.size(), 144u);
	EXPECT_EQ(overrideCount, 52u);
	EXPECT_EQ(inheritanceCount, 92u);
}

TEST_F(NewHorizonsHeroBiographyTest, LiveInstanceCustomBiographyTextTakesPrecedence)
{
	const HeroTypeID sylvia(HeroTypeID::decode("core:sylvia"));
	ASSERT_TRUE(sylvia.hasValue());

	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, sylvia, PlayerColor(0));
	startWithMap(std::move(builder));

	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(hero->getHeroType(), nullptr);
	ASSERT_FALSE(hero->getHeroType()->getBiographyTranslated().empty());

	const std::string customTextId = "test.newHorizons.mapHeroBiography";
	const std::string customBiography = "Map-authored biography sentinel.";
	LIBRARY->generaltexth->registerString("core", TextIdentifier(customTextId), customBiography);
	hero->biographyCustomTextId = customTextId;

	EXPECT_EQ(hero->getBiographyTextID(), customTextId);
	EXPECT_EQ(LIBRARY->generaltexth->translate(hero->getBiographyTextID()), customBiography);
}

TEST_F(NewHorizonsHeroBiographyTest, StartingPreviewBiographyWithoutInstanceKeepsReviewedPrototype)
{
	const auto * prototype = HeroTypeID(HeroTypeID::decode("core:sylvia")).toHeroType();
	ASSERT_NE(prototype, nullptr);
	const auto result = heroStartingBiography(*prototype, *LIBRARY->generaltexth);
	EXPECT_EQ(result.textID, prototype->getBiographyTextID());
	EXPECT_EQ(result.translated, prototype->getBiographyTranslated());
	EXPECT_FALSE(result.translated.empty());
}

TEST_F(NewHorizonsHeroBiographyTest, StartingPreviewCopiesCustomBiographyBeforeMapContextChanges)
{
	const HeroTypeID sylvia(HeroTypeID::decode("core:sylvia"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0)).hero({5, 5, 0}, sylvia, PlayerColor(0));
	startWithMap(std::move(builder));
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	const std::string customID = "test.newHorizons.startingPreviewBiography";
	const std::string customText = "Selected-map biography sentinel.";
	LIBRARY->generaltexth->registerString("core", TextIdentifier(customID), customText);
	hero->biographyCustomTextId = customID;
	const auto beforeBonuses = hero->getExportedBonusList().toJsonNode();
	const auto beforeRules = hero->getPrimaryGrowthRules();
	const auto result = heroStartingBiography(*hero->getHeroType(), *LIBRARY->generaltexth, hero);
	EXPECT_EQ(result.textID, customID);
	EXPECT_EQ(result.translated, customText);
	EXPECT_EQ(hero->biographyCustomTextId, customID);
	EXPECT_EQ(hero->getExportedBonusList().toJsonNode(), beforeBonuses);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), beforeRules);
	// Prepared text is a value copy, not a pointer into the temporary map.
	hero->biographyCustomTextId.clear();
	EXPECT_EQ(result.textID, customID);
	EXPECT_EQ(result.translated, customText);
	const auto fallback = heroStartingBiography(*hero->getHeroType(), *LIBRARY->generaltexth, hero);
	EXPECT_EQ(fallback.textID, hero->getHeroType()->getBiographyTextID());
	EXPECT_EQ(fallback.translated, hero->getHeroType()->getBiographyTranslated());
}

TEST_F(NewHorizonsHeroBiographyTest, StartingPreviewBiographyRejectsUnrelatedHero)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:sylvia")), PlayerColor(0));
	startWithMap(std::move(builder));
	const auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	const auto * other = HeroTypeID(HeroTypeID::decode("core:ash")).toHeroType();
	ASSERT_NE(other, nullptr);
	EXPECT_THROW(heroStartingBiography(*other, *LIBRARY->generaltexth, hero), std::invalid_argument);
}

TEST_F(NewHorizonsHeroBiographyTest, StartingPreviewResolvesMapLocalBiographyWithoutGlobalRegistration)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:sylvia")), PlayerColor(0));
	startWithMap(std::move(builder));
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(map(), nullptr);
	const std::string customID = "map.test.starting_preview_local_biography";
	const std::string customText = "Private map-local biography sentinel.";
	ASSERT_FALSE(LIBRARY->generaltexth->identifierExists(TextIdentifier(customID)));
	hero->biographyCustomTextId = mapRegisterLocalizedString("map", *map(),
		TextIdentifier(customID), customText, "english");
	ASSERT_TRUE(map()->texts->identifierExists(TextIdentifier(customID)));
	const auto beforeBonuses = hero->getExportedBonusList().toJsonNode();
	const auto beforeRules = hero->getPrimaryGrowthRules();
	HeroStartingBiography result;
	{
		CompositeTranslator translator;
		translator.install(map()->texts);
		result = heroStartingBiography(*hero->getHeroType(), translator, hero);
		EXPECT_EQ(result.textID, customID);
		EXPECT_EQ(result.translated, customText);
		// Preserve the existing CompositeTranslator static-override-first policy.
		const auto * prototype = hero->getHeroType();
		mapRegisterLocalizedString("map", *map(), TextIdentifier(prototype->getBiographyTextID()),
			"Map text must not shadow a static translation.", "english");
		const auto staticResult = heroStartingBiography(*prototype, translator);
		EXPECT_EQ(staticResult.translated, prototype->getBiographyTranslated());
		translator.clear();
	}
	EXPECT_EQ(result.textID, customID);
	EXPECT_EQ(result.translated, customText);
	EXPECT_EQ(hero->biographyCustomTextId, customID);
	EXPECT_EQ(hero->getExportedBonusList().toJsonNode(), beforeBonuses);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), beforeRules);
	EXPECT_FALSE(LIBRARY->generaltexth->identifierExists(TextIdentifier(customID)));
}
