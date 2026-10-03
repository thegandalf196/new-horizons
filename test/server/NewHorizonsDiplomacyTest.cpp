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
#include <optional>
#include <string>
#include <string_view>

#include "../../lib/GameConstants.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/bonuses/BonusCustomTypes.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/NewHorizonsDiplomacy.h"
#include "../../lib/entities/creature/NewHorizonsMusterRules.h"
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
constexpr auto ENVOY_PERK_ID = "new-horizons:diplomacy.envoy";
constexpr auto PEACEMAKER_PERK_ID = "new-horizons:diplomacy.peacemaker";
constexpr auto TRIBUTE_PERK_ID = "new-horizons:diplomacy.tribute";

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

	void startGame(int32_t heroPikemen, int32_t neutralPikemen, CGCreature::Character character,
		int3 neutralPosition = {12, 12, 0}, bool addSecondHero = false)
	{
		const auto pikeman = creature("core:pikeman");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({5, 5, 0}, heroType("core:christian"), PLAYER)
			.heroGarrison({{pikeman, heroPikemen}})
			.monster(neutralPosition, pikeman, static_cast<uint16_t>(neutralPikemen),
				static_cast<int8_t>(character));
		if(addSecondHero)
			builder.hero({24, 24, 0}, heroType("core:adela"), PLAYER);
		startWithMap(std::move(builder));

		hero = findHeroAt({5, 5, 0});
		otherHero = addSecondHero ? findHeroAt({24, 24, 0}) : nullptr;
		neutral = findFirst<CGCreature>();
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(neutral, nullptr);
		if(addSecondHero)
			ASSERT_NE(otherHero, nullptr);

		server = std::make_unique<DiplomacyRecordingServer>(gameState(), PLAYER);
		gameHandler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
	}

	void revealNeutralForPlayer()
	{
		FoWChange reveal;
		reveal.player = PLAYER;
		reveal.mode = ETileVisibility::REVEALED;
		for(int y = 0; y < neutral->getHeight(); ++y)
			for(int x = 0; x < neutral->getWidth(); ++x)
				reveal.tiles.insert(neutral->anchorPos() + int3(-x, -y, 0));
		gameHandler->sendAndApply(reveal);
		ASSERT_TRUE(neutral->isVisibleFor(PLAYER));
	}

	std::string neutralPopupText() const
	{
		return neutral->getPopupText(hero).toString(LIBRARY->generaltexth.get());
	}

	void selectBasicEnvoy()
	{
		advanceToDiplomacyRank(MasteryLevel::BASIC, ENVOY_PERK_ID);
		ASSERT_TRUE(hero->hasActivePerk(DIPLOMACY_SKILL, ENVOY_PERK_ID));
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

	int32_t currentAbsoluteWeek() const
	{
		const auto calendar = gameState()->getCalendar();
		return newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	}

	std::optional<int3> neutralGuardApproachFor(const CGHeroInstance * movingHero) const
	{
		for(int dx = -1; dx <= 1; ++dx)
			for(int dy = -1; dy <= 1; ++dy)
			{
				if(dx == 0 && dy == 0)
					continue;
				const int3 destination = movingHero->pos + int3(dx, dy, 0);
				if(!map()->isInTheMap(destination))
					continue;
				const int3 visitableTile = movingHero->convertToVisitablePos(destination);
				if(map()->isInTheMap(visitableTile) && visitableTile != neutral->visitablePos()
					&& map()->guardingCreaturePosition(visitableTile) == neutral->visitablePos())
					return destination;
			}
		return std::nullopt;
	}

	CGHeroInstance * hero = nullptr;
	CGHeroInstance * otherHero = nullptr;
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
	startGame(16, 2, CGCreature::Character::HOSTILE, {9, 5, 0});
	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	EXPECT_FALSE(forecast.usesNewHorizonsRules);
	EXPECT_FALSE(forecast.active);
	EXPECT_FALSE(forecast.willing);
	revealNeutralForPlayer();
	const auto popup = neutralPopupText();
	EXPECT_EQ(popup.find("Envoy:"), std::string::npos);
	EXPECT_EQ(popup.find("Gold required if it joins:"), std::string::npos);
}

