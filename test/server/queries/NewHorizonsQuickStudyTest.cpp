/*
 * NewHorizonsQuickStudyTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include <gtest/gtest.h>

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/constants/Enumerations.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

namespace
{
constexpr auto LEARNING_SKILL = "new-horizons:learning";
constexpr auto MENTOR = "new-horizons:learning.mentor";
constexpr auto QUICK_STUDY = "new-horizons:learning.quickStudy";

struct ExpectedOffer
{
	std::vector<SecondarySkill> skills;
	std::vector<newHorizonsHeroes::PerkOfferCandidate> perks;
	int64_t perkOfferSeed = 0;
};

ExpectedOffer prepareExpectedOffer(GameRandomizer & randomizer, const CGHeroInstance * hero)
{
	ExpectedOffer result;
	result.skills = randomizer.rollSecondarySkills(hero);
	const auto & perkState = hero->getPerkState();
	if(!newHorizonsHeroes::usesPerkRules(perkState.rules))
		return result;

	std::erase_if(result.skills, [hero, &perkState](SecondarySkill skill)
	{
		const auto * definition = LIBRARY->skillh->getById(skill);
		return !definition || !perkState.canAdvanceSkillNormally(definition->getJsonKey(),
			hero->getSecSkillLevel(skill));
	});
	const size_t maxSkillChoices = static_cast<size_t>(perkState.rules["maxSkillChoices"].Integer());
	if(result.skills.size() > maxSkillChoices)
		result.skills.resize(maxSkillChoices);

	result.perkOfferSeed = static_cast<uint32_t>(randomizer.getDefault().nextInt());
	result.perks = perkState.prepareOffer([hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	}, static_cast<uint64_t>(result.perkOfferSeed));
	return result;
}

class NewHorizonsQuickStudyTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		auto perkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perkRules["skills"][LEARNING_SKILL]["perks"].Vector())
		{
			if(perk["id"].String() == QUICK_STUDY || perk["id"].String() == MENTOR)
				perk["effect"]["status"].String() = "active";
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_MASTERIES, JsonNode());
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 10}});
		startWithMap(std::move(builder));

		hero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(hero, nullptr);
		server = std::make_unique<GameHandlerTestServer>(gameState(), PlayerColor(0));
		gameHandler = std::make_unique<CGameHandler>(*server, gameState());
		gameHandler->randomizer->setSeed(1337);
	}

	void setLearningRank(int rank)
	{
		const int skillId = SecondarySkill::decode(LEARNING_SKILL);
		ASSERT_GE(skillId, 0);
		hero->setSecSkillLevel(SecondarySkill(skillId), rank, ChangeValueMode::ABSOLUTE);
	}

	void selectQuickStudy()
	{
		setLearningRank(MasteryLevel::BASIC);
		hero->applyPerkSelection({LEARNING_SKILL, MENTOR});
		setLearningRank(MasteryLevel::ADVANCED);
		hero->applyPerkSelection({LEARNING_SKILL, QUICK_STUDY});
	}

	void markQuickStudyPlanned()
	{
		auto & state = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
		for(auto & perk : state.rules["skills"][LEARNING_SKILL]["perks"].Vector())
			if(perk["id"].String() == QUICK_STUDY)
				perk["effect"]["status"].String() = "planned";
	}

	void expectLevelUp(int reachedLevel, bool expectReroll)
	{
		hero->level = reachedLevel - 1;
		hero->setExperience(LIBRARY->heroh->reqExp(reachedLevel), ChangeValueMode::ABSOLUTE);
		const auto owner = hero->getOwner();
		gameHandler->onAdvInterfaceReady(owner);

		CMemorySerializer randomizerSnapshot;
		randomizerSnapshot.oser & *gameHandler->randomizer;
		GameRandomizer expectedRandomizer(*gameState());
		randomizerSnapshot.iser & expectedRandomizer;
		const auto expectedPrimaryGains = expectedRandomizer.rollPrimarySkillsForLevelup(hero);
		auto expectedOffer = prepareExpectedOffer(expectedRandomizer, hero);
		if(expectReroll)
			expectedOffer = prepareExpectedOffer(expectedRandomizer, hero);

		gameHandler->levelUpHero(hero);
		const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
			gameHandler->queries->topQuery(owner));
		ASSERT_NE(query, nullptr);
		EXPECT_EQ(query->hlu.primaryGains, expectedPrimaryGains);
		EXPECT_EQ(query->hlu.skills, expectedOffer.skills);
		EXPECT_EQ(query->hlu.perks, expectedOffer.perks);
		EXPECT_EQ(query->hlu.perkOfferSeed, expectedOffer.perkOfferSeed);
		EXPECT_EQ(hero->level, reachedLevel);

		// Re-exposure must not spend another draw. The same authoritative offer is
		// passed through the level-up callback used by both human UI and AI.
		query->onExposure(query);
		EXPECT_EQ(gameHandler->randomizer->rollSecondarySkills(hero),
			expectedRandomizer.rollSecondarySkills(hero));
		EXPECT_EQ(gameHandler->randomizer->getDefault().nextInt(),
			expectedRandomizer.getDefault().nextInt());

		ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, owner));
	}

	CGHeroInstance * hero = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> gameHandler;
};
}

TEST_F(NewHorizonsQuickStudyTest, RerollsTheWholeOfferOnceAtEveryFifthReachedLevel)
{
	startGame();
	setLearningRank(MasteryLevel::ADVANCED);
	selectQuickStudy();
	ASSERT_TRUE(hero->hasActivePerk(LEARNING_SKILL, QUICK_STUDY));

	expectLevelUp(5, true);
	expectLevelUp(10, true);
	expectLevelUp(13, false);
}

TEST_F(NewHorizonsQuickStudyTest, DoesNotRerollWithoutThePerkSelected)
{
	startGame();
	setLearningRank(MasteryLevel::ADVANCED);
	EXPECT_FALSE(hero->hasActivePerk(LEARNING_SKILL, QUICK_STUDY));
	expectLevelUp(5, false);
}

TEST_F(NewHorizonsQuickStudyTest, DoesNotRerollBelowThePerkRequiredRank)
{
	startGame();
	selectQuickStudy();
	setLearningRank(MasteryLevel::BASIC);
	EXPECT_FALSE(hero->hasActivePerk(LEARNING_SKILL, QUICK_STUDY));
	expectLevelUp(5, false);
}

TEST_F(NewHorizonsQuickStudyTest, DoesNotRerollWhenTheSavedPerkDefinitionIsPlanned)
{
	startGame();
	selectQuickStudy();
	markQuickStudyPlanned();
	EXPECT_FALSE(hero->hasActivePerk(LEARNING_SKILL, QUICK_STUDY));
	expectLevelUp(5, false);
}

TEST_F(NewHorizonsQuickStudyTest, DoesNotRerollAtANonFifthLevelWithThePerkActive)
{
	startGame();
	setLearningRank(MasteryLevel::ADVANCED);
	selectQuickStudy();
	ASSERT_TRUE(hero->hasActivePerk(LEARNING_SKILL, QUICK_STUDY));

	expectLevelUp(4, false);
}
