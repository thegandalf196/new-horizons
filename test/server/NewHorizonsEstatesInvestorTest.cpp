/*
 * NewHorizonsEstatesInvestorTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "../../lib/CPlayerState.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/ResourceSet.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"
#include "battles/FullGameSnapshotTypes.h"
#include "../mock/TinyMapGameTest.h"

#include <vstd/ContainerUtils.h>

namespace
{
const PlayerColor PLAYER(0);
constexpr auto ESTATES_SKILL = "new-horizons:estates";
constexpr auto TAX_COLLECTOR = "new-horizons:estates.taxCollector";
constexpr auto INVESTOR = "new-horizons:estates.investor";
constexpr auto FINANCIER = "new-horizons:estates.financier";

class RecordingGameServer final : public IGameServer
{
public:
	explicit RecordingGameServer(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	void setState(EServerState value) override { stateValue = value; }
	EServerState getState() const override { return stateValue; }
	bool isPlayerHost(const PlayerColor &) const override { return true; }
	bool hasPlayerAt(PlayerColor, GameConnectionID) const override { return true; }
	bool hasBothPlayersAtSameConnection(PlayerColor, PlayerColor) const override { return true; }

	void applyPack(CPackForClient & pack) override
	{
		if(const auto * turn = dynamic_cast<const NewTurn *>(&pack))
			lastNewTurn = *turn;
		state->apply(pack);
	}

	void sendPack(CPackForClient &, GameConnectionID) override {}

	std::optional<NewTurn> lastNewTurn;

private:
	EServerState stateValue = EServerState::GAMEPLAY;
	std::shared_ptr<CGameState> state;
};

class NewHorizonsEstatesInvestorTest : public TinyMapGameTest
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

		JsonNode daysPerWeek;
		daysPerWeek.Integer() = 7;
		loaded->overrideGameSetting(EGameSettings::GENERAL_DAYS_PER_WEEK, daysPerWeek);
		JsonNode weeksPerMonth;
		weeksPerMonth.Integer() = 4;
		loaded->overrideGameSetting(EGameSettings::GENERAL_WEEKS_PER_MONTH, weeksPerMonth);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		// Investor is still planned in the live registry. Enable only this saved
		// rule so the fixture exercises a normal legal offer and selection.
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perkRules["skills"][ESTATES_SKILL]["perks"].Vector())
		{
			if(perk["id"].String() == INVESTOR)
				perk["effect"]["status"].String() = "active";
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({32, 32, 0}, HeroTypeID(0), PLAYER)
			.hero({32, 28, 0}, HeroTypeID(1), PLAYER);
		startWithMap(std::move(builder));

		firstHero = findHeroAt({32, 32, 0});
		secondHero = findHeroAt({32, 28, 0});
		ASSERT_NE(firstHero, nullptr);
		ASSERT_NE(secondHero, nullptr);
	}

	static SecondarySkill estatesSkill()
	{
		const int decoded = SecondarySkill::decode(ESTATES_SKILL);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	static bool selectOfferedPerk(CGameHandler & handler, CGHeroInstance * hero,
		std::string_view perkId, MasteryLevel::Type requiredRank)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t index = 0; index < offer.size(); ++index)
			{
				if(offer[index].selection.perkId != perkId)
					continue;
				if(offer[index].requiredRank != requiredRank)
					return false;
				handler.levelUpHero(hero, offer, index, seed, false);
				return true;
			}
		}
		return false;
	}

	static bool hasOfferedPerk(const CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 2048; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			if(std::ranges::any_of(offer, [perkId](const auto & candidate)
				{
					return candidate.selection.perkId == perkId;
				}))
				return true;
		}
		return false;
	}

	void prepareForEstatesProgression(CGameHandler & handler, CGHeroInstance * hero)
	{
		handler.changeSecSkill(hero, estatesSkill(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	}

	void acquireInvestor(CGameHandler & handler, CGHeroInstance * hero)
	{
		prepareForEstatesProgression(handler, hero);
		const auto estates = estatesSkill();
		handler.levelUpHero(hero, estates, false);
		ASSERT_EQ(hero->getSecSkillLevel(estates), MasteryLevel::BASIC);
		ASSERT_TRUE(selectOfferedPerk(handler, hero, TAX_COLLECTOR, MasteryLevel::BASIC));
		ASSERT_TRUE(hero->hasActivePerk(ESTATES_SKILL, TAX_COLLECTOR));

		EXPECT_FALSE(hasOfferedPerk(hero, INVESTOR))
			<< "Investor must not be offered before Advanced Estates";
		handler.levelUpHero(hero, estates, false);
		ASSERT_EQ(hero->getSecSkillLevel(estates), MasteryLevel::ADVANCED);
		ASSERT_TRUE(selectOfferedPerk(handler, hero, INVESTOR, MasteryLevel::ADVANCED));
		ASSERT_TRUE(hero->hasActivePerk(ESTATES_SKILL, INVESTOR));
	}

	void acquireInvestorAndFinancier(CGameHandler & handler, CGHeroInstance * hero)
	{
		acquireInvestor(handler, hero);
		const auto estates = estatesSkill();
		handler.levelUpHero(hero, estates, false);
		ASSERT_EQ(hero->getSecSkillLevel(estates), MasteryLevel::EXPERT);
		ASSERT_TRUE(selectOfferedPerk(handler, hero, FINANCIER, MasteryLevel::EXPERT));
		ASSERT_TRUE(hero->hasActivePerk(ESTATES_SKILL, FINANCIER));
	}

	void setGold(CGameHandler & handler, int64_t amount)
	{
		auto * playerState = gameState()->getPlayerState(PLAYER);
		if(amount < 0)
		{
			// Negative balances are not obtainable through the ordinary resource
			// command, but exercise Investor's defensive treasury floor.
			playerState->resources[EGameResID::GOLD] = amount;
			return;
		}

		ResourceSet change;
		change[EGameResID::GOLD] = amount - playerState->resources[EGameResID::GOLD];
		handler.giveResources(PLAYER, change);
	}

	void advanceToDay(CGameHandler & handler, uint32_t day)
	{
		while(gameState()->day < day)
			handler.onNewTurn();
		ASSERT_EQ(gameState()->day, day);
	}

	static int64_t packetGold(const NewTurn & turn)
	{
		const auto it = turn.playerIncome.find(PLAYER);
		return it == turn.playerIncome.end() ? 0 : it->second[EGameResID::GOLD];
	}

	int64_t ordinaryDailyGoldIncome() const
	{
		int64_t income = 0;
		const auto * playerState = gameState()->getPlayerState(PLAYER);
		for(const auto * object : playerState->getOwnedObjects())
			income += object->asOwnable()->dailyIncome()[EGameResID::GOLD];
		return income;
	}

	void expectSnapshot(CGHeroInstance * hero, RecordingGameServer & server,
		int32_t previousValue, int32_t expected)
	{
		ASSERT_TRUE(server.lastNewTurn);
		EXPECT_EQ(hero->getNewHorizonsInvestorDailyGold(), expected);
		const auto & updates = server.lastNewTurn->newHorizonsInvestorDailyGold;
		const auto found = updates.find(hero->id);
		if(previousValue != expected)
		{
			ASSERT_NE(found, updates.end()) << "A changed hero snapshot must be published in NewTurn";
			EXPECT_EQ(found->second, expected);
		}
		else if(found != updates.end())
			EXPECT_EQ(found->second, expected);
	}

	CGHeroInstance * firstHero = nullptr;
	CGHeroInstance * secondHero = nullptr;
};
}

TEST_F(NewHorizonsEstatesInvestorTest, LegalAdvancedOfferFloorsNegativeTreasuryAndCapsWeeklySnapshot)
{
	startGame();
	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	acquireInvestor(handler, firstHero);

	struct Sample
	{
		uint32_t beforeWeekStart;
		int64_t treasury;
		int32_t expectedDailyGold;
	};
	const std::array samples = {
		Sample{0, -1, 0},
		Sample{7, 4999, 0},
		Sample{14, 5000, 50},
		Sample{21, 9999, 50},
		Sample{28, 24999, 200},
		Sample{35, 25000, 250}
	};

	for(const auto & sample : samples)
	{
		SCOPED_TRACE(sample.beforeWeekStart);
		advanceToDay(handler, sample.beforeWeekStart);
		setGold(handler, sample.treasury);
		const int32_t previous = firstHero->getNewHorizonsInvestorDailyGold();
		handler.onNewTurn();
		ASSERT_EQ(gameState()->day, sample.beforeWeekStart + 1);
		expectSnapshot(firstHero, server, previous, sample.expectedDailyGold);
		const int64_t baseDailyGold = firstHero->dailyIncomeWithInvestorGold(0)[EGameResID::GOLD];
		EXPECT_EQ(firstHero->dailyIncome()[EGameResID::GOLD], baseDailyGold + sample.expectedDailyGold);
	}
}

TEST_F(NewHorizonsEstatesInvestorTest, FirstWeekSeedsDailyIncomeWithoutASeparateFirstDayGrant)
{
	startGame();
	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	acquireInvestor(handler, firstHero);
	setGold(handler, 25000);

	EXPECT_EQ(firstHero->getNewHorizonsInvestorDailyGold(), 0);
	handler.onNewTurn(); // Day zero -> one snapshots the week, but skips regular income.
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 1u);
	expectSnapshot(firstHero, server, 0, 250);
	EXPECT_EQ(packetGold(*server.lastNewTurn), 0);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], 25000);
	const int64_t baseDailyGold = firstHero->dailyIncomeWithInvestorGold(0)[EGameResID::GOLD];
	EXPECT_EQ(firstHero->dailyIncomeWithInvestorGold(250)[EGameResID::GOLD], baseDailyGold + 250);

	handler.onNewTurn(); // The snapshot joins ordinary daily income from day two onward.
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 2u);
	EXPECT_EQ(packetGold(*server.lastNewTurn), ordinaryDailyGoldIncome());
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD],
		25000 + ordinaryDailyGoldIncome());
}

TEST_F(NewHorizonsEstatesInvestorTest, MidweekBalanceIsFixedAndNextWeekUsesPreFinancierTreasury)
{
	startGame();
	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	acquireInvestorAndFinancier(handler, firstHero);
	setGold(handler, 5000);

	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	expectSnapshot(firstHero, server, 0, 50);

	setGold(handler, 25000);
	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(firstHero->getNewHorizonsInvestorDailyGold(), 50)
		<< "Changing treasury midweek must not recalculate the current snapshot";
	advanceToDay(handler, 7);
	setGold(handler, 4999);

	const auto previous = firstHero->getNewHorizonsInvestorDailyGold();
	handler.onNewTurn(); // The 49-Gold Financier receipt must not change the Investor sample.
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 8u);
	expectSnapshot(firstHero, server, previous, 0);
	EXPECT_EQ(packetGold(*server.lastNewTurn), ordinaryDailyGoldIncome() + 49);
	EXPECT_EQ(firstHero->dailyIncome()[EGameResID::GOLD],
		firstHero->dailyIncomeWithInvestorGold(0)[EGameResID::GOLD]);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD],
		4999 + ordinaryDailyGoldIncome() + 49);
}

TEST_F(NewHorizonsEstatesInvestorTest, SameTreasurySeedsEveryHolderAndAnUnownedHeroIsCleared)
{
	startGame();
	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	acquireInvestor(handler, firstHero);
	acquireInvestor(handler, secondHero);
	setGold(handler, 25000);
	const auto pooledHeroTypes = gameState()->getMap().getHeroesInPool();
	ASSERT_FALSE(pooledHeroTypes.empty());
	auto * pooledHero = gameState()->getMap().tryGetFromHeroPool(pooledHeroTypes.front());
	ASSERT_NE(pooledHero, nullptr);
	pooledHero->setNewHorizonsInvestorDailyGold(250); // Seed stale pooled state from a prior week.

	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(pooledHero->getNewHorizonsInvestorDailyGold(), 0)
		<< "A pooled hero is not an owned weekly holder and must not carry last week's snapshot";
	expectSnapshot(firstHero, server, 0, 250);
	expectSnapshot(secondHero, server, 0, 250);
	EXPECT_EQ(server.lastNewTurn->newHorizonsInvestorDailyGold.at(firstHero->id), 250);
	EXPECT_EQ(server.lastNewTurn->newHorizonsInvestorDailyGold.at(secondHero->id), 250)
		<< "Both snapshots must use the same pre-income treasury";

	handler.setOwner(secondHero, PlayerColor::NEUTRAL);
	EXPECT_EQ(secondHero->getOwner(), PlayerColor::NEUTRAL);
	advanceToDay(handler, 7);
	setGold(handler, 5000);
	const auto firstPrevious = firstHero->getNewHorizonsInvestorDailyGold();
	const auto secondPrevious = secondHero->getNewHorizonsInvestorDailyGold();
	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	expectSnapshot(firstHero, server, firstPrevious, 50);
	expectSnapshot(secondHero, server, secondPrevious, 0);
}

TEST_F(NewHorizonsEstatesInvestorTest, HeroAndNewTurnSnapshotsRoundTripAndRejectLossyFormats)
{
	startGame();
	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	acquireInvestor(handler, firstHero);
	setGold(handler, 25000);
	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	ASSERT_EQ(firstHero->getNewHorizonsInvestorDailyGold(), 250);

	const auto restoredHero = CMemorySerializer::deepCopy(*firstHero, gameState().get());
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getNewHorizonsInvestorDailyGold(), 250);
	// This standalone deep copy preserves saved perk/snapshot data but does not
	// rehydrate the hero's bonus graph; compare income against its own baseline.
	const int64_t restoredBaseDailyGold = restoredHero->dailyIncomeWithInvestorGold(0)[EGameResID::GOLD];
	EXPECT_EQ(restoredHero->dailyIncome()[EGameResID::GOLD], restoredBaseDailyGold + 250);

	NewTurn currentPacket = *server.lastNewTurn;
	CMemorySerializer packetWire;
	packetWire.oser.version = ESerializationVersion::CURRENT;
	packetWire.iser.version = ESerializationVersion::CURRENT;
	packetWire.oser & currentPacket;
	NewTurn packetCopy;
	packetWire.iser & packetCopy;
	ASSERT_EQ(packetCopy.newHorizonsInvestorDailyGold.size(), 1u);
	EXPECT_EQ(packetCopy.newHorizonsInvestorDailyGold.at(firstHero->id), 250);

	CMemorySerializer oldPacketWriter;
	oldPacketWriter.oser.version = ESerializationVersion::BATTLE_FINAL_RELOCATION;
	EXPECT_THROW(oldPacketWriter.oser & currentPacket, std::runtime_error);
	EXPECT_TRUE(oldPacketWriter.extractBuffer().empty())
		<< "An older packet writer must reject Investor snapshots before writing bytes";

	NewTurn legacyPacket;
	CMemorySerializer legacyPacketWire;
	legacyPacketWire.oser.version = ESerializationVersion::BATTLE_FINAL_RELOCATION;
	legacyPacketWire.iser.version = ESerializationVersion::BATTLE_FINAL_RELOCATION;
	legacyPacketWire.oser & legacyPacket;
	NewTurn legacyPacketCopy;
	legacyPacketWire.iser & legacyPacketCopy;
	EXPECT_TRUE(legacyPacketCopy.newHorizonsInvestorDailyGold.empty());

	CMemorySerializer oldHeroWriter;
	oldHeroWriter.oser.version = ESerializationVersion::BATTLE_FINAL_RELOCATION;
	EXPECT_THROW(oldHeroWriter.oser & *firstHero, std::runtime_error);
	EXPECT_TRUE(oldHeroWriter.extractBuffer().empty())
		<< "An older hero save must reject its nonzero Investor snapshot before bytes";

	firstHero->setNewHorizonsInvestorDailyGold(0);
	CMemorySerializer oldHeroWire;
	oldHeroWire.oser.version = ESerializationVersion::BATTLE_FINAL_RELOCATION;
	oldHeroWire.iser.version = ESerializationVersion::BATTLE_FINAL_RELOCATION;
	oldHeroWire.iser.cb = gameState().get();
	oldHeroWire.oser & firstHero;
	std::unique_ptr<CGHeroInstance> legacyHeroCopy;
	oldHeroWire.iser & legacyHeroCopy;
	ASSERT_NE(legacyHeroCopy, nullptr);
	EXPECT_EQ(legacyHeroCopy->getNewHorizonsInvestorDailyGold(), 0);
	firstHero->setNewHorizonsInvestorDailyGold(250);

	NewTurn malformed;
	malformed.newHorizonsInvestorDailyGold[firstHero->id] = 25;
	CMemorySerializer malformedWire;
	malformedWire.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(malformedWire.oser & malformed, std::runtime_error);
	EXPECT_TRUE(malformedWire.extractBuffer().empty());
	EXPECT_THROW(firstHero->setNewHorizonsInvestorDailyGold(-50), std::runtime_error);
	EXPECT_EQ(firstHero->getNewHorizonsInvestorDailyGold(), 250);
}
