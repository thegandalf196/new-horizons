/*
 * NewHorizonsPerkRewardContextTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

namespace
{
class NewHorizonsPerkRewardContextTest : public TinyMapGameTest
{
protected:
	bool legacyPolicy = false;

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		if(legacyPolicy)
		{
			perks.Struct().erase("offerWeightPolicyVersion");
			perks.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(6), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 10}});
		startWithMap(std::move(builder));
	}

	HeroLevelUp matchingPacket(CGHeroInstance * hero) const
	{
		HeroLevelUp packet;
		packet.player = hero->getOwner();
		packet.heroId = hero->id;
		packet.perkOfferSeed = 42;
		packet.perks = hero->getPerkState().prepareOffer([hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		}, packet.perkOfferSeed, hero->getPerkOfferWeightContext());
		return packet;
	}
};
}

TEST_F(NewHorizonsPerkRewardContextTest, ExperienceRewardAppliesCapturedWeightedOfferThroughSharedGameState)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(newHorizonsHeroes::usesWeightedPerkOffers(hero->getPerkState().rules));
	ASSERT_EQ(hero->getPerkOfferWeightContext().mode,
		newHorizonsHeroes::PerkState::OfferWeightMode::CapturedTable);
	GameHandlerTestServer server(gameState(), hero->getOwner());
	CGameHandler handler(server, gameState());
	handler.onAdvInterfaceReady(hero->getOwner());
	const auto oldLevel = hero->level;
	const auto targetExperience = LIBRARY->heroh->reqExp(oldLevel + 1);
	ASSERT_GT(targetExperience, hero->exp);
	// Same server reward endpoint used by rewardable::Interface (Learning Stone).
	// GameHandlerTestServer applies real SetPrimarySkill and HeroLevelUp packets.
	EXPECT_NO_THROW(handler.giveExperience(hero, targetExperience - hero->exp));
	EXPECT_EQ(hero->exp, targetExperience);
	EXPECT_EQ(hero->level, oldLevel + 1);
	const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
		handler.queries->topQuery(hero->getOwner()));
	ASSERT_NE(query, nullptr);
	ASSERT_FALSE(query->hlu.perks.empty());
	EXPECT_EQ(query->hlu.perks, hero->getPerkState().prepareOffer([hero](const std::string & skill)
	{
		return hero->getPerkSkillRank(skill);
	}, query->hlu.perkOfferSeed, hero->getPerkOfferWeightContext()));
}

TEST_F(NewHorizonsPerkRewardContextTest, ForgedWeightedPacketIsRejectedBeforeLevelOrPrimaryMutation)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	auto packet = matchingPacket(hero);
	ASSERT_FALSE(packet.perks.empty());
	packet.perks.front().name += " forged";
	packet.primaryGains[0] = 9;
	const auto oldLevel = hero->level;
	const auto oldAttack = hero->getPrimSkillLevel(PrimarySkill::ATTACK);
	const auto oldExperience = hero->exp;
	EXPECT_THROW(gameState()->apply(packet), std::runtime_error);
	EXPECT_EQ(hero->level, oldLevel);
	EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::ATTACK), oldAttack);
	EXPECT_EQ(hero->exp, oldExperience);
}

TEST_F(NewHorizonsPerkRewardContextTest, CapturedLegacyPolicyRetainsMatchingUniformPacketValidation)
{
	legacyPolicy = true;
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_FALSE(newHorizonsHeroes::usesWeightedPerkOffers(hero->getPerkState().rules));
	auto packet = matchingPacket(hero);
	ASSERT_FALSE(packet.perks.empty());
	EXPECT_EQ(packet.perks, hero->getPerkState().prepareOffer([hero](const std::string & skill)
	{
		return hero->getPerkSkillRank(skill);
	}, packet.perkOfferSeed));
	const auto oldLevel = hero->level;
	EXPECT_NO_THROW(gameState()->apply(packet));
	EXPECT_EQ(hero->level, oldLevel + 1);
}
