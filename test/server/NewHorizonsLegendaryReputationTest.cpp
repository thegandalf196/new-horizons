/*
 * NewHorizonsLegendaryReputationTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../NewHorizonsHistoricalAdventurePolicyTestUtils.h"
#include <gtest/gtest.h>

#include "../../lib/CPlayerState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameSettings.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/callback/Calendar.h"
#include "../../lib/entities/hero/NewHorizonsDiplomacy.h"
#include "../../lib/entities/hero/NewHorizonsLegendaryReputation.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include "../../server/CGameHandler.h"
#include "../../server/queries/QueriesProcessor.h"
#include "../../server/queries/VisitQueries.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"
#include "battles/FullGameSnapshotTypes.h"
#include <limits>

#include <array>
#include <memory>
#include <string_view>

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr auto PREVIOUS = ESerializationVersion::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES;

class RecordingServer final : public GameHandlerTestServer
{
public:
	using GameHandlerTestServer::GameHandlerTestServer;
	std::vector<BlockingDialog> offers;
	std::vector<GarrisonDialog> garrisons;
	std::vector<newHorizonsDiplomacy::LegendaryAdmission> receipts;

	void applyPack(CPackForClient & pack) override
	{
		if(const auto * offer = dynamic_cast<const BlockingDialog *>(&pack)) offers.push_back(*offer);
		if(const auto * dialog = dynamic_cast<const GarrisonDialog *>(&pack)) garrisons.push_back(*dialog);
		if(const auto * move = dynamic_cast<const RebalanceStacks *>(&pack); move && move->legendaryAdmission)
			receipts.push_back(*move->legendaryAdmission);
		if(const auto * moves = dynamic_cast<const BulkRebalanceStacks *>(&pack); moves && moves->legendaryAdmission)
			receipts.push_back(*moves->legendaryAdmission);
		if(const auto * swap = dynamic_cast<const SwapStacks *>(&pack); swap && swap->legendaryAdmission)
			receipts.push_back(*swap->legendaryAdmission);
		GameHandlerTestServer::applyPack(pack);
	}
};

class NewHorizonsLegendaryReputationTest : public TinyMapGameTest
{
protected:
	bool oldFormatSpecialtyIsolation = false;
	CGHeroInstance * hero = nullptr;
	CGCreature * neutral = nullptr;
	CGCreature * second = nullptr;
	std::unique_ptr<RecordingServer> server;
	std::unique_ptr<CGameHandler> handler;

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto heroes = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		if(oldFormatSpecialtyIsolation)
		{
			// This positive pre-specialty save control isolates the monthly receipt.
			// Absence (not false/null) preserves the independent key-presence guards.
			heroes["nonDamageSpellSpecialties"].Struct().erase("remainingStartReplacements");
			heroes["startingSkills"].Struct().erase("startingBookReplacements");
			heroes.Struct().erase("lighthouseDeparture");
			heroes["skillSpecialties"].Struct().erase("navigationStartReplacements");
			heroes.Struct().erase("defaultCreatureLineReplacements");
			heroes.Struct().erase("remainingSpellSpecialtyReplacements");
			auto & supported = heroes["nonDamageSpellSpecialties"]["spells"].Vector();
			supported.erase(std::remove_if(supported.begin(), supported.end(), [](const JsonNode & spell)
			{
				return spell.String() == "new-horizons:phantomArmy";
			}), supported.end());
			heroes["damageSpellSpecialties"].Struct().erase("coroniusHolyWrathReplacement");
			heroes.setOverrideFlag(true); // Preserve the authored absence through current base-settings merge.
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroes);
		auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		// The positive old-hero control captures the historical profile, not later Crisis metadata.
		if(oldFormatSpecialtyIsolation)
		{
			for(auto & perk : perks["skills"]["new-horizons:command"]["perks"].Vector())
				if(perk["id"].String() == "new-horizons:command.crisisCommand")
					perk["effect"]["status"].String() = "planned";
			perks.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		auto capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
		capabilities["leadership"]["creatureRequirements"]["core:pikeman"] = JsonNode(60);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, capabilities);
		if(oldFormatSpecialtyIsolation)
			isolateHistoricalAdventurePolicies(*loaded);
	}

	void start(CGCreature::Character character = CGCreature::Character::HOSTILE, bool selectLegendary = true, bool selectPact = false)
	{
		const auto pikeman = CreatureID(CreatureID::decode("core:pikeman"));
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PLAYER)
			.heroGarrison({{pikeman, 10}})
			.monster({12, 12, 0}, pikeman, 2, static_cast<int8_t>(character))
			.monster({24, 24, 0}, pikeman, 2, static_cast<int8_t>(CGCreature::Character::HOSTILE));
		startWithMap(std::move(builder));
		hero = findHeroAt({5, 5, 0});
		neutral = expectAt<CGCreature>({12, 12, 0});
		second = expectAt<CGCreature>({24, 24, 0});
		ASSERT_NE(hero, nullptr);
		server = std::make_unique<RecordingServer>(gameState(), PLAYER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
		gameState()->actingPlayers.insert(PLAYER);
		for(int rank = 1; rank <= 3; ++rank)
		{
			handler->levelUpHero(hero, SecondarySkill(SecondarySkill::decode(newHorizonsDiplomacy::SKILL_ID)), false);
			if(rank == 1) select("new-horizons:diplomacy.envoy", MasteryLevel::BASIC);
			if(rank == 2) select(selectPact ? newHorizonsDiplomacy::RECRUITMENT_PACT_ID : newHorizonsDiplomacy::COMMON_CAUSE_ID, MasteryLevel::ADVANCED);
			if(rank == 3 && selectLegendary) select(newHorizonsDiplomacy::LEGENDARY_REPUTATION_ID, MasteryLevel::EXPERT);
		}
	}

	void select(std::string_view perk, MasteryLevel::Type rank)
	{
		const auto lookup = [this](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(lookup, seed);
			for(size_t i = 0; i < offer.size(); ++i)
				if(offer[i].selection.skillId == newHorizonsDiplomacy::SKILL_ID && offer[i].selection.perkId == perk)
				{
					ASSERT_EQ(offer[i].requiredRank, rank);
					handler->levelUpHero(hero, offer, i, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(newHorizonsDiplomacy::SKILL_ID, std::string(perk)));
					return;
				}
		}
		FAIL() << "No legal shipped active offer for " << perk;
	}

	int64_t gold() const { return gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD]; }

	QueryID offer(CGCreature * source = nullptr)
	{
		if(!source) source = neutral;
		const auto old = server->offers.size();
		handler->objectVisited(source, hero);
		EXPECT_EQ(server->offers.size(), old + 1);
		return server->offers.back().queryID;
	}

	newHorizonsDiplomacy::LegendaryAdmission witness() const
	{
		const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
		return {hero->id, neutral->id, SlotID(0), neutral->getCreatureID(),
			gameState()->getCalendar().getMonth(), hero->getNewHorizonsLegendaryReputationLastMonth(),
			forecast.joiningAmount, neutral->getStackCount(SlotID(0)), forecast.normalGoldCost, forecast.creatureArmyValue, forecast.recruitmentPact};
	}

	MapObjectVisitQuery * acceptedVisit() const
	{
		return handler->queries->findQuery<MapObjectVisitQuery>([this](const auto & visit)
			{ return visit.visitedObject == neutral->id && visit.visitingHero == hero->id; });
	}

	void retainAcceptedOffer()
	{
		const auto capacity = hero->getLeadershipSlotCapacity(neutral->getCreatureID());
		ASSERT_TRUE(capacity);
		for(int slot = 0; slot < GameConstants::ARMY_SIZE; ++slot)
			ASSERT_TRUE(hero->setCreature(SlotID(slot), neutral->getCreatureID(), capacity->maximum));
		ASSERT_TRUE(handler->queryReply(offer(), 1, PLAYER));
		ASSERT_EQ(server->garrisons.size(), 1u);
		ASSERT_EQ(neutral->getStackCount(SlotID(0)), 2);
		ASSERT_NE(acceptedVisit(), nullptr);
		ASSERT_TRUE(acceptedVisit()->legendaryOfferAccepted);
		ASSERT_TRUE(acceptedVisit()->trackingNeutralRecruitment);
		ASSERT_EQ(acceptedVisit()->acceptedNeutralRemaining, 2);
		ASSERT_TRUE(handler->arrangeStacks(hero->id, neutral->id, 1, SlotID(6), SlotID(3), 0, PLAYER));
		ASSERT_EQ(hero->getStackPtr(SlotID(6)), nullptr);
	}

	RebalanceStacks move() const
	{
		RebalanceStacks result;
		result.srcArmy = neutral->id; result.dstArmy = hero->id;
		result.srcSlot = SlotID(0); result.dstSlot = SlotID(0); result.count = 1;
		result.legendaryAdmission = witness();
		return result;
	}
};

TEST_F(NewHorizonsLegendaryReputationTest, ActualAcceptedVisitWaivesGoldAndCommitsOnePositiveAdmission)
{
	start();
	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.willing);
	ASSERT_TRUE(forecast.legendaryReputation);
	ASSERT_GT(forecast.normalGoldCost, 0);
	EXPECT_EQ(forecast.goldCost(), 0);
	const auto beforeGold = gold();
	const auto sourceID = neutral->id;
	const auto query = offer();
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	ASSERT_TRUE(handler->queryReply(query, 1, PLAYER));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 12);
	EXPECT_EQ(gold(), beforeGold);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), gameState()->getCalendar().getMonth());
	EXPECT_EQ(gameState()->getObjInstance(sourceID), nullptr);
	ASSERT_EQ(server->receipts.size(), 1);
	EXPECT_EQ(server->receipts.front().originalQuantity, 2);
	EXPECT_FALSE(second->getNewHorizonsDiplomacyForecast(*hero).legendaryReputation);
}

TEST_F(NewHorizonsLegendaryReputationTest, AcceptedPactQualifiedWaiverKeepsOriginalThresholdAfterPactConsumption)
{
	start(CGCreature::Character::HOSTILE, true, true);
	neutral->setStackCount(SlotID(0), 8);
	ASSERT_FALSE(neutral->getNewHorizonsDiplomacyForecast(*hero).willing);
	hero->setNewHorizonsDiplomacyState(-1, ObjectInstanceID::NONE, -1,
		gameState()->getCalendar().getCurrentDay() + 7);
	const auto forecast = neutral->getNewHorizonsDiplomacyForecast(*hero);
	ASSERT_TRUE(forecast.recruitmentPact);
	ASSERT_TRUE(forecast.legendaryReputation);
	const auto beforeGold = gold();
	ASSERT_TRUE(handler->queryReply(offer(), 1, PLAYER));
	ASSERT_EQ(server->receipts.size(), 1);
	EXPECT_TRUE(server->receipts.front().recruitmentPactApplied);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), gameState()->getCalendar().getMonth());
	EXPECT_EQ(hero->getNewHorizonsRecruitmentPactExpiryDay(), -1);
	EXPECT_EQ(gold(), beforeGold);
	ASSERT_EQ(server->garrisons.size(), 1);
	ASSERT_TRUE(handler->queryReply(server->garrisons.back().queryID, 0, PLAYER));
	EXPECT_EQ(hero->getNewHorizonsRecruitmentPactExpiryDay(), gameState()->getCalendar().getCurrentDay() + 7);
}

TEST_F(NewHorizonsLegendaryReputationTest, PartialLeadershipIntakeSpendsOnceBeforeOrdinaryRemainderDialog)
{
	start();
	const auto capacity = hero->getLeadershipSlotCapacity(neutral->getCreatureID());
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 1);
	hero->setStackCount(SlotID(0), capacity->maximum - 1);
	const auto beforeGold = gold();
	ASSERT_TRUE(handler->queryReply(offer(), 1, PLAYER));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 1);
	ASSERT_EQ(server->garrisons.size(), 1);
	EXPECT_EQ(server->receipts.size(), 1);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), gameState()->getCalendar().getMonth());
	ASSERT_TRUE(handler->queryReply(server->garrisons.back().queryID, 0, PLAYER));
	EXPECT_EQ(server->receipts.size(), 1);
	EXPECT_EQ(gold(), beforeGold);
}

TEST_F(NewHorizonsLegendaryReputationTest, ZeroAdmissionAndCancelledRemainderDoNotSpendMonth)
{
	start();
	const auto capacity = hero->getLeadershipSlotCapacity(neutral->getCreatureID());
	ASSERT_TRUE(capacity);
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		ASSERT_TRUE(hero->setCreature(SlotID(i), neutral->getCreatureID(), capacity->maximum));
	const auto beforeGold = gold();
	ASSERT_TRUE(handler->queryReply(offer(), 1, PLAYER));
	ASSERT_EQ(server->garrisons.size(), 1);
	EXPECT_TRUE(server->receipts.empty());
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	ASSERT_TRUE(handler->queryReply(server->garrisons.back().queryID, 0, PLAYER));
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	EXPECT_EQ(gold(), beforeGold);
}

TEST_F(NewHorizonsLegendaryReputationTest, RefusedOfferDoesNotConsume)
{
	start();
	const auto beforeGold = gold();
	ASSERT_TRUE(handler->queryReply(offer(), 0, PLAYER));
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	EXPECT_TRUE(server->receipts.empty());
	EXPECT_EQ(gold(), beforeGold);
}

TEST_F(NewHorizonsLegendaryReputationTest, AuthoredFreeAdmissionPreservesTheMonthlyWaiver)
{
	start(CGCreature::Character::COMPLIANT);
	ASSERT_TRUE(neutral->getNewHorizonsDiplomacyForecast(*hero).authoredFree);
	ASSERT_FALSE(neutral->getNewHorizonsDiplomacyForecast(*hero).legendaryReputation);
	ASSERT_TRUE(handler->queryReply(offer(), 1, PLAYER));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 12);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	EXPECT_TRUE(server->receipts.empty());
	EXPECT_TRUE(second->getNewHorizonsDiplomacyForecast(*hero).legendaryReputation);
}

TEST_F(NewHorizonsLegendaryReputationTest, ActualCalendarMonthRollRearmsWithoutPolling)
{
	start();
	ASSERT_TRUE(handler->queryReply(offer(), 1, PLAYER));
	const auto month = gameState()->getCalendar().getMonth();
	EXPECT_FALSE(second->getNewHorizonsDiplomacyForecast(*hero).legendaryReputation);
	gameState()->day = gameState()->getCalendar().getDaysInMonth() + 1;
	ASSERT_EQ(gameState()->getCalendar().getMonth(), month + 1);
	ASSERT_TRUE(second->getNewHorizonsDiplomacyForecast(*hero).legendaryReputation);
	ASSERT_TRUE(handler->queryReply(offer(second), 1, PLAYER));
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), month + 1);
	EXPECT_EQ(server->receipts.size(), 2);
}

TEST_F(NewHorizonsLegendaryReputationTest, CapturedFreeOfferOnOldMonthRejectsBeforeRewardPaymentOrStock)
{
	start();
	const auto query = offer();
	const auto beforeGold = gold();
	gameState()->day = gameState()->getCalendar().getDaysInMonth() + 1;
	EXPECT_FALSE(handler->validateNeutralDiplomacyOffer(neutral, hero, 0));
	ASSERT_TRUE(handler->queryReply(query, 1, PLAYER));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	EXPECT_TRUE(server->receipts.empty());
	EXPECT_EQ(gold(), beforeGold);
}

TEST_F(NewHorizonsLegendaryReputationTest, PerkAcquiredAfterPaidQuoteCannotInventAFreeOffer)
{
	start(CGCreature::Character::HOSTILE, false);
	handler->giveResource(PLAYER, EGameResID::GOLD, 1000);
	const auto query = offer();
	select(newHorizonsDiplomacy::LEGENDARY_REPUTATION_ID, MasteryLevel::EXPERT);
	ASSERT_TRUE(neutral->getNewHorizonsDiplomacyForecast(*hero).legendaryReputation);
	const auto beforeGold = gold();
	EXPECT_FALSE(handler->validateNeutralDiplomacyOffer(neutral, hero, 0));
	ASSERT_TRUE(handler->queryReply(query, 1, PLAYER));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(gold(), beforeGold);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
}

TEST_F(NewHorizonsLegendaryReputationTest, MalformedAndReplayTransactionsRejectBeforeAnyStockOrMonthMutation)
{
	start();
	auto packet = move();
	EXPECT_THROW(handler->sendAndApply(packet), std::runtime_error);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	packet.count = 3;
	EXPECT_THROW(gameState()->apply(packet), std::runtime_error);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	packet = move();
	EXPECT_NO_THROW(gameState()->apply(packet));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 1);
	EXPECT_THROW(gameState()->apply(packet), std::runtime_error);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 11);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 1);
}

TEST_F(NewHorizonsLegendaryReputationTest, BulkWholeTransactionFailureAndWrongSourceSwapPreserveBothArmies)
{
	start();
	BulkRebalanceStacks bulk;
	auto first = move(); first.legendaryAdmission.reset();
	bulk.moves.push_back(first);
	auto bad = first; bad.count = 2;
	bulk.moves.push_back(bad);
	bulk.legendaryAdmission = witness();
	EXPECT_THROW(gameState()->apply(bulk), std::runtime_error);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	SwapStacks swap;
	swap.srcArmy = second->id; swap.dstArmy = hero->id;
	swap.srcSlot = swap.dstSlot = SlotID(0); swap.legendaryAdmission = witness();
	EXPECT_THROW(gameState()->apply(swap), std::runtime_error);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(second->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
}

TEST_F(NewHorizonsLegendaryReputationTest, CurrentPacketsRoundTripAndOldMeaningfulWritesHaveZeroPrefix)
{
	start();
	const auto value = witness();
	auto packet = move();
	CMemorySerializer current;
	current.oser & packet;
	RebalanceStacks restored;
	current.iser & restored;
	ASSERT_TRUE(restored.legendaryAdmission);
	EXPECT_EQ(restored.legendaryAdmission->hero, value.hero);
	EXPECT_EQ(restored.legendaryAdmission->source, value.source);
	EXPECT_EQ(restored.legendaryAdmission->month, value.month);
	EXPECT_EQ(restored.legendaryAdmission->previousMonth, value.previousMonth);
	EXPECT_EQ(restored.legendaryAdmission->normalGoldCost, value.normalGoldCost);
	EXPECT_EQ(restored.legendaryAdmission->originalQuantity, value.originalQuantity);
	EXPECT_EQ(restored.legendaryAdmission->sourceQuantity, value.sourceQuantity);
	EXPECT_EQ(*restored.legendaryAdmission, value);
	const auto roundTrip = [&](auto packet)
	{
		CMemorySerializer stream;
		stream.oser & packet;
		decltype(packet) decoded;
		stream.iser & decoded;
		ASSERT_TRUE(decoded.legendaryAdmission);
		EXPECT_EQ(*decoded.legendaryAdmission, value);
		packet.legendaryAdmission.reset();
		CMemorySerializer old; old.oser.version = old.iser.version = PREVIOUS;
		old.oser & packet;
		decoded.legendaryAdmission = value;
		old.iser & decoded;
		EXPECT_FALSE(decoded.legendaryAdmission);
	};
	SwapStacks roundSwap; roundSwap.srcArmy = value.source; roundSwap.dstArmy = value.hero;
	roundSwap.srcSlot = roundSwap.dstSlot = SlotID(0); roundSwap.legendaryAdmission = value;
	roundTrip(roundSwap);
	BulkRebalanceStacks roundBulk; auto roundChild = packet; roundChild.legendaryAdmission.reset();
	roundBulk.moves.push_back(roundChild); roundBulk.legendaryAdmission = value;
	roundTrip(roundBulk);
	for(int kind = 0; kind < 3; ++kind)
	{
		CMemorySerializer older; older.oser.version = PREVIOUS;
		if(kind == 0) EXPECT_THROW(older.oser & packet, std::runtime_error);
		if(kind == 1)
		{
			BulkRebalanceStacks bulk; auto child = packet; child.legendaryAdmission.reset();
			bulk.moves.push_back(child); bulk.legendaryAdmission = value;
			EXPECT_THROW(older.oser & bulk, std::runtime_error);
		}
		if(kind == 2)
		{
			SwapStacks swap; swap.srcArmy = value.source; swap.dstArmy = value.hero;
			swap.srcSlot = swap.dstSlot = SlotID(0); swap.legendaryAdmission = value;
			EXPECT_THROW(older.oser & swap, std::runtime_error);
		}
		EXPECT_TRUE(older.extractBuffer().empty());
	}
	packet.legendaryAdmission.reset();
	CMemorySerializer oldPlain; oldPlain.oser.version = oldPlain.iser.version = PREVIOUS;
	oldPlain.oser & packet;
	restored.legendaryAdmission = value;
	oldPlain.iser & restored;
	EXPECT_FALSE(restored.legendaryAdmission);
}

TEST_F(NewHorizonsLegendaryReputationTest, HeroMapWorldAndLobbyRejectOldPrefixAndCurrentStateRestores)
{
	oldFormatSpecialtyIsolation = true;
	start();
	EXPECT_FALSE(hero->getPrimaryGrowthRules()["nonDamageSpellSpecialties"].Struct().contains("remainingStartReplacements"));
	EXPECT_FALSE(hero->getPrimaryGrowthRules()["damageSpellSpecialties"].Struct().contains("coroniusHolyWrathReplacement"));
	CMemorySerializer unusedOld;
	unusedOld.oser.version = unusedOld.iser.version = PREVIOUS;
	EXPECT_NO_THROW(unusedOld.oser & *hero);
	hero->markNewHorizonsLegendaryReputationUsed(gameState()->getCalendar().getMonth());
	CMemorySerializer current; current.iser.cb = gameState().get();
	current.oser & *hero;
	CGHeroInstance copy(gameState().get());
	current.iser & copy;
	EXPECT_EQ(copy.getNewHorizonsLegendaryReputationLastMonth(), hero->getNewHorizonsLegendaryReputationLastMonth());
	unusedOld.iser.cb = gameState().get();
	unusedOld.iser & copy;
	EXPECT_EQ(copy.getNewHorizonsLegendaryReputationLastMonth(), -1);
	CMemorySerializer oldHero; oldHero.oser.version = PREVIOUS;
	EXPECT_THROW(oldHero.oser & *hero, std::runtime_error);
	EXPECT_TRUE(oldHero.extractBuffer().empty());
	CMemorySerializer oldMap; oldMap.oser.version = PREVIOUS;
	EXPECT_THROW(oldMap.oser & *map(), std::runtime_error);
	EXPECT_TRUE(oldMap.extractBuffer().empty());
	CMemorySerializer oldWorld; oldWorld.oser.version = PREVIOUS;
	EXPECT_THROW(oldWorld.oser & *gameState(), std::runtime_error);
	EXPECT_TRUE(oldWorld.extractBuffer().empty());
	LobbyStartGame lobby; lobby.initializedGameState = gameState();
	CMemorySerializer oldLobby; oldLobby.oser.version = PREVIOUS;
	EXPECT_THROW(oldLobby.oser & lobby, std::runtime_error);
	EXPECT_TRUE(oldLobby.extractBuffer().empty());
	const auto pooled = std::dynamic_pointer_cast<CGHeroInstance>(map()->eraseObject(hero->id));
	ASSERT_NE(pooled, nullptr);
	map()->addToHeroPool(pooled);
	ASSERT_EQ(map()->tryGetFromHeroPool(pooled->getHeroTypeID()), hero);
	ASSERT_EQ(map()->objects[hero->id.getNum()], nullptr);
	CMemorySerializer offMap; offMap.oser.version = PREVIOUS;
	EXPECT_THROW(offMap.oser & *map(), std::runtime_error);
	EXPECT_TRUE(offMap.extractBuffer().empty());
	CMemorySerializer offWorld; offWorld.oser.version = PREVIOUS;
	EXPECT_THROW(offWorld.oser & *gameState(), std::runtime_error);
	EXPECT_TRUE(offWorld.extractBuffer().empty());
	CMemorySerializer offLobby; offLobby.oser.version = PREVIOUS;
	EXPECT_THROW(offLobby.oser & lobby, std::runtime_error);
	EXPECT_TRUE(offLobby.extractBuffer().empty());
}

TEST_F(NewHorizonsLegendaryReputationTest, FutureCurrentMonthRejectsInCalendarBoundHeroAndBeforeWorldPrefix)
{
	start();
	const auto originalDay = gameState()->day;
	gameState()->day = gameState()->getCalendar().getDaysInMonth() + 1;
	hero->markNewHorizonsLegendaryReputationUsed(gameState()->getCalendar().getMonth());
	gameState()->day = originalDay;
	EXPECT_THROW(hero->validateNewHorizonsLegendaryReputationSerialization(true,
		gameState()->getCalendar().getMonth()), std::runtime_error);
	CMemorySerializer worldWriter;
	EXPECT_THROW(worldWriter.oser & *gameState(), std::runtime_error);
	EXPECT_TRUE(worldWriter.extractBuffer().empty());
}

TEST_F(NewHorizonsLegendaryReputationTest, JointValidMonthInvalidCohortAndReverseRejectWithoutPublication)
{
	start();
	const auto training = hero->getStackPtr(SlotID(0))->getTrainingReceipt();
	auto packet = move();
	packet.diplomacyRecruiter = neutral->id; // Not the receiving hero.
	EXPECT_THROW(gameState()->apply(packet), std::runtime_error);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(hero->getStackPtr(SlotID(0))->getTrainingReceipt(), training);
	packet = move();
	packet.diplomacyRecruiter = hero->id;
	packet.legendaryAdmission->month += 1; // Valid cohort, stale monthly witness.
	EXPECT_THROW(gameState()->apply(packet), std::runtime_error);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(hero->getStackPtr(SlotID(0))->getTrainingReceipt(), training);
	EXPECT_TRUE(neutral->getStackPtr(SlotID(0))->getTrainingReceipt().mercenaryOrigins.empty());
}

TEST_F(NewHorizonsLegendaryReputationTest, JointLaterBulkCohortFailurePreservesEarlierMonthAndTraining)
{
	start();
	BulkRebalanceStacks bulk;
	bulk.legendaryAdmission = witness();
	auto first = move();
	first.legendaryAdmission.reset();
	first.diplomacyRecruiter = hero->id;
	bulk.moves.push_back(first);
	auto invalid = first;
	invalid.srcArmy = hero->id;
	invalid.dstArmy = neutral->id;
	invalid.dstSlot = SlotID(1);
	bulk.moves.push_back(invalid); // A hero source cannot invent neutral provenance.
	EXPECT_THROW(gameState()->apply(bulk), std::runtime_error);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 10);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(neutral->getStackPtr(SlotID(1)), nullptr);
	EXPECT_TRUE(hero->getStackPtr(SlotID(0))->getTrainingReceipt().mercenaryOrigins.empty());
}

TEST_F(NewHorizonsLegendaryReputationTest, JointRejectedSenderBulkPreservesBothAcceptedQueryCursorsAndBudget)
{
	start();
	retainAcceptedOffer();
	auto * visit = acceptedVisit();
	const auto remaining = visit->acceptedNeutralRemaining;
	const auto month = hero->getNewHorizonsLegendaryReputationLastMonth();
	const auto beforeGold = gold();
	BulkRebalanceStacks bulk;
	RebalanceStacks first;
	first.srcArmy = neutral->id; first.dstArmy = hero->id;
	first.srcSlot = SlotID(0); first.dstSlot = SlotID(6); first.count = 1;
	bulk.moves.push_back(first);
	auto invalid = first; invalid.count = 2;
	bulk.moves.push_back(invalid);
	EXPECT_THROW(handler->sendAndApply(bulk), std::runtime_error);
	EXPECT_EQ(hero->getStackPtr(SlotID(6)), nullptr);
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 2);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), month);
	EXPECT_EQ(gold(), beforeGold);
	EXPECT_EQ(visit->acceptedNeutralRemaining, remaining);
	EXPECT_EQ(visit->acceptedNeutralSlot, SlotID(0));
	EXPECT_EQ(visit->legendaryOfferSlot, SlotID(0));
	EXPECT_FALSE(visit->legendaryOfferAdmitted);
	EXPECT_FALSE(visit->admittedNeutralRecruitment);
	EXPECT_TRUE(neutral->getStackPtr(SlotID(0))->getTrainingReceipt().mercenaryOrigins.empty());
}

TEST_F(NewHorizonsLegendaryReputationTest, JointSequentialBulkRelocationAndPartialIntakesCommitMonthOnce)
{
	start();
	retainAcceptedOffer();
	auto * visit = acceptedVisit();
	BulkRebalanceStacks bulk;
	RebalanceStacks relocate;
	relocate.srcArmy = relocate.dstArmy = neutral->id;
	relocate.srcSlot = SlotID(0); relocate.dstSlot = SlotID(2); relocate.count = 2;
	bulk.moves.push_back(relocate);
	RebalanceStacks intake;
	intake.srcArmy = neutral->id; intake.dstArmy = hero->id;
	intake.srcSlot = SlotID(2); intake.dstSlot = SlotID(6); intake.count = 1;
	bulk.moves.push_back(intake);
	const auto beforeGold = gold();
	ASSERT_NO_THROW(handler->sendAndApply(bulk));
	ASSERT_EQ(neutral->getStackPtr(SlotID(0)), nullptr);
	ASSERT_EQ(neutral->getStackCount(SlotID(2)), 1);
	ASSERT_EQ(hero->getStackCount(SlotID(6)), 1);
	EXPECT_TRUE(hero->getStackPtr(SlotID(6))->getTrainingReceipt().recruitedBy(hero->id, true));
	EXPECT_EQ(hero->getStackPtr(SlotID(6))->getTrainingReceipt().mercenaryOrigins.front().combatsRemaining, 3);
	EXPECT_TRUE(neutral->getStackPtr(SlotID(2))->getTrainingReceipt().mercenaryOrigins.empty());
	EXPECT_EQ(visit->acceptedNeutralRemaining, 1);
	EXPECT_EQ(visit->acceptedNeutralSlot, SlotID(2));
	EXPECT_EQ(visit->legendaryOfferSlot, SlotID(2));
	EXPECT_TRUE(visit->legendaryOfferAdmitted);
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), gameState()->getCalendar().getMonth());
	ASSERT_EQ(server->receipts.size(), 1u);
	ASSERT_NO_THROW(handler->sendAndApply(intake));
	EXPECT_EQ(hero->getStackCount(SlotID(6)), 2);
	EXPECT_EQ(visit->acceptedNeutralRemaining, 0);
	EXPECT_EQ(server->receipts.size(), 1u);
	EXPECT_EQ(gold(), beforeGold);
	EXPECT_EQ(hero->getStackPtr(SlotID(6))->getTrainingReceipt().mercenaryOrigins.size(), 1u);
	EXPECT_EQ(hero->getStackPtr(SlotID(6))->getTrainingReceipt().mercenaryOrigins.front().combatsRemaining, 3);
}

TEST_F(NewHorizonsLegendaryReputationTest, JointPublicReturnedUnofferedSlotBindsNeitherReceipt)
{
	start();
	retainAcceptedOffer();
	auto * visit = acceptedVisit();
	ASSERT_TRUE(handler->arrangeStacks(hero->id, neutral->id, 3, SlotID(0), SlotID(1), 1, PLAYER));
	ASSERT_TRUE(handler->arrangeStacks(neutral->id, hero->id, 1, SlotID(1), SlotID(6), 0, PLAYER));
	EXPECT_EQ(neutral->getStackCount(SlotID(0)), 2);
	ASSERT_NE(hero->getStackPtr(SlotID(6)), nullptr);
	EXPECT_TRUE(hero->getStackPtr(SlotID(6))->getTrainingReceipt().mercenaryOrigins.empty());
	EXPECT_EQ(hero->getNewHorizonsLegendaryReputationLastMonth(), -1);
	EXPECT_EQ(visit->acceptedNeutralRemaining, 2);
	EXPECT_EQ(visit->acceptedNeutralSlot, SlotID(0));
	EXPECT_EQ(visit->legendaryOfferSlot, SlotID(0));
	EXPECT_FALSE(visit->legendaryOfferAdmitted);
}

TEST_F(NewHorizonsLegendaryReputationTest, JointPacketsRoundTripBothReceiptsAndLegacyReuseClearsBoth)
{
	start();
	const auto value = witness();
	const auto check = [&](auto packet, auto hasCohort, auto clearCohort, auto restoreCohort)
	{
		CMemorySerializer current;
		current.oser & packet;
		decltype(packet) restored;
		current.iser & restored;
		ASSERT_TRUE(restored.legendaryAdmission);
		EXPECT_EQ(*restored.legendaryAdmission, value);
		EXPECT_TRUE(hasCohort(restored));
		for(int only = 0; only < 3; ++only)
		{
			auto oldPacket = packet;
			if(only == 0) oldPacket.legendaryAdmission.reset();
			if(only == 1) clearCohort(oldPacket);
			CMemorySerializer old; old.oser.version = PREVIOUS;
			EXPECT_THROW(old.oser & oldPacket, std::runtime_error);
			EXPECT_TRUE(old.extractBuffer().empty());
		}
		packet.legendaryAdmission.reset();
		clearCohort(packet);
		CMemorySerializer old; old.oser.version = old.iser.version = PREVIOUS;
		old.oser & packet;
		restored.legendaryAdmission = value;
		restoreCohort(restored);
		old.iser & restored;
		EXPECT_FALSE(restored.legendaryAdmission);
		EXPECT_FALSE(hasCohort(restored));
	};
	auto rebalance = move(); rebalance.diplomacyRecruiter = hero->id;
	const auto hasSingle = [](const auto & p) { return p.diplomacyRecruiter.hasValue(); };
	const auto clearSingle = [](auto & p) { p.diplomacyRecruiter = ObjectInstanceID::NONE; };
	const auto restoreSingle = [this](auto & p) { p.diplomacyRecruiter = hero->id; };
	check(rebalance, hasSingle, clearSingle, restoreSingle);
	SwapStacks swap;
	swap.srcArmy = neutral->id; swap.dstArmy = hero->id;
	swap.srcSlot = swap.dstSlot = SlotID(0);
	swap.legendaryAdmission = value; swap.diplomacyRecruiter = hero->id;
	check(swap, hasSingle, clearSingle, restoreSingle);
	BulkRebalanceStacks bulk;
	rebalance.legendaryAdmission.reset();
	bulk.moves.push_back(rebalance); bulk.legendaryAdmission = value;
	check(bulk, [](const auto & p) { return p.moves.front().diplomacyRecruiter.hasValue(); },
		[](auto & p) { p.moves.front().diplomacyRecruiter = ObjectInstanceID::NONE; },
		[this](auto & p) { p.moves.front().diplomacyRecruiter = hero->id; });
}

TEST(NewHorizonsLegendaryReputationRules, OverflowFreeAuthoredAndUnselectedDoNotSpendAPrototypeWaiver)
{
	newHorizonsDiplomacy::ForecastInput input;
	input.usesNewHorizonsRules = input.encounterEligible = true;
	input.skillRank = 3; input.legendaryReputation = true;
	input.heroArmyValue = 1000; input.creatureArmyValue = 1;
	input.goldCostPerCreature = 10; input.joiningAmount = 2;
	ASSERT_TRUE(newHorizonsDiplomacy::resolveForecast(input).legendaryReputation);
	input.legendaryReputation = false;
	EXPECT_FALSE(newHorizonsDiplomacy::resolveForecast(input).legendaryReputation);
	input.legendaryReputation = true; input.authoredFree = true;
	EXPECT_FALSE(newHorizonsDiplomacy::resolveForecast(input).legendaryReputation);
	input.authoredFree = false; input.goldCostPerCreature = 0;
	EXPECT_FALSE(newHorizonsDiplomacy::resolveForecast(input).legendaryReputation);
	input.goldCostPerCreature = std::numeric_limits<int64_t>::max();
	const auto overflow = newHorizonsDiplomacy::resolveForecast(input);
	EXPECT_FALSE(overflow.normalGoldCostValid);
	EXPECT_FALSE(overflow.willing);
	EXPECT_FALSE(overflow.legendaryReputation);
}

TEST(NewHorizonsLegendaryReputationRules, ArmyValueWireRoundTripsEntireUnsignedRangeThroughBulkPacket)
{
	const std::array<uint64_t, 7> values = {
		0, 1, std::numeric_limits<uint32_t>::max(), uint64_t{1} << 32,
		static_cast<uint64_t>(std::numeric_limits<int64_t>::max()),
		static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1,
		std::numeric_limits<uint64_t>::max()
	};
	for(const auto value : values)
	{
		SCOPED_TRACE(value);
		newHorizonsDiplomacy::LegendaryAdmission receipt;
		receipt.hero = ObjectInstanceID(2); receipt.source = ObjectInstanceID(1);
		receipt.sourceSlot = SlotID(0); receipt.creature = CreatureID(0);
		receipt.month = 1; receipt.previousMonth = -1;
		receipt.originalQuantity = receipt.sourceQuantity = receipt.normalGoldCost = 1;
		receipt.originalArmyValue = value;
		BulkRebalanceStacks packet;
		RebalanceStacks move;
		move.srcArmy = receipt.source; move.dstArmy = receipt.hero;
		move.srcSlot = move.dstSlot = SlotID(0); move.count = 1;
		packet.moves.push_back(move); packet.legendaryAdmission = receipt;
		CMemorySerializer current;
		ASSERT_NO_THROW(current.oser & packet);
		BulkRebalanceStacks restored;
		ASSERT_NO_THROW(current.iser & restored);
		ASSERT_TRUE(restored.legendaryAdmission);
		EXPECT_EQ(*restored.legendaryAdmission, receipt);
		CMemorySerializer older;
		older.oser.version = static_cast<ESerializationVersion>(
			static_cast<int>(ESerializationVersion::NEW_HORIZONS_LEGENDARY_REPUTATION) - 1);
		EXPECT_THROW(older.oser & packet, std::runtime_error);
		EXPECT_TRUE(older.extractBuffer().empty());
	}
}

TEST(NewHorizonsLegendaryReputationRules, ArmyValueMalformedWireHalvesRejectBeforeValueAssignment)
{
	constexpr int64_t outside = static_cast<int64_t>(std::numeric_limits<uint32_t>::max()) + 1;
	const std::array<std::pair<int64_t, int64_t>, 4> invalid = {
		std::pair<int64_t, int64_t>{-1, 0}, {0, -1}, {outside, 0}, {0, outside}
	};
	for(const auto & [high, low] : invalid)
	{
		newHorizonsDiplomacy::LegendaryAdmission receipt;
		receipt.hero = ObjectInstanceID(2); receipt.source = ObjectInstanceID(1);
		receipt.sourceSlot = SlotID(0); receipt.creature = CreatureID(0);
		receipt.month = 1; receipt.previousMonth = -1;
		receipt.originalQuantity = receipt.sourceQuantity = receipt.normalGoldCost = 1;
		receipt.originalArmyValue = 42;
		CMemorySerializer forged;
		forged.oser & receipt.hero; forged.oser & receipt.source;
		forged.oser & receipt.sourceSlot; forged.oser & receipt.creature;
		forged.oser & receipt.month; forged.oser & receipt.previousMonth;
		forged.oser & receipt.originalQuantity; forged.oser & receipt.sourceQuantity;
		forged.oser & receipt.normalGoldCost; forged.oser & high; forged.oser & low;
		forged.oser & receipt.recruitmentPactApplied;
		EXPECT_THROW(forged.iser & receipt, std::runtime_error);
		EXPECT_EQ(receipt.originalArmyValue, 42u);
	}
}
}