TEST_F(NewHorizonsDiplomacyTest, UnselectedEnvoyDoesNotChangeTheVisibleNeutralPopup)
{
	startGame(16, 2, CGCreature::Character::HOSTILE, {9, 5, 0});
	advanceToDiplomacyRank(MasteryLevel::BASIC);
	ASSERT_FALSE(hero->hasActivePerk(DIPLOMACY_SKILL, ENVOY_PERK_ID));
	revealNeutralForPlayer();
	EXPECT_EQ(neutral->anchorPos().dist2dSQ(hero->visitablePos()), 25u);

	const auto popup = neutralPopupText();
	EXPECT_EQ(popup.find("Envoy:"), std::string::npos);
	EXPECT_EQ(popup.find("Gold required if it joins:"), std::string::npos);
	EXPECT_EQ(popup.find("Diplomacy threshold:"), std::string::npos);
}

TEST_F(NewHorizonsDiplomacyTest, EnvoyReportsWillingPaidStackAtInclusiveFiveTileRangeWithoutChangingVisibility)
{
	startGame(16, 2, CGCreature::Character::HOSTILE, {9, 5, 0});
	selectBasicEnvoy();
	ASSERT_EQ(neutral->anchorPos().dist2dSQ(hero->visitablePos()), 25u);
	const int3 unrelatedHiddenTile{30, 30, 0};
	const int originalSightRadius = hero->getSightRadius();
	ASSERT_GT(originalSightRadius, 0);
	GiveBonus sightPenalty;
	sightPenalty.id = hero->id;
	sightPenalty.bonus.type = BonusType::SIGHT_RADIUS;
	sightPenalty.bonus.valType = BonusValueType::ADDITIVE_VALUE;
	sightPenalty.bonus.val = -originalSightRadius;
	gameHandler->sendAndApply(sightPenalty);
	ASSERT_EQ(hero->getSightRadius(), 0);

	FoWChange hide;
	hide.player = PLAYER;
	hide.mode = ETileVisibility::HIDDEN;
	for(int y = 0; y < neutral->getHeight(); ++y)
		for(int x = 0; x < neutral->getWidth(); ++x)
			hide.tiles.insert(neutral->anchorPos() + int3(-x, -y, 0));
	gameHandler->sendAndApply(hide);
	ASSERT_FALSE(gameState()->isVisibleFor(neutral, PLAYER));
	ASSERT_FALSE(gameState()->isVisibleFor(unrelatedHiddenTile, PLAYER));
	const auto hiddenPopup = neutralPopupText();
	EXPECT_EQ(hiddenPopup.find("Envoy:"), std::string::npos);
	EXPECT_EQ(hiddenPopup.find("Gold required if it joins:"), std::string::npos);
	EXPECT_FALSE(gameState()->isVisibleFor(neutral, PLAYER));
	EXPECT_FALSE(gameState()->isVisibleFor(unrelatedHiddenTile, PLAYER));

	revealNeutralForPlayer();
	ASSERT_TRUE(gameState()->isVisibleFor(neutral, PLAYER));
	ASSERT_FALSE(hero->hasVisions(neutral, BonusCustomSubtype::visionsMonsters));

	const auto normalHover = neutral->getHoverText(hero).toString(LIBRARY->generaltexth.get());
	EXPECT_EQ(normalHover, neutral->getHoverText(PLAYER).toString(LIBRARY->generaltexth.get()));
	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.willing);
	const auto popup = neutralPopupText();
	EXPECT_NE(popup.find("Envoy: willing to negotiate."), std::string::npos);
	EXPECT_NE(popup.find("Diplomacy threshold: up to 25% of your current Army Value."), std::string::npos);
	EXPECT_NE(popup.find("Gold required if it joins: " + std::to_string(forecast.normalGoldCost) + " Gold."),
		std::string::npos);
	EXPECT_EQ(neutral->getHoverText(hero).toString(LIBRARY->generaltexth.get()), normalHover)
		<< "Envoy reports in the detail popup, not the one-line hover/status text";
	EXPECT_FALSE(hero->hasVisions(neutral, BonusCustomSubtype::visionsMonsters));
	EXPECT_TRUE(gameState()->isVisibleFor(neutral, PLAYER));
	EXPECT_FALSE(gameState()->isVisibleFor(unrelatedHiddenTile, PLAYER))
		<< "Reading an Envoy report must not grant map visibility";
}

