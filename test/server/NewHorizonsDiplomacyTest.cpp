/*
 * NewHorizonsDiplomacyTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include "../../lib/GameConstants.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/NewHorizonsDiplomacy.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/Component.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include "../../lib/serializer/JsonDeserializer.h"
#include "../../lib/serializer/JsonSerializer.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../server/CGameHandler.h"
#include "../../server/queries/QueriesProcessor.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
using newHorizonsDiplomacy::ForecastInput;
using newHorizonsDiplomacy::resolveForecast;

constexpr PlayerColor PLAYER(0);
constexpr auto DIPLOMACY_SKILL = "new-horizons:diplomacy";

CreatureID creature(const char * id)
{
	return CreatureID(CreatureID::decode(id));
}

HeroTypeID heroType(const char * id)
{
	return HeroTypeID(HeroTypeID::decode(id));
}

class DiplomacyRecordingServer final : public GameHandlerTestServer
{
public:
	using GameHandlerTestServer::GameHandlerTestServer;

	void applyPack(CPackForClient & pack) override
	{
		if(const auto * dialog = dynamic_cast<const BlockingDialog *>(&pack))
			blockingDialogs.push_back(*dialog);
		if(const auto * dialog = dynamic_cast<const GarrisonDialog *>(&pack))
			garrisonDialogs.push_back(*dialog);
		if(const auto * message = dynamic_cast<const SystemMessage *>(&pack))
			systemMessageTexts.push_back(message->text.toString(LIBRARY->generaltexth.get()));
		GameHandlerTestServer::applyPack(pack);
	}

	std::vector<BlockingDialog> blockingDialogs;
	std::vector<GarrisonDialog> garrisonDialogs;
	std::vector<std::string> systemMessageTexts;
};

class NewHorizonsDiplomacyTest : public TinyMapGameTest
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
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));

		// Force a narrow Leadership ceiling so an accepted whole-stack offer
		// exercises the existing partial-transfer admission path deterministically.
		auto capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
		capabilities["leadership"]["creatureRequirements"]["core:pikeman"] = JsonNode(60);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, capabilities);
	}

	void startGame(int32_t heroPikemen, int32_t neutralPikemen, CGCreature::Character character)
	{
		const auto pikeman = creature("core:pikeman");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({5, 5, 0}, heroType("core:christian"), PLAYER)
			.heroGarrison({{pikeman, heroPikemen}})
			.monster({12, 12, 0}, pikeman, static_cast<uint16_t>(neutralPikemen),
				static_cast<int8_t>(character));
		startWithMap(std::move(builder));

		hero = findHeroByOwner(PLAYER);
		neutral = findFirst<CGCreature>();
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(neutral, nullptr);

		server = std::make_unique<DiplomacyRecordingServer>(gameState(), PLAYER);
		gameHandler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
	}

	static bool selectOfferedPerk(CGameHandler & handler, CGHeroInstance * candidate,
		std::string_view perkId, MasteryLevel::Type requiredRank)
	{
		const auto rankLookup = [candidate](const std::string & skillId)
		{
			return candidate->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offer = candidate->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t index = 0; index < offer.size(); ++index)
			{
				if(offer[index].selection.skillId != DIPLOMACY_SKILL
					|| offer[index].selection.perkId != perkId)
					continue;
				if(offer[index].requiredRank != requiredRank)
					return false;
				handler.levelUpHero(candidate, offer, index, seed, false);
				return candidate->hasActivePerk(DIPLOMACY_SKILL, std::string(perkId));
			}
		}
		return false;
	}

	void advanceToDiplomacyRank(MasteryLevel::Type targetRank,
		std::string_view basicPerk = {}, std::string_view advancedPerk = {},
		std::string_view expertPerk = {})
	{
		const int diplomacyIndex = SecondarySkill::decode(DIPLOMACY_SKILL);
		ASSERT_GE(diplomacyIndex, 0);
		const auto skill = SecondarySkill(diplomacyIndex);
		const std::array<std::string_view, 3> perks = {basicPerk, advancedPerk, expertPerk};
		for(int rank = 1; rank <= static_cast<int>(targetRank); ++rank)
		{
			gameHandler->levelUpHero(hero, skill, false);
			ASSERT_EQ(hero->getSecSkillLevel(skill), rank);
			if(!perks[static_cast<size_t>(rank - 1)].empty())
				ASSERT_TRUE(selectOfferedPerk(*gameHandler, hero, perks[static_cast<size_t>(rank - 1)],
					static_cast<MasteryLevel::Type>(rank)));
		}
	}

	CGHeroInstance * hero = nullptr;
	CGCreature * neutral = nullptr;
	std::unique_ptr<DiplomacyRecordingServer> server;
	std::unique_ptr<CGameHandler> gameHandler;
};

class NewHorizonsDiplomacyLegacyGateTest : public NewHorizonsDiplomacyTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode());
	}
};

ForecastInput eligibleInput()
{
	ForecastInput input;
	input.usesNewHorizonsRules = true;
	input.encounterEligible = true;
	input.skillRank = 1;
	input.heroArmyValue = 400;
	input.creatureArmyValue = 100;
	input.goldCostPerCreature = 50;
	input.joiningAmount = 3;
	return input;
}
}

TEST(NewHorizonsDiplomacy, UsesExactRankAndPerkThresholdBoundaries)
{
	auto input = eligibleInput();
	const auto basicBoundary = resolveForecast(input);
	EXPECT_TRUE(basicBoundary.usesNewHorizonsRules);
	EXPECT_TRUE(basicBoundary.active);
	EXPECT_TRUE(basicBoundary.eligible);
	EXPECT_TRUE(basicBoundary.willing);
	EXPECT_EQ(basicBoundary.skillRank, 1);
	EXPECT_EQ(basicBoundary.thresholdPercent, 25);
	EXPECT_EQ(basicBoundary.heroArmyValue, 400u);
	EXPECT_EQ(basicBoundary.creatureArmyValue, 100u);
	EXPECT_EQ(basicBoundary.normalGoldCost, 150);
	EXPECT_EQ(basicBoundary.joiningAmount, 3);

	input.creatureArmyValue = 101;
	EXPECT_FALSE(resolveForecast(input).willing)
		<< "Basic Diplomacy accepts exactly 25%, not the next unit above the threshold";

	input = eligibleInput();
	input.negotiator = true;
	input.creatureArmyValue = 160;
	const auto negotiatorBoundary = resolveForecast(input);
	EXPECT_EQ(negotiatorBoundary.thresholdPercent, 40);
	EXPECT_TRUE(negotiatorBoundary.negotiator);
	EXPECT_TRUE(negotiatorBoundary.willing);
	input.creatureArmyValue = 161;
	EXPECT_FALSE(resolveForecast(input).willing);

	input = eligibleInput();
	input.skillRank = 2;
	input.negotiator = true;
	input.commonCause = true;
	input.creatureArmyValue = 520;
	const auto commonCauseBoundary = resolveForecast(input);
	EXPECT_EQ(commonCauseBoundary.thresholdPercent, 65);
	EXPECT_TRUE(commonCauseBoundary.commonCause);
	EXPECT_TRUE(commonCauseBoundary.willing)
		<< "Common Cause compares exactly half the raw stack value at the 65% boundary";
	input.creatureArmyValue = 521;
	EXPECT_FALSE(resolveForecast(input).willing)
		<< "An odd raw value above the half-value boundary must not be truncated down";

	input = eligibleInput();
	input.skillRank = 3;
	input.negotiator = true;
	input.grandDiplomat = true;
	input.creatureArmyValue = 400;
	const auto grandDiplomatBoundary = resolveForecast(input);
	EXPECT_EQ(grandDiplomatBoundary.thresholdPercent, 100);
	EXPECT_TRUE(grandDiplomatBoundary.grandDiplomat);
	EXPECT_TRUE(grandDiplomatBoundary.willing);
	input.creatureArmyValue = 401;
	EXPECT_FALSE(resolveForecast(input).willing)
		<< "Grand Diplomat caps the threshold at 100% of hero Army Value";
}

TEST(NewHorizonsDiplomacy, KeepsEligibilityAndAuthoredFreeExceptionsSeparateFromRank)
{
	auto input = eligibleInput();
	input.skillRank = 0;
	input.heroArmyValue = 1;
	input.creatureArmyValue = 100000;
	input.authoredFree = true;
	const auto authoredFree = resolveForecast(input);
	EXPECT_FALSE(authoredFree.active);
	EXPECT_TRUE(authoredFree.eligible);
	EXPECT_TRUE(authoredFree.willing)
		<< "A map-authored compliant free join is an exception to the Diplomacy threshold";
	EXPECT_TRUE(authoredFree.authoredFree);
	EXPECT_EQ(authoredFree.thresholdPercent, 0);
	EXPECT_EQ(authoredFree.normalGoldCost, 150);

	input.authoredFree = false;
	EXPECT_FALSE(resolveForecast(input).willing)
		<< "Rank zero does not gain a paid join through Diplomacy";

	input = eligibleInput();
	input.encounterEligible = false;
	const auto excluded = resolveForecast(input);
	EXPECT_FALSE(excluded.eligible);
	EXPECT_FALSE(excluded.willing);

	input = eligibleInput();
	input.usesNewHorizonsRules = false;
	const auto legacy = resolveForecast(input);
	EXPECT_FALSE(legacy.usesNewHorizonsRules);
	EXPECT_FALSE(legacy.active);
	EXPECT_FALSE(legacy.willing)
		<< "A hero without captured New Horizons rules remains on the legacy path";
}

TEST(NewHorizonsDiplomacy, RejectsUnrepresentableFullStackGoldCosts)
{
	auto input = eligibleInput();
	input.goldCostPerCreature = std::numeric_limits<int32_t>::max();
	input.joiningAmount = 2;
	const auto outOfActionRange = resolveForecast(input);
	EXPECT_TRUE(outOfActionRange.normalGoldCostValid);
	EXPECT_FALSE(outOfActionRange.normalGoldCostFitsAction);
	EXPECT_EQ(outOfActionRange.normalGoldCost,
		static_cast<int64_t>(std::numeric_limits<int32_t>::max()) * 2);
	EXPECT_FALSE(outOfActionRange.willing)
		<< "An existing int-valued join action must not overflow on an unaffordable offer";

	input.goldCostPerCreature = std::numeric_limits<int64_t>::max();
	const auto overflowing = resolveForecast(input);
	EXPECT_FALSE(overflowing.normalGoldCostValid);
	EXPECT_FALSE(overflowing.willing);
}

TEST(NewHorizonsDiplomacy, UsesTheCapturedPerkRegistryForRulesetGating)
{
	JsonNode capturedRules;
	capturedRules["skills"][newHorizonsDiplomacy::SKILL_ID]
		["ranks"]["basic"]["effect"]["status"].String() = "active";
	EXPECT_TRUE(newHorizonsDiplomacy::usesNewHorizonsRules(capturedRules));

	capturedRules["skills"][newHorizonsDiplomacy::SKILL_ID]
		["ranks"]["basic"]["effect"]["status"].String() = "planned";
	EXPECT_FALSE(newHorizonsDiplomacy::usesNewHorizonsRules(capturedRules));
	EXPECT_FALSE(newHorizonsDiplomacy::usesNewHorizonsRules(JsonNode()));
}

TEST_F(NewHorizonsDiplomacyLegacyGateTest, MapWithoutCapturedNewHorizonsPerksStaysOnTheLegacyPath)
{
	startGame(16, 2, CGCreature::Character::HOSTILE);
	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	EXPECT_FALSE(forecast.usesNewHorizonsRules);
	EXPECT_FALSE(forecast.active);
	EXPECT_FALSE(forecast.willing);
}

TEST_F(NewHorizonsDiplomacyTest, PaidOfferUsesSelectedAdvancedDiplomacyPerksAndSafeAdmission)
{
	const auto pikeman = creature("core:pikeman");
	startGame(16, 20, CGCreature::Character::HOSTILE);
	advanceToDiplomacyRank(MasteryLevel::ADVANCED,
		newHorizonsDiplomacy::NEGOTIATOR_ID, newHorizonsDiplomacy::COMMON_CAUSE_ID);
	ASSERT_TRUE(hero->hasActivePerk(DIPLOMACY_SKILL, newHorizonsDiplomacy::NEGOTIATOR_ID));
	ASSERT_TRUE(hero->hasActivePerk(DIPLOMACY_SKILL, newHorizonsDiplomacy::COMMON_CAUSE_ID));
	ASSERT_EQ(hero->getPerkSkillRank(DIPLOMACY_SKILL), MasteryLevel::ADVANCED);

	// HOSTILE is a disposition, not the explicit map-authored Diplomacy opt-out.
	// A willing New Horizons offer must take precedence over legacy aggression.
	neutral->agression = 10;
	auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.usesNewHorizonsRules);
	ASSERT_TRUE(forecast.active);
	ASSERT_TRUE(forecast.eligible);
	ASSERT_TRUE(forecast.willing);
	ASSERT_TRUE(forecast.negotiator);
	ASSERT_TRUE(forecast.commonCause);
	ASSERT_EQ(forecast.skillRank, MasteryLevel::ADVANCED);
	ASSERT_EQ(forecast.thresholdPercent, 65);
	ASSERT_EQ(forecast.joiningAmount, 20);
	ASSERT_TRUE(forecast.normalGoldCostValid);
	ASSERT_TRUE(forecast.normalGoldCostFitsAction);
	EXPECT_EQ(forecast.normalGoldCost,
		static_cast<int64_t>(pikeman.toCreature()->getRecruitCost(EGameResID::GOLD)) * 20);
	const auto pikemanCapacity = hero->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(pikemanCapacity.has_value());
	ASSERT_EQ(pikemanCapacity->maximum, 17);

	const auto neutralId = neutral->id;
	grantResources(PLAYER, GameResID(EGameResID::GOLD), 10000);
	const auto fundedGold = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	ASSERT_GT(fundedGold, forecast.normalGoldCost);

	gameHandler->objectVisited(neutral, hero);
	ASSERT_EQ(server->blockingDialogs.size(), 1u);
	const auto & offer = server->blockingDialogs.back();
	ASSERT_EQ(offer.components.size(), 2u);
	EXPECT_EQ(offer.components[0].type, ComponentType::CREATURE);
	EXPECT_EQ(offer.components[0].subType.as<CreatureID>(), pikeman);
	ASSERT_TRUE(offer.components[0].value.has_value());
	EXPECT_EQ(*offer.components[0].value, 20);
	EXPECT_EQ(offer.components[1].type, ComponentType::RESOURCE);
	EXPECT_EQ(offer.components[1].subType.as<GameResID>(), GameResID(EGameResID::GOLD));
	ASSERT_TRUE(offer.components[1].value.has_value());
	EXPECT_EQ(*offer.components[1].value, forecast.normalGoldCost);
	const auto offerText = offer.text.toString(LIBRARY->generaltexth.get());
	EXPECT_NE(offerText.find("dismissed permanently"), std::string::npos);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 16);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 20);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], fundedGold)
		<< "Visiting presents the offer; payment waits for acceptance";

	ASSERT_NE(offer.queryID, QueryID::NONE);
	ASSERT_TRUE(gameHandler->queryReply(offer.queryID, 1, PLAYER));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 17)
		<< "Only the one Pikeman that fits the existing Leadership slot is transferred";
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 19)
		<< "The full-stack offer is admitted through the normal partial-transfer path";
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD],
		fundedGold - forecast.normalGoldCost);
	ASSERT_EQ(server->garrisonDialogs.size(), 1u);
	EXPECT_FALSE(std::any_of(server->systemMessageTexts.begin(), server->systemMessageTexts.end(),
		[](const std::string & text)
		{
			return text.find("Leadership limit exceeded") != std::string::npos;
		})) << "A valid Leadership-limited join must not become a server-side rejection";

	ASSERT_NE(server->garrisonDialogs.back().queryID, QueryID::NONE);
	ASSERT_TRUE(gameHandler->queryReply(server->garrisonDialogs.back().queryID, 0, PLAYER));
	EXPECT_EQ(gameState()->getObjInstance(neutralId), nullptr)
		<< "The creatures left after closing the transfer window are dismissed";
}

TEST_F(NewHorizonsDiplomacyTest, ExpertGrandDiplomatIsSelectedThroughNormalOffers)
{
	startGame(16, 10, CGCreature::Character::HOSTILE);
	advanceToDiplomacyRank(MasteryLevel::EXPERT,
		newHorizonsDiplomacy::NEGOTIATOR_ID,
		newHorizonsDiplomacy::COMMON_CAUSE_ID,
		newHorizonsDiplomacy::GRAND_DIPLOMAT_ID);

	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	EXPECT_TRUE(forecast.usesNewHorizonsRules);
	EXPECT_TRUE(forecast.active);
	EXPECT_TRUE(forecast.negotiator);
	EXPECT_TRUE(forecast.commonCause);
	EXPECT_TRUE(forecast.grandDiplomat);
	EXPECT_EQ(forecast.skillRank, MasteryLevel::EXPERT);
	EXPECT_EQ(forecast.thresholdPercent, 100);
}

TEST_F(NewHorizonsDiplomacyTest, RankZeroCanAcceptOnlyAnAuthoredCompliantFreeStack)
{
	const auto pikeman = creature("core:pikeman");
	startGame(1, 2, CGCreature::Character::COMPLIANT);
	ASSERT_EQ(neutral->tempOwner, PlayerColor::UNFLAGGABLE)
		<< "A standard map-created wandering stack keeps the unflagged neutral-owner sentinel";
	ASSERT_EQ(hero->getPerkSkillRank(DIPLOMACY_SKILL), MasteryLevel::NONE);
	ASSERT_FALSE(hero->hasActivePerk(DIPLOMACY_SKILL, newHorizonsDiplomacy::NEGOTIATOR_ID));

	const auto mapOwner = neutral->tempOwner;
	neutral->tempOwner = PLAYER;
	const auto playerOwned = neutral->getNewHorizonsDiplomacyForecast(*hero);
	EXPECT_FALSE(playerOwned.eligible);
	EXPECT_FALSE(playerOwned.willing);
	neutral->tempOwner = mapOwner;

	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.usesNewHorizonsRules);
	ASSERT_FALSE(forecast.active);
	ASSERT_TRUE(forecast.eligible);
	ASSERT_TRUE(forecast.authoredFree);
	ASSERT_TRUE(forecast.willing);
	ASSERT_EQ(forecast.skillRank, 0);
	ASSERT_EQ(forecast.joiningAmount, 2);
	const auto goldBefore = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];

	const auto neutralId = neutral->id;
	gameHandler->objectVisited(neutral, hero);
	ASSERT_EQ(server->blockingDialogs.size(), 1u);
	const auto & offer = server->blockingDialogs.back();
	ASSERT_EQ(offer.components.size(), 1u);
	EXPECT_EQ(offer.components.front().type, ComponentType::CREATURE);
	EXPECT_EQ(offer.components.front().subType.as<CreatureID>(), pikeman);
	ASSERT_TRUE(offer.components.front().value.has_value());
	EXPECT_EQ(*offer.components.front().value, 2);
	EXPECT_NE(offer.text.toString(LIBRARY->generaltexth.get()).find("dismissed permanently"), std::string::npos);

	ASSERT_NE(offer.queryID, QueryID::NONE);
	ASSERT_TRUE(gameHandler->queryReply(offer.queryID, 1, PLAYER));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 3);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], goldBefore);
	EXPECT_EQ(gameState()->getObjInstance(neutralId), nullptr);
}

TEST_F(NewHorizonsDiplomacyTest, DecliningAnOfferCanStillResolveTheFollowupFleeChoice)
{
	startGame(16, 2, CGCreature::Character::HOSTILE);
	advanceToDiplomacyRank(MasteryLevel::BASIC, newHorizonsDiplomacy::NEGOTIATOR_ID);
	neutral->agression = 0;
	const auto neutralId = neutral->id;
	const auto goldBefore = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];

	gameHandler->objectVisited(neutral, hero);
	ASSERT_EQ(server->blockingDialogs.size(), 1u);
	ASSERT_NE(server->blockingDialogs.back().queryID, QueryID::NONE);
	ASSERT_TRUE(gameHandler->queryReply(server->blockingDialogs.back().queryID, 0, PLAYER));

	ASSERT_EQ(server->blockingDialogs.size(), 2u)
		<< "Refusing the recruitment offer should ask whether to pursue the fleeing stack";
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], goldBefore);
	ASSERT_NE(server->blockingDialogs.back().queryID, QueryID::NONE);
	ASSERT_TRUE(gameHandler->queryReply(server->blockingDialogs.back().queryID, 0, PLAYER));
	EXPECT_EQ(gameState()->getObjInstance(neutralId), nullptr)
		<< "Choosing not to pursue must resolve the follow-up flee query, not reopen the expired offer";
}

TEST_F(NewHorizonsDiplomacyTest, ExplicitMapOptOutAndEligibilitySerializationRemainDistinct)
{
	startGame(16, 2, CGCreature::Character::HOSTILE);
	advanceToDiplomacyRank(MasteryLevel::BASIC, newHorizonsDiplomacy::NEGOTIATOR_ID);
	neutral->agression = 0;
	neutral->diplomacyEligible = false;

	const auto excluded = neutral->getNewHorizonsDiplomacyForecast(*hero);
	EXPECT_TRUE(excluded.usesNewHorizonsRules);
	EXPECT_TRUE(excluded.active);
	EXPECT_FALSE(excluded.eligible);
	EXPECT_FALSE(excluded.willing);

	JsonNode saved;
	JsonSerializer jsonWriter(nullptr, saved);
	neutral->CGObjectInstance::serializeJson(jsonWriter);
	ASSERT_TRUE(saved["options"]["diplomacyEligible"].isBool());
	EXPECT_FALSE(saved["options"]["diplomacyEligible"].Bool());

	CGCreature jsonCopy(gameState().get());
	JsonDeserializer jsonReader(nullptr, saved);
	jsonCopy.CGObjectInstance::serializeJson(jsonReader);
	EXPECT_FALSE(jsonCopy.diplomacyEligible);
	EXPECT_EQ(jsonCopy.getStackCount(SlotID(0)), 2);

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	current.oser & *neutral;
	CGCreature currentCopy(gameState().get());
	current.iser.cb = gameState().get();
	current.iser & currentCopy;
	EXPECT_FALSE(currentCopy.diplomacyEligible);

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_NECROMANCY_LORD_OF_DEAD;
	EXPECT_THROW(oldWriter.oser & *neutral, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty())
		<< "The unsupported explicit opt-out must be rejected before writing any object bytes";

	neutral->diplomacyEligible = true;
	CMemorySerializer oldSnapshot;
	oldSnapshot.oser.version = ESerializationVersion::NEW_HORIZONS_NECROMANCY_LORD_OF_DEAD;
	oldSnapshot.iser.version = ESerializationVersion::NEW_HORIZONS_NECROMANCY_LORD_OF_DEAD;
	oldSnapshot.oser & *neutral;
	CGCreature legacyCopy(gameState().get());
	legacyCopy.diplomacyEligible = false;
	oldSnapshot.iser.cb = gameState().get();
	oldSnapshot.iser & legacyCopy;
	EXPECT_TRUE(legacyCopy.diplomacyEligible)
		<< "An older map snapshot without the field must retain the default-eligible behavior";
}
