/*
 * NewHorizonsSpecialtyCapturedPresentationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "FullGameSnapshotTypes.h"
#include "../../../client/windows/HeroSpecialtyPresentation.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <array>

namespace
{
struct CapturedCase
{
	const char * id;
	const char * artwork;
	int3 position;
};
const std::array<CapturedCase, 3> cases = {{
	{"core:serena", "newHorizons/heroSpecialties/SerenaArcanePreparation", {5, 5, 0}},
	{"core:gem", "newHorizons/heroSpecialties/GemRestorativeSorceress", {7, 5, 0}},
	{"core:daremyth", "newHorizons/heroSpecialties/DaremythFortunesChild", {7, 7, 0}}
}};

void expectSamePresentation(const newHorizonsHeroes::SpecialtyPresentation & actual,
	const newHorizonsHeroes::SpecialtyPresentation & expected)
{
	EXPECT_EQ(actual.name, expected.name);
	EXPECT_EQ(actual.description, expected.description);
	EXPECT_EQ(actual.creature, expected.creature);
	EXPECT_EQ(actual.spell, expected.spell);
	EXPECT_EQ(actual.skill, expected.skill);
	EXPECT_EQ(actual.artwork, expected.artwork);
}
}

class NewHorizonsSpecialtyCapturedPresentationTest : public HeroCommandFixture
{
protected:
	bool historical = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons content";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode development(JsonPath::builtin("config/newHorizonsHeroes"));
		if(historical)
			for(const char * key : {"arcanePreparationSpecialty", "fortuneChildSpecialty", "druidSpecialties"})
				development.Struct().erase(key);
		development.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, development);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1));
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		for(size_t i = 0; i < cases.size(); ++i)
			builder.hero(cases[i].position, HeroTypeID(HeroTypeID::decode(cases[i].id)),
				PlayerColor(i == 2 ? 1 : 0)).heroExperience(0).heroGarrison({{pikeman, 1}});
		startWithMap(std::move(builder));
		for(size_t i = 0; i < cases.size(); ++i)
			ASSERT_NE(hero(i), nullptr);
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
	}

	CGHeroInstance * hero(size_t index) const
	{
		return findHeroAt(cases.at(index).position);
	}

	std::vector<std::byte> randomState() const
	{
		CMemorySerializer snapshot;
		snapshot.oser & *gameHandler->randomizer;
		snapshot.oser & CRandomGenerator::getDefault();
		return snapshot.extractBuffer();
	}

	void expectRestored(CGameState & restored)
	{
		GameRandomizer randomizer(restored);
		for(size_t i = 0; i < cases.size(); ++i)
		{
			SCOPED_TRACE(cases[i].id);
			const auto * original = hero(i);
			auto * loaded = const_cast<CGHeroInstance *>(restored.getHero(original->id));
			ASSERT_NE(loaded, nullptr);
			EXPECT_EQ(loaded->getPrimaryGrowthRules(), original->getPrimaryGrowthRules());
			EXPECT_EQ(loaded->getMagicRules(), original->getMagicRules());
			const auto before = original->getSpecialtyPresentation();
			if(!historical)
			{
				ASSERT_TRUE(before.artwork);
				EXPECT_EQ(*before.artwork, cases[i].artwork);
			}
			expectSamePresentation(loaded->getSpecialtyPresentation(), before);
			const auto originalCard = heroSpecialtyPresentation(*original);
			const auto loadedCard = heroSpecialtyPresentation(*loaded);
			EXPECT_EQ(loadedCard.animation(), originalCard.animation());
			EXPECT_EQ(loadedCard.animation(true), originalCard.animation(true));
			EXPECT_EQ(loadedCard.frame(), originalCard.frame());
			ASSERT_NO_THROW(loaded->initHero(randomizer));
			expectSamePresentation(loaded->getSpecialtyPresentation(), before);
			EXPECT_EQ(loaded->getPrimaryGrowthRules(), original->getPrimaryGrowthRules());
		}
		EXPECT_EQ(restored.getHeroDevelopmentRules(), gameState()->getHeroDevelopmentRules());
	}
};

TEST_F(NewHorizonsSpecialtyCapturedPresentationTest, LiveCapturedAndDetachedPreviewAgreeWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto stateBefore = gameState()->saveToMemory();
	const auto rngBefore = randomState();
	for(size_t i = 0; i < cases.size(); ++i)
	{
		SCOPED_TRACE(cases[i].id);
		const auto * liveHero = hero(i);
		const auto * prototype = liveHero->getHeroType();
		const auto localBefore = liveHero->getExportedBonusList().toJsonNode();
		const auto prototypeBefore = prototype->specialty.toJsonNode();
		const std::vector<std::shared_ptr<Bonus>> pointersBefore(
			liveHero->getExportedBonusList().begin(), liveHero->getExportedBonusList().end());
		const auto live = liveHero->getSpecialtyPresentation();
		ASSERT_TRUE(live.artwork);
		EXPECT_EQ(*live.artwork, cases[i].artwork);
		const auto explicitCaptured = newHorizonsHeroes::resolveSpecialtyPresentation(
			*prototype, liveHero->getPrimaryGrowthRules(), liveHero->getMagicRules(), std::nullopt, liveHero);
		expectSamePresentation(explicitCaptured, live);
		const auto preview = newHorizonsHeroes::resolveSpecialtyPresentation(
			*prototype, liveHero->getPrimaryGrowthRules(), liveHero->getMagicRules(), std::nullopt);
		EXPECT_EQ(preview.artwork, live.artwork);
		EXPECT_EQ(preview.name, live.name);
		EXPECT_EQ(preview.spell, live.spell);
		EXPECT_EQ(preview.skill, live.skill);
		// Detached preview omits live "Current" readback; descriptions need not equal.
		EXPECT_EQ(liveHero->getSpecialtyDescriptionTranslated(), live.description);
		EXPECT_EQ(heroSpecialtyPresentation(*liveHero).description, live.description);
		EXPECT_EQ(liveHero->getExportedBonusList().toJsonNode(), localBefore);
		EXPECT_EQ(prototype->specialty.toJsonNode(), prototypeBefore);
		EXPECT_EQ((std::vector<std::shared_ptr<Bonus>>(liveHero->getExportedBonusList().begin(),
			liveHero->getExportedBonusList().end())), pointersBefore);
	}
	EXPECT_EQ(gameState()->saveToMemory(), stateBefore);
	EXPECT_EQ(randomState(), rngBefore);
}

TEST_F(NewHorizonsSpecialtyCapturedPresentationTest, CapturedContextMismatchThrowsBeforeResolvingArt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(size_t i = 0; i < cases.size(); ++i)
	{
		const auto * captured = hero(i);
		const auto & development = captured->getPrimaryGrowthRules();
		const auto & magic = captured->getMagicRules();
		EXPECT_THROW(newHorizonsHeroes::resolveSpecialtyPresentation(
			*hero((i + 1) % cases.size())->getHeroType(), development, magic, std::nullopt, captured),
			std::invalid_argument);
		EXPECT_THROW(newHorizonsHeroes::resolveSpecialtyPresentation(
			*captured->getHeroType(), JsonNode(), magic, std::nullopt, captured), std::invalid_argument);
		EXPECT_THROW(newHorizonsHeroes::resolveSpecialtyPresentation(
			*captured->getHeroType(), development, JsonNode(), std::nullopt, captured), std::invalid_argument);
	}
}

TEST_F(NewHorizonsSpecialtyCapturedPresentationTest, DaremythNeedsActualLiveLuckMarkerEvenAtLevelOne)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * daremyth = hero(2);
	ASSERT_TRUE(daremyth->getSpecialtyPresentation().artwork);
	const auto marker = "new-horizons:fortune-child:" + std::to_string(daremyth->getHeroTypeID().getNum());
	std::vector<std::shared_ptr<Bonus>> markers;
	for(const auto & bonus : daremyth->getExportedBonusList())
		if(bonus && bonus->type == BonusType::LUCK && bonus->stacking == marker)
			markers.push_back(bonus);
	ASSERT_FALSE(markers.empty());
	for(const auto & bonus : markers)
		daremyth->removeBonus(bonus);
	EXPECT_FALSE(daremyth->getSpecialtyPresentation().artwork);
	// Detached preview retains producer eligibility; no widget may infer it for a live hero.
	EXPECT_TRUE(newHorizonsHeroes::resolveSpecialtyPresentation(*daremyth->getHeroType(),
		daremyth->getPrimaryGrowthRules(), daremyth->getMagicRules(), std::nullopt).artwork);
}

TEST_F(NewHorizonsSpecialtyCapturedPresentationTest, WholeWorldCloneRetainsAllThreeCapturedIdentities)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CMemorySerializer memory;
	ASSERT_NO_THROW(memory.oser & *gameState());
	CGameState restored;
	restored.preInit(LIBRARY);
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	ASSERT_NO_THROW(memory.iser & restored);
	ASSERT_NO_FATAL_FAILURE(expectRestored(restored));
}

TEST_F(NewHorizonsSpecialtyCapturedPresentationTest, CurrentMemorySaveRetainsAllThreeCapturedIdentities)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CGameState restored;
	restored.preInit(LIBRARY);
	ASSERT_NO_THROW(restored.loadFromMemory(gameState()->saveToMemory()));
	ASSERT_NO_FATAL_FAILURE(expectRestored(restored));
}

TEST_F(NewHorizonsSpecialtyCapturedPresentationTest, SavedPolicyAbsenceDoesNotRecaptureInstalledDefaults)
{
	historical = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const JsonNode installed(JsonPath::builtin("config/newHorizonsHeroes"));
	for(const char * key : {"arcanePreparationSpecialty", "fortuneChildSpecialty", "druidSpecialties"})
	{
		ASSERT_TRUE(installed[key].isStruct());
		EXPECT_FALSE(gameState()->getHeroDevelopmentRules().Struct().contains(key));
		for(size_t i = 0; i < cases.size(); ++i)
			EXPECT_FALSE(hero(i)->getPrimaryGrowthRules().Struct().contains(key));
	}
	for(size_t i = 0; i < cases.size(); ++i)
		EXPECT_FALSE(hero(i)->getSpecialtyPresentation().artwork);
	const auto oldDaremyth = hero(2)->getSpecialtyPresentation();
	EXPECT_EQ(oldDaremyth.spell, SpellID(SpellID::HASTE));
	EXPECT_EQ(heroSpecialtyPresentation(*hero(2)).animation().getOriginalName(), "SPELLSCR");
	CGameState restored;
	restored.preInit(LIBRARY);
	ASSERT_NO_THROW(restored.loadFromMemory(gameState()->saveToMemory()));
	ASSERT_NO_FATAL_FAILURE(expectRestored(restored));
}