TEST_F(NewHorizonsDiplomacyTest, EnvoyReportsAuthoredFreeJoinWithoutThresholdOrGold)
{
	startGame(1, 2, CGCreature::Character::COMPLIANT, {9, 5, 0});
	selectBasicEnvoy();
	revealNeutralForPlayer();
	ASSERT_EQ(neutral->anchorPos().dist2dSQ(hero->visitablePos()), 25u);
	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.authoredFree);
	ASSERT_TRUE(forecast.willing);

	const auto popup = neutralPopupText();
	EXPECT_NE(popup.find("Envoy: willing to negotiate."), std::string::npos);
	EXPECT_NE(popup.find("Gold required if it joins: 0 Gold."), std::string::npos);
	EXPECT_EQ(popup.find("Diplomacy threshold:"), std::string::npos);
	EXPECT_FALSE(hero->hasVisions(neutral, BonusCustomSubtype::visionsMonsters));
}

TEST_F(NewHorizonsDiplomacyTest, EnvoyReportsThresholdFailureAndGoldAtFiveTiles)
{
	startGame(16, 20, CGCreature::Character::HOSTILE, {9, 5, 0});
	selectBasicEnvoy();
	revealNeutralForPlayer();
	ASSERT_EQ(neutral->anchorPos().dist2dSQ(hero->visitablePos()), 25u);
	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_FALSE(forecast.willing);

	const auto popup = neutralPopupText();
	EXPECT_NE(popup.find("Envoy: not willing to negotiate."), std::string::npos);
	EXPECT_NE(popup.find("Diplomacy threshold: up to 25% of your current Army Value."),
		std::string::npos);
	EXPECT_NE(popup.find("Gold required if it joins: " + std::to_string(forecast.normalGoldCost) + " Gold."),
		std::string::npos);
}

TEST_F(NewHorizonsDiplomacyTest, EnvoyDoesNotReportBeyondFiveTilesOrAtSquaredDistanceTwentySix)
{
	startGame(16, 2, CGCreature::Character::HOSTILE, {10, 5, 0});
	selectBasicEnvoy();
	revealNeutralForPlayer();
	ASSERT_EQ(neutral->anchorPos().dist2dSQ(hero->visitablePos()), 36u);
	const auto sixTilePopup = neutralPopupText();
	EXPECT_EQ(sixTilePopup.find("Envoy:"), std::string::npos);
	EXPECT_EQ(sixTilePopup.find("Gold required if it joins:"), std::string::npos);

	// This is a read-only popup query; relocating the fixture stack lets the
	// same selected Envoy exercise the diagonal squared-distance boundary.
	neutral->setAnchorPos(hero->visitablePos() + int3(5, 1, 0));
	revealNeutralForPlayer();
	ASSERT_EQ(neutral->anchorPos().dist2dSQ(hero->visitablePos()), 26u);
	const auto diagonalPopup = neutralPopupText();
	EXPECT_EQ(diagonalPopup.find("Envoy:"), std::string::npos);
	EXPECT_EQ(diagonalPopup.find("Gold required if it joins:"), std::string::npos);
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

TEST_F(NewHorizonsDiplomacyTest, PeacemakerProtectsOnlyItsHeroAndExpiresAtTheNextWeek)
{
	startGame(1, 20, CGCreature::Character::HOSTILE, {6, 4, 0}, true);
	advanceToDiplomacyRank(MasteryLevel::ADVANCED, PEACEMAKER_PERK_ID, TRIBUTE_PERK_ID);
	neutral->agression = 10;
	neutral->neverFlees = true;

	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.active);
	ASSERT_TRUE(forecast.eligible);
	ASSERT_FALSE(forecast.willing);
	ASSERT_TRUE(forecast.normalGoldCostValid);
	const auto goldAtStart = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	if(goldAtStart < forecast.normalGoldCost)
		grantResources(PLAYER, GameResID(EGameResID::GOLD),
			static_cast<int>(forecast.normalGoldCost - goldAtStart));
	ASSERT_TRUE(hero->hasActivePerk(DIPLOMACY_SKILL, PEACEMAKER_PERK_ID));
	ASSERT_TRUE(hero->hasActivePerk(DIPLOMACY_SKILL, TRIBUTE_PERK_ID));
	ASSERT_FALSE(otherHero->hasActivePerk(DIPLOMACY_SKILL, PEACEMAKER_PERK_ID));

	const auto neutralId = neutral->id;
	const auto week = currentAbsoluteWeek();
	const auto goldBefore = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	ASSERT_GE(goldBefore, forecast.normalGoldCost)
		<< "The active Tribute perk must be affordable so Peacemaker precedence is exercised";
	const auto destination = neutralGuardApproachFor(hero);
	ASSERT_TRUE(destination.has_value()) << "Fixture must move into a tile guarded by the neutral stack";
	ASSERT_TRUE(hero->pos.areNeighbours(*destination));
	ASSERT_EQ(map()->guardingCreaturePosition(hero->convertToVisitablePos(*destination)),
		neutral->visitablePos());

	gameHandler->setMovePoints(hero->id, 20000);
	ASSERT_TRUE(gameHandler->moveHero(hero->id, *destination, EMovementMode::STANDARD, false,
		PLAYER, EPathfindingLayer::LAND));
	EXPECT_EQ(hero->pos, *destination);
	EXPECT_EQ(gameState()->getObjInstance(neutralId), neutral);
	EXPECT_EQ(gameState()->getBattle(PLAYER), nullptr)
		<< "The first qualifying hostile encounter passes without starting combat";
	EXPECT_TRUE(hero->isNewHorizonsCreaturePacified(neutralId, week));
	EXPECT_TRUE(neutral->passableFor(hero));
	EXPECT_FALSE(neutral->passableFor(otherHero))
		<< "The protected stack remains a guard for a different hero of the same player";
	EXPECT_EQ(hero->getNewHorizonsPeacemakerLastWeek(), week);
	EXPECT_EQ(hero->getNewHorizonsPacifiedCreatureId(), neutralId);
	EXPECT_EQ(hero->getNewHorizonsTributeLastWeek(), -1);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], goldBefore);

	const auto currentCalendar = gameState()->getCalendar();
	gameState()->day = (week + 1) * currentCalendar.getDaysInWeek() + 1;
	const auto nextWeek = currentAbsoluteWeek();
	ASSERT_NE(nextWeek, week);
	EXPECT_FALSE(hero->isNewHorizonsCreaturePacified(neutralId, nextWeek));
	EXPECT_FALSE(neutral->passableFor(hero))
		<< "Protection is derived from the current absolute week and expires without a reset event";
	EXPECT_TRUE(hero->hasUsedNewHorizonsPeacemaker(week));
	EXPECT_FALSE(hero->hasUsedNewHorizonsPeacemaker(nextWeek));
}

