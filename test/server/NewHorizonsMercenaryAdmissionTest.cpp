/*
 * NewHorizonsMercenaryAdmissionTest.cpp, part of VCMI engine
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
#include "../../lib/GameLibrary.h"
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
#include "../../lib/entities/creature/NewHorizonsRecruitmentTraining.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/Component.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/StackLocation.h"
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
		if(const auto * info = dynamic_cast<const InfoWindow *>(&pack))
			infoWindowTexts.push_back(info->text.toString(LIBRARY->generaltexth.get()));
		GameHandlerTestServer::applyPack(pack);
	}

	std::vector<BlockingDialog> blockingDialogs;
	std::vector<GarrisonDialog> garrisonDialogs;
	std::vector<std::string> systemMessageTexts;
	std::vector<std::string> infoWindowTexts;
};

class NewHorizonsMercenaryAdmissionTest : public TinyMapGameTest
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
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		for(const auto id : {newHorizonsTraining::MERCENARY_CAPTAIN, newHorizonsTraining::LOYAL_MERCENARIES})
		{
			bool found = false;
			for(const auto & entry : perks["skills"][DIPLOMACY_SKILL]["perks"].Vector())
				if(entry["id"].String() == id)
				{
					EXPECT_EQ(entry["effect"]["status"].String(), "active");
					found = true;
				}
			EXPECT_TRUE(found);
		}
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

	void startGameWithTwoNeutralStacks(int32_t heroPikemen, int32_t firstNeutralPikemen,
		int32_t secondNeutralPikemen, CGCreature::Character character)
	{
		const auto pikeman = creature("core:pikeman");
		const int3 firstNeutralPosition{12, 12, 0};
		const int3 secondNeutralPosition{24, 24, 0};
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({5, 5, 0}, heroType("core:christian"), PLAYER)
			.heroGarrison({{pikeman, heroPikemen}})
			.monster(firstNeutralPosition, pikeman, static_cast<uint16_t>(firstNeutralPikemen),
				static_cast<int8_t>(character))
			.monster(secondNeutralPosition, pikeman, static_cast<uint16_t>(secondNeutralPikemen),
				static_cast<int8_t>(character));
		startWithMap(std::move(builder));

		hero = findHeroAt({5, 5, 0});
		neutral = dynamic_cast<CGCreature *>(findObjectAt(firstNeutralPosition));
		secondNeutral = dynamic_cast<CGCreature *>(findObjectAt(secondNeutralPosition));
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(neutral, nullptr);
		ASSERT_NE(secondNeutral, nullptr);

		server = std::make_unique<DiplomacyRecordingServer>(gameState(), PLAYER);
		gameHandler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
	}

	void selectRecruitmentPact()
	{
		advanceToDiplomacyRank(MasteryLevel::ADVANCED, ENVOY_PERK_ID,
			newHorizonsDiplomacy::RECRUITMENT_PACT_ID);
		ASSERT_TRUE(hero->hasActivePerk(DIPLOMACY_SKILL, ENVOY_PERK_ID));
		ASSERT_TRUE(hero->hasActivePerk(DIPLOMACY_SKILL, newHorizonsDiplomacy::RECRUITMENT_PACT_ID));
	}

	void acceptFirstNeutralAndArmRecruitmentPact(int32_t & triggerDay)
	{
		ASSERT_TRUE(hero->hasActivePerk(DIPLOMACY_SKILL, newHorizonsDiplomacy::RECRUITMENT_PACT_ID));
		neutral->agression = 10;
		triggerDay = gameState()->getCalendar().getCurrentDay();
		ASSERT_GE(triggerDay, 0);
		EXPECT_EQ(hero->getNewHorizonsRecruitmentPactExpiryDay(), -1);

		const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
		ASSERT_TRUE(forecast.eligible);
		ASSERT_TRUE(forecast.willing);
		ASSERT_FALSE(forecast.authoredFree);
		grantResources(PLAYER, GameResID(EGameResID::GOLD), 10000);
		const auto neutralId = neutral->id;
		gameHandler->objectVisited(neutral, hero);
		ASSERT_EQ(server->blockingDialogs.size(), 1u)
			<< "The first stack must use the ordinary paid Diplomacy offer";
		const auto offerQuery = server->blockingDialogs.back().queryID;
		ASSERT_NE(offerQuery, QueryID::NONE);
		EXPECT_EQ(hero->getNewHorizonsRecruitmentPactExpiryDay(), -1)
			<< "A pending offer does not arm Pact before recruitment succeeds";
		ASSERT_TRUE(gameHandler->queryReply(offerQuery, 1, PLAYER));
		ASSERT_EQ(hero->getStackCount(SlotID(0)), 17);
		ASSERT_EQ(neutral->getStackCount(SlotID(0)), 1);
		ASSERT_EQ(server->garrisonDialogs.size(), 1u)
			<< "The Leadership-limited remainder stays in the normal transfer window";
		const auto transferQuery = server->garrisonDialogs.back().queryID;
		ASSERT_NE(transferQuery, QueryID::NONE);
		ASSERT_TRUE(gameHandler->queryReply(transferQuery, 0, PLAYER));
		EXPECT_EQ(gameState()->getObjInstance(neutralId), nullptr);
		EXPECT_EQ(hero->getNewHorizonsRecruitmentPactExpiryDay(), triggerDay + 7)
			<< "An accepted recruitment arms Pact only after positive troop admission";
	}

	int32_t findPactOnlyWillingStackCount(CGCreature * contactedNeutral, int32_t pactExpiryDay)
	{
		for(int32_t count = 1; count <= 100; ++count)
		{
			contactedNeutral->setStackCount(SlotID(0), static_cast<TQuantity>(count));
			const auto withPact = contactedNeutral->getNewHorizonsDiplomacyForecast(*hero);
			if(!withPact.recruitmentPact || !withPact.willing)
				continue;

			hero->setNewHorizonsDiplomacyState(hero->getNewHorizonsPeacemakerLastWeek(),
				hero->getNewHorizonsPacifiedCreatureId(), hero->getNewHorizonsTributeLastWeek(), -1);
			const bool willingWithoutPact = contactedNeutral->getNewHorizonsDiplomacyForecast(*hero).willing;
			hero->setNewHorizonsDiplomacyState(hero->getNewHorizonsPeacemakerLastWeek(),
				hero->getNewHorizonsPacifiedCreatureId(), hero->getNewHorizonsTributeLastWeek(), pactExpiryDay);
			if(!willingWithoutPact)
				return count;
		}
		return 0;
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
	CGCreature * secondNeutral = nullptr;
	std::unique_ptr<DiplomacyRecordingServer> server;
	std::unique_ptr<CGameHandler> gameHandler;
};


}

TEST_F(NewHorizonsMercenaryAdmissionTest, ActualPaidPartialJoinTagsOnlyAdmittedPortion)
{
	startGame(16, 2, CGCreature::Character::HOSTILE);
	advanceToDiplomacyRank(MasteryLevel::BASIC, newHorizonsTraining::MERCENARY_CAPTAIN);
	neutral->agression = 10;
	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.willing);
	ASSERT_FALSE(forecast.authoredFree);
	grantResources(PLAYER, GameResID(EGameResID::GOLD), 10000);
	const auto before = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	gameHandler->objectVisited(neutral, hero);
	ASSERT_EQ(server->blockingDialogs.size(), 1u);
	ASSERT_TRUE(gameHandler->queryReply(server->blockingDialogs.back().queryID, 1, PLAYER));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 17);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 1);
	EXPECT_TRUE(hero->getStackPtr(SlotID(0))->getTrainingReceipt().recruitedBy(hero->id, true));
	EXPECT_TRUE(neutral->getStackPtr(SlotID(0))->getTrainingReceipt().mercenaryOrigins.empty());
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], before - forecast.normalGoldCost);
}

TEST_F(NewHorizonsMercenaryAdmissionTest, AuthoredFreeJoinRecordsOriginWithoutSelectedPerkOrPayment)
{
	startGame(1, 2, CGCreature::Character::COMPLIANT);
	ASSERT_EQ(hero->getPerkSkillRank(DIPLOMACY_SKILL), 0);
	const auto gold = gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
	gameHandler->objectVisited(neutral, hero);
	ASSERT_EQ(server->blockingDialogs.size(), 1u);
	ASSERT_TRUE(gameHandler->queryReply(server->blockingDialogs.back().queryID, 1, PLAYER));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 3);
	EXPECT_TRUE(hero->getStackPtr(SlotID(0))->getTrainingReceipt().recruitedBy(hero->id));
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], gold);
}

TEST_F(NewHorizonsMercenaryAdmissionTest, RefusedJoinDoesNotCreateOrigin)
{
	startGame(16, 2, CGCreature::Character::HOSTILE);
	advanceToDiplomacyRank(MasteryLevel::BASIC, newHorizonsTraining::MERCENARY_CAPTAIN);
	gameHandler->objectVisited(neutral, hero);
	ASSERT_EQ(server->blockingDialogs.size(), 1u);
	ASSERT_TRUE(gameHandler->queryReply(server->blockingDialogs.back().queryID, 0, PLAYER));
	EXPECT_TRUE(hero->getStackPtr(SlotID(0))->getTrainingReceipt().mercenaryOrigins.empty());
}

TEST_F(NewHorizonsMercenaryAdmissionTest, AcceptedZeroIntakeDoesNotCreateOrigin)
{
	startGame(17, 2, CGCreature::Character::COMPLIANT);
	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
		ASSERT_TRUE(hero->setCreature(SlotID(slot), creature("core:pikeman"), 17));
	gameHandler->objectVisited(neutral, hero);
	ASSERT_EQ(server->blockingDialogs.size(), 1u);
	ASSERT_TRUE(gameHandler->queryReply(server->blockingDialogs.back().queryID, 1, PLAYER));
	ASSERT_EQ(server->garrisonDialogs.size(), 1u);
	for(const auto & [slot, stack] : hero->Slots())
		EXPECT_TRUE(stack->getTrainingReceipt().mercenaryOrigins.empty());
}

TEST_F(NewHorizonsMercenaryAdmissionTest, AcceptedLeftoverManualTransferRetainsLiveQueryOrigin)
{
	startGame(16, 20, CGCreature::Character::COMPLIANT);
	gameHandler->objectVisited(neutral, hero);
	ASSERT_TRUE(gameHandler->queryReply(server->blockingDialogs.back().queryID, 1, PLAYER));
	ASSERT_EQ(server->garrisonDialogs.size(), 1u);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 19);
	ASSERT_TRUE(gameHandler->moveStack(StackLocation(neutral->id, SlotID(0)), StackLocation(hero->id, SlotID(1)), 2));
	EXPECT_TRUE(hero->getStackPtr(SlotID(1))->getTrainingReceipt().recruitedBy(hero->id));
	EXPECT_TRUE(neutral->getStackPtr(SlotID(0))->getTrainingReceipt().mercenaryOrigins.empty());
}

TEST_F(NewHorizonsMercenaryAdmissionTest, GenericNeutralMoveOutsideAcceptedQueryDoesNotInventOrigin)
{
	startGame(1, 2, CGCreature::Character::COMPLIANT);
	ASSERT_TRUE(gameHandler->moveStack(StackLocation(neutral->id, SlotID(0)), StackLocation(hero->id, SlotID(0)), 1));
	EXPECT_TRUE(hero->getStackPtr(SlotID(0))->getTrainingReceipt().mercenaryOrigins.empty());
}

TEST_F(NewHorizonsMercenaryAdmissionTest, SplitTransferReturnAndMergePreserveOriginalRecruiter)
{
	startGame(1, 2, CGCreature::Character::COMPLIANT, {12,12,0}, true);
	gameHandler->objectVisited(neutral, hero);
	ASSERT_TRUE(gameHandler->queryReply(server->blockingDialogs.back().queryID, 1, PLAYER));
	const auto before = hero->getStackPtr(SlotID(0))->getTrainingReceipt();
	ASSERT_TRUE(gameHandler->moveStack(StackLocation(hero->id, SlotID(0)), StackLocation(otherHero->id, SlotID(6)), 1));
	EXPECT_EQ(hero->getStackPtr(SlotID(0))->getTrainingReceipt(), before);
	EXPECT_EQ(otherHero->getStackPtr(SlotID(6))->getTrainingReceipt().mercenaryOrigins, before.mercenaryOrigins);
	ASSERT_TRUE(gameHandler->moveStack(StackLocation(otherHero->id, SlotID(6)), StackLocation(hero->id, SlotID(0)), 1));
	EXPECT_EQ(hero->getStackPtr(SlotID(0))->getTrainingReceipt(), before);
}

TEST_F(NewHorizonsMercenaryAdmissionTest, ForgedHeroSourceAndInvalidBulkOriginRejectBeforeMutation)
{
	startGame(4, 2, CGCreature::Character::COMPLIANT, {12,12,0}, true);
	RebalanceStacks forged;
	forged.srcArmy = hero->id;
	forged.srcSlot = SlotID(0);
	forged.dstArmy = otherHero->id;
	forged.dstSlot = SlotID(6);
	forged.count = 1;
	forged.diplomacyRecruiter = otherHero->id;
	EXPECT_THROW(gameHandler->sendAndApply(forged), std::runtime_error);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 4);
	EXPECT_EQ(otherHero->getStackPtr(SlotID(6)), nullptr);
	RebalanceStacks valid = forged;
	valid.diplomacyRecruiter = ObjectInstanceID::NONE;
	BulkRebalanceStacks bulk;
	bulk.moves = {valid, forged};
	EXPECT_THROW(gameHandler->sendAndApply(bulk), std::runtime_error);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 4);
	EXPECT_EQ(otherHero->getStackPtr(SlotID(6)), nullptr);
}

TEST_F(NewHorizonsMercenaryAdmissionTest, PublicRoundTripOfUnofferedNeutralSlotDoesNotInventOrigin)
{
	startGame(17, 20, CGCreature::Character::COMPLIANT);
	for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
		ASSERT_TRUE(hero->setCreature(SlotID(slot), creature("core:pikeman"), 17));
	gameHandler->objectVisited(neutral, hero);
	ASSERT_TRUE(gameHandler->queryReply(server->blockingDialogs.back().queryID, 1, PLAYER));
	ASSERT_EQ(server->garrisonDialogs.size(), 1u);
	ASSERT_EQ(neutral->getStackCount(SlotID(0)), 20);
	// Free a destination without admitting the original offered stack.
	ASSERT_TRUE(gameHandler->arrangeStacks(hero->id, neutral->id, 1, SlotID(6), SlotID(3), 0, PLAYER));
	ASSERT_TRUE(gameHandler->arrangeStacks(hero->id, neutral->id, 3, SlotID(0), SlotID(1), 1, PLAYER));
	ASSERT_TRUE(gameHandler->arrangeStacks(neutral->id, hero->id, 1, SlotID(1), SlotID(6), 0, PLAYER));
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 20);
	EXPECT_EQ(hero->getStackCount(SlotID(6)), 1);
	for(const auto & [slot, stack] : hero->Slots())
		EXPECT_TRUE(stack->getTrainingReceipt().mercenaryOrigins.empty());
	EXPECT_TRUE(neutral->getStackPtr(SlotID(0))->getTrainingReceipt().mercenaryOrigins.empty());
}

TEST_F(NewHorizonsMercenaryAdmissionTest, PublicOfferedStackRelocationStillTagsActualIntake)
{
	startGame(16, 20, CGCreature::Character::COMPLIANT);
	gameHandler->objectVisited(neutral, hero);
	ASSERT_TRUE(gameHandler->queryReply(server->blockingDialogs.back().queryID, 1, PLAYER));
	ASSERT_EQ(server->garrisonDialogs.size(), 1u);
	ASSERT_EQ(neutral->getStackCount(SlotID(0)), 19);
	ASSERT_TRUE(gameHandler->arrangeStacks(neutral->id, neutral->id, 1, SlotID(0), SlotID(2), 0, PLAYER));
	ASSERT_EQ(neutral->getStackCount(SlotID(2)), 19);
	// Merge admits only the Leadership-supported part and retains offered leftovers.
	ASSERT_TRUE(hero->setCreature(SlotID(1), creature("core:pikeman"), 15));
	ASSERT_TRUE(gameHandler->arrangeStacks(neutral->id, hero->id, 2, SlotID(2), SlotID(1), 0, PLAYER));
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 17);
	EXPECT_EQ(neutral->getStackCount(SlotID(2)), 17);
	EXPECT_TRUE(hero->getStackPtr(SlotID(1))->getTrainingReceipt().recruitedBy(hero->id));
	EXPECT_TRUE(neutral->getStackPtr(SlotID(2))->getTrainingReceipt().mercenaryOrigins.empty());
}