TEST_F(NewHorizonsDiplomacyTest, DeliberateAttackClearsPeacemakerTargetButKeepsWeeklyUse)
{
	startGame(1, 20, CGCreature::Character::HOSTILE, {6, 4, 0});
	advanceToDiplomacyRank(MasteryLevel::BASIC, PEACEMAKER_PERK_ID);
	neutral->agression = 10;
	neutral->neverFlees = true;
	const auto neutralId = neutral->id;
	const auto week = currentAbsoluteWeek();

	const auto destination = neutralGuardApproachFor(hero);
	ASSERT_TRUE(destination.has_value());
	gameHandler->setMovePoints(hero->id, 20000);
	ASSERT_TRUE(gameHandler->moveHero(hero->id, *destination, EMovementMode::STANDARD, false,
		PLAYER, EPathfindingLayer::LAND));
	ASSERT_TRUE(hero->isNewHorizonsCreaturePacified(neutralId, week));

	gameHandler->objectVisited(neutral, hero);
	EXPECT_EQ(hero->getNewHorizonsPacifiedCreatureId(), ObjectInstanceID::NONE);
	EXPECT_EQ(hero->getNewHorizonsPeacemakerLastWeek(), week);
	EXPECT_TRUE(hero->hasUsedNewHorizonsPeacemaker(week))
		<< "Choosing a deliberate attack clears passage but does not refund this week's use";
	EXPECT_NE(gameState()->getBattle(PLAYER), nullptr);
}

TEST_F(NewHorizonsDiplomacyTest, TributePaysAndRemovesAnUnwillingStackOnTheMovementPath)
{
	startGame(1, 2, CGCreature::Character::HOSTILE, {6, 4, 0});
	advanceToDiplomacyRank(MasteryLevel::ADVANCED,
		newHorizonsDiplomacy::NEGOTIATOR_ID, TRIBUTE_PERK_ID);
	neutral->agression = 10;
	neutral->neverFlees = true;

	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(hero->hasActivePerk(DIPLOMACY_SKILL, TRIBUTE_PERK_ID));
	ASSERT_TRUE(forecast.eligible);
	ASSERT_FALSE(forecast.authoredFree);
	ASSERT_FALSE(forecast.willing);
	ASSERT_TRUE(forecast.normalGoldCostValid);
	ASSERT_TRUE(forecast.normalGoldCostFitsAction);
	const auto neutralId = neutral->id;
	const auto week = currentAbsoluteWeek();
	const auto goldBefore = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	if(goldBefore < forecast.normalGoldCost)
		grantResources(PLAYER, GameResID(EGameResID::GOLD),
			static_cast<int>(forecast.normalGoldCost - goldBefore));
	const auto fundedGold = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	ASSERT_GE(fundedGold, forecast.normalGoldCost);
	const auto destination = neutralGuardApproachFor(hero);
	ASSERT_TRUE(destination.has_value());

	gameHandler->setMovePoints(hero->id, 20000);
	ASSERT_TRUE(gameHandler->moveHero(hero->id, *destination, EMovementMode::STANDARD, false,
		PLAYER, EPathfindingLayer::LAND));
	EXPECT_EQ(hero->pos, *destination)
		<< "Movement must complete after Tribute removes the guarding object during its callback";
	EXPECT_EQ(gameState()->getObjInstance(neutralId), nullptr);
	EXPECT_EQ(gameState()->getBattle(PLAYER), nullptr);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD],
		fundedGold - forecast.normalGoldCost);
	EXPECT_EQ(hero->getNewHorizonsTributeLastWeek(), week);
	EXPECT_TRUE(hero->hasUsedNewHorizonsTribute(week));
	EXPECT_EQ(hero->getNewHorizonsPacifiedCreatureId(), ObjectInstanceID::NONE);
	EXPECT_TRUE(server->blockingDialogs.empty())
		<< "An unwilling stack is handled by automatic Tribute, not a recruitment offer";
}

TEST_F(NewHorizonsDiplomacyTest, InsufficientTributeGoldFallsThroughToCombatWithoutUsingTheQuota)
{
	startGame(1, 2, CGCreature::Character::HOSTILE, {6, 4, 0});
	advanceToDiplomacyRank(MasteryLevel::ADVANCED,
		newHorizonsDiplomacy::NEGOTIATOR_ID, TRIBUTE_PERK_ID);
	neutral->agression = 10;
	neutral->neverFlees = true;

	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.eligible);
	ASSERT_FALSE(forecast.willing);
	ASSERT_GT(forecast.normalGoldCost, 0);
	const auto neutralId = neutral->id;
	const auto affordableGold = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	grantResources(PLAYER, GameResID(EGameResID::GOLD),
		static_cast<int>(forecast.normalGoldCost - 1 - affordableGold));
	const auto goldBefore = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	ASSERT_EQ(goldBefore, forecast.normalGoldCost - 1);
	const auto destination = neutralGuardApproachFor(hero);
	ASSERT_TRUE(destination.has_value());

	gameHandler->setMovePoints(hero->id, 20000);
	ASSERT_TRUE(gameHandler->moveHero(hero->id, *destination, EMovementMode::STANDARD, false,
		PLAYER, EPathfindingLayer::LAND));
	EXPECT_EQ(gameState()->getObjInstance(neutralId), neutral);
	EXPECT_EQ(gameState()->getBattle(PLAYER) != nullptr, true)
		<< "An unaffordable Tribute attempt must retain the ordinary hostile battle path";
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], goldBefore);
	EXPECT_EQ(hero->getNewHorizonsTributeLastWeek(), -1);
	EXPECT_FALSE(hero->hasUsedNewHorizonsTribute(currentAbsoluteWeek()));
	EXPECT_EQ(hero->getNewHorizonsPeacemakerLastWeek(), -1);
}

TEST_F(NewHorizonsDiplomacyTest, WillingPaidOfferDoesNotConsumeTribute)
{
	startGame(4, 1, CGCreature::Character::HOSTILE, {6, 4, 0});
	advanceToDiplomacyRank(MasteryLevel::ADVANCED,
		newHorizonsDiplomacy::NEGOTIATOR_ID, TRIBUTE_PERK_ID);
	neutral->agression = 10;
	neutral->neverFlees = true;

	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.eligible);
	ASSERT_TRUE(forecast.willing);
	ASSERT_FALSE(forecast.authoredFree);
	const auto neutralId = neutral->id;
	const auto week = currentAbsoluteWeek();
	ASSERT_TRUE(hero->hasActivePerk(DIPLOMACY_SKILL, TRIBUTE_PERK_ID));
	const auto goldBefore = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	if(goldBefore < forecast.normalGoldCost)
		grantResources(PLAYER, GameResID(EGameResID::GOLD),
			static_cast<int>(forecast.normalGoldCost - goldBefore));
	const auto fundedGold = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	const auto destination = neutralGuardApproachFor(hero);
	ASSERT_TRUE(destination.has_value());

	gameHandler->setMovePoints(hero->id, 20000);
	ASSERT_TRUE(gameHandler->moveHero(hero->id, *destination, EMovementMode::STANDARD, false,
		PLAYER, EPathfindingLayer::LAND));
	ASSERT_EQ(server->blockingDialogs.size(), 1u)
		<< "A willing stack should retain the ordinary paid recruitment offer";
	EXPECT_EQ(hero->getNewHorizonsTributeLastWeek(), -1);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], fundedGold);
	ASSERT_NE(server->blockingDialogs.back().queryID, QueryID::NONE);
	ASSERT_TRUE(gameHandler->queryReply(server->blockingDialogs.back().queryID, 1, PLAYER));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 5);
	EXPECT_EQ(gameState()->getObjInstance(neutralId), nullptr);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD],
		fundedGold - forecast.normalGoldCost);
	EXPECT_EQ(hero->getNewHorizonsTributeLastWeek(), -1);
	EXPECT_FALSE(hero->hasUsedNewHorizonsTribute(week))
		<< "Accepting a willing ordinary offer is not the automatic Tribute mechanic";
}

TEST_F(NewHorizonsDiplomacyTest, WeeklyPeacemakerAndTributeStateIsVersionedAndValidated)
{
	startGame(1, 1, CGCreature::Character::COMPLIANT);
	const auto neutralId = neutral->id;

	hero->setNewHorizonsDiplomacyState(4, neutralId, 4);
	CMemorySerializer currentHero;
	currentHero.oser.version = ESerializationVersion::CURRENT;
	currentHero.iser.version = ESerializationVersion::CURRENT;
	currentHero.oser & *hero;
	CGHeroInstance currentHeroCopy(gameState().get());
	currentHero.iser.cb = gameState().get();
	currentHero.iser & currentHeroCopy;
	EXPECT_EQ(currentHeroCopy.getNewHorizonsPeacemakerLastWeek(), 4);
	EXPECT_EQ(currentHeroCopy.getNewHorizonsPacifiedCreatureId(), neutralId);
	EXPECT_EQ(currentHeroCopy.getNewHorizonsTributeLastWeek(), 4);
	EXPECT_TRUE(currentHeroCopy.hasUsedNewHorizonsPeacemaker(4));
	EXPECT_TRUE(currentHeroCopy.hasUsedNewHorizonsTribute(4));

	CMemorySerializer oldHeroWriter;
	oldHeroWriter.oser.version = ESerializationVersion::NEW_HORIZONS_DIPLOMACY_ELIGIBILITY;
	EXPECT_THROW(oldHeroWriter.oser & *hero, std::runtime_error);
	EXPECT_TRUE(oldHeroWriter.extractBuffer().empty())
		<< "An older hero writer must reject populated weekly Diplomacy state before writing bytes";

	hero->setNewHorizonsDiplomacyState(-1, ObjectInstanceID::NONE, -1);
	CMemorySerializer oldHeroSnapshot;
	oldHeroSnapshot.oser.version = ESerializationVersion::NEW_HORIZONS_DIPLOMACY_ELIGIBILITY;
	oldHeroSnapshot.iser.version = ESerializationVersion::NEW_HORIZONS_DIPLOMACY_ELIGIBILITY;
	oldHeroSnapshot.oser & *hero;
	CGHeroInstance legacyHeroCopy(gameState().get());
	legacyHeroCopy.setNewHorizonsDiplomacyState(8, neutralId, 9);
	oldHeroSnapshot.iser.cb = gameState().get();
	oldHeroSnapshot.iser & legacyHeroCopy;
	EXPECT_EQ(legacyHeroCopy.getNewHorizonsPeacemakerLastWeek(), -1);
	EXPECT_EQ(legacyHeroCopy.getNewHorizonsPacifiedCreatureId(), ObjectInstanceID::NONE);
	EXPECT_EQ(legacyHeroCopy.getNewHorizonsTributeLastWeek(), -1)
		<< "An older hero snapshot must clear any pre-existing weekly Diplomacy state";

	SetNewHorizonsDiplomacyState packet;
	packet.heroId = hero->id;
	packet.peacemakerLastWeek = 4;
	packet.pacifiedCreatureId = neutralId;
	packet.tributeLastWeek = 4;
	CMemorySerializer currentPacket;
	currentPacket.oser.version = ESerializationVersion::CURRENT;
	currentPacket.iser.version = ESerializationVersion::CURRENT;
	currentPacket.oser & packet;
	SetNewHorizonsDiplomacyState packetCopy;
	currentPacket.iser & packetCopy;
	EXPECT_EQ(packetCopy.heroId, hero->id);
	EXPECT_EQ(packetCopy.peacemakerLastWeek, 4);
	EXPECT_EQ(packetCopy.pacifiedCreatureId, neutralId);
	EXPECT_EQ(packetCopy.tributeLastWeek, 4);
	EXPECT_TRUE(packetCopy.hasValidState());

	CMemorySerializer oldPacketWriter;
	oldPacketWriter.oser.version = ESerializationVersion::NEW_HORIZONS_DIPLOMACY_ELIGIBILITY;
	EXPECT_THROW(oldPacketWriter.oser & packet, std::runtime_error);
	EXPECT_TRUE(oldPacketWriter.extractBuffer().empty())
		<< "An older packet writer must reject weekly Diplomacy state before writing bytes";

	SetNewHorizonsDiplomacyState legacyPacket;
	legacyPacket.heroId = hero->id;
	CMemorySerializer oldPacketSnapshot;
	oldPacketSnapshot.oser.version = ESerializationVersion::NEW_HORIZONS_DIPLOMACY_ELIGIBILITY;
	oldPacketSnapshot.iser.version = ESerializationVersion::NEW_HORIZONS_DIPLOMACY_ELIGIBILITY;
	// The packet was introduced with the new format, so an old-version packet
	// writer correctly refuses to emit one. Supply only its historical heroId
	// prefix to exercise the old reader's defaulting branch without weakening
	// that writer guard or implying this was a valid old network packet.
	oldPacketSnapshot.oser & legacyPacket.heroId;
	SetNewHorizonsDiplomacyState legacyPacketCopy;
	legacyPacketCopy.peacemakerLastWeek = 8;
	legacyPacketCopy.pacifiedCreatureId = neutralId;
	legacyPacketCopy.tributeLastWeek = 9;
	oldPacketSnapshot.iser & legacyPacketCopy;
	EXPECT_EQ(legacyPacketCopy.heroId, hero->id);
	EXPECT_EQ(legacyPacketCopy.peacemakerLastWeek, -1);
	EXPECT_EQ(legacyPacketCopy.pacifiedCreatureId, ObjectInstanceID::NONE);
	EXPECT_EQ(legacyPacketCopy.tributeLastWeek, -1)
		<< "An older packet snapshot must default all appended state fields";

	EXPECT_THROW(hero->setNewHorizonsDiplomacyState(-2, neutralId, 4), std::runtime_error);
	EXPECT_THROW(hero->setNewHorizonsDiplomacyState(-1, ObjectInstanceID(-2), 4), std::runtime_error);
	EXPECT_EQ(hero->getNewHorizonsPeacemakerLastWeek(), -1);

	SetNewHorizonsDiplomacyState invalidWeekPacket;
	invalidWeekPacket.heroId = hero->id;
	invalidWeekPacket.peacemakerLastWeek = -2;
	CMemorySerializer invalidWeekWire;
	invalidWeekWire.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(invalidWeekWire.oser & invalidWeekPacket, std::runtime_error);
	EXPECT_TRUE(invalidWeekWire.extractBuffer().empty());

	SetNewHorizonsDiplomacyState invalidTargetPacket;
	invalidTargetPacket.heroId = hero->id;
	invalidTargetPacket.peacemakerLastWeek = 4;
	invalidTargetPacket.pacifiedCreatureId = ObjectInstanceID(-2);
	CMemorySerializer invalidTargetWire;
	invalidTargetWire.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(invalidTargetWire.oser & invalidTargetPacket, std::runtime_error);
	EXPECT_TRUE(invalidTargetWire.extractBuffer().empty());
}
