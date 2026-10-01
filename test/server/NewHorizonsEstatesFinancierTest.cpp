/*
 * NewHorizonsEstatesFinancierTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "../../lib/CPlayerState.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/ResourceSet.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
const PlayerColor PLAYER(0);
constexpr auto ESTATES_SKILL = "new-horizons:estates";
constexpr auto TAX_COLLECTOR = "new-horizons:estates.taxCollector";
constexpr auto ESTATE_NETWORK = "new-horizons:estates.estateNetwork";
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

class NewHorizonsEstatesFinancierTest : public TinyMapGameTest
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

		// Financier is still pending in the live registry. Activate just this
		// perk in the saved map rules so these tests exercise its runtime path.
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : perkRules["skills"][ESTATES_SKILL]["perks"].Vector())
			if(perk["id"].String() == FINANCIER)
				perk["effect"]["status"].String() = "active";
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

	static bool offerContains(const std::vector<newHorizonsHeroes::PerkOfferCandidate> & offer,
		std::string_view perkId)
	{
		return std::any_of(offer.begin(), offer.end(), [perkId](const auto & candidate)
		{
			return candidate.selection.perkId == perkId;
		});
	}

	static bool hasOfferedPerk(const CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 2048; ++seed)
			if(offerContains(hero->getPerkState().prepareOffer(rankLookup, seed), perkId))
				return true;
		return false;
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

	void prepareForEstatesProgression(CGameHandler & handler, CGHeroInstance * hero)
	{
		const auto estates = estatesSkill();
		// Establish a clean rank-zero starting point through the authoritative
		// skill-change path, then use ordinary rank advancement below.
		handler.changeSecSkill(hero, estates, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	}

	void advanceToAdvancedEstates(CGameHandler & handler, CGHeroInstance * hero)
	{
		const auto estates = estatesSkill();
		handler.levelUpHero(hero, estates, false);
		ASSERT_EQ(hero->getSecSkillLevel(estates), MasteryLevel::BASIC);
		ASSERT_TRUE(selectOfferedPerk(handler, hero, TAX_COLLECTOR, MasteryLevel::BASIC));
		ASSERT_TRUE(hero->hasActivePerk(ESTATES_SKILL, TAX_COLLECTOR));

		handler.levelUpHero(hero, estates, false);
		ASSERT_EQ(hero->getSecSkillLevel(estates), MasteryLevel::ADVANCED);
		ASSERT_TRUE(selectOfferedPerk(handler, hero, ESTATE_NETWORK, MasteryLevel::ADVANCED));
		ASSERT_TRUE(hero->hasActivePerk(ESTATES_SKILL, ESTATE_NETWORK));
	}

	void advanceToExpertEstates(CGameHandler & handler, CGHeroInstance * hero)
	{
		const auto estates = estatesSkill();
		handler.levelUpHero(hero, estates, false);
		ASSERT_EQ(hero->getSecSkillLevel(estates), MasteryLevel::EXPERT);
	}

	void acquireFinancier(CGameHandler & handler, CGHeroInstance * hero)
	{
		prepareForEstatesProgression(handler, hero);
		advanceToAdvancedEstates(handler, hero);
		advanceToExpertEstates(handler, hero);
		ASSERT_TRUE(selectOfferedPerk(handler, hero, FINANCIER, MasteryLevel::EXPERT));
		ASSERT_TRUE(hero->hasActivePerk(ESTATES_SKILL, FINANCIER));
	}

	void setGold(CGameHandler & handler, int64_t amount)
	{
		ResourceSet change;
		change[EGameResID::GOLD] = amount
			- gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD];
		handler.giveResources(PLAYER, change);
	}

	int64_t ordinaryDailyGoldIncome() const
	{
		int64_t income = 0;
		const auto * playerState = gameState()->getPlayerState(PLAYER);
		for(const auto * object : playerState->getOwnedObjects())
			income += object->asOwnable()->dailyIncome()[EGameResID::GOLD];
		return income;
	}

	static int64_t packetGold(const NewTurn & turn)
	{
		const auto it = turn.playerIncome.find(PLAYER);
		return it == turn.playerIncome.end() ? 0 : it->second[EGameResID::GOLD];
	}

	CGHeroInstance * firstHero = nullptr;
	CGHeroInstance * secondHero = nullptr;
};

void setFinancierPlannedInSavedRules(CGHeroInstance * hero)
{
	auto & savedPerks = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
	for(auto & perk : savedPerks.rules["skills"][ESTATES_SKILL]["perks"].Vector())
		if(perk["id"].String() == FINANCIER)
			perk["effect"]["status"].String() = "planned";
	savedPerks.validate();
}

} // namespace

TEST_F(NewHorizonsEstatesFinancierTest, PaysOnFirstAndFollowingWeekStartsButNotOnAnOrdinaryDay)
{
	startGame();
	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	acquireFinancier(handler, firstHero);
	setGold(handler, 199);

	handler.onNewTurn(); // Day zero -> one is the first week start.
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 1u);
	EXPECT_EQ(packetGold(*server.lastNewTurn), 1);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], 200);

	// Nullkiller's getFreeResources consumes this same player-specific callback
	// resource view; no AI turn or alternate weekly forecast is needed here.
	const auto callback = makeCallback(PLAYER);
	EXPECT_EQ(callback->getResourceAmount()[EGameResID::GOLD], 200);

	const int64_t dailyGold = ordinaryDailyGoldIncome();
	handler.onNewTurn(); // Ordinary day two has no Financier interest.
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 2u);
	EXPECT_EQ(packetGold(*server.lastNewTurn), dailyGold);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], 200 + dailyGold);

	while(gameState()->day < 7)
		handler.onNewTurn();
	setGold(handler, 199);
	handler.onNewTurn(); // Day eight starts the next week.
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 8u);
	EXPECT_EQ(packetGold(*server.lastNewTurn), dailyGold + 1);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], 199 + dailyGold + 1);
}

TEST_F(NewHorizonsEstatesFinancierTest, FloorsCapsAndSnapshotsInterestPerActiveHolder)
{
	startGame();
	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	acquireFinancier(handler, firstHero);
	acquireFinancier(handler, secondHero);

	setGold(handler, 999);
	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 1u);
	// Both holders receive floor(999 / 100) from the same pre-turn snapshot:
	// 18 total, rather than compounding the first holder's gain into the second.
	EXPECT_EQ(packetGold(*server.lastNewTurn), 18);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], 1017);

	const int64_t dailyGold = ordinaryDailyGoldIncome();
	while(gameState()->day < 7)
		handler.onNewTurn();
	setGold(handler, 99);
	handler.onNewTurn();	// The 1% calculation floors to zero at this treasury.
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 8u);
	EXPECT_EQ(packetGold(*server.lastNewTurn), dailyGold);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], 99 + dailyGold);

	while(gameState()->day < 14)
		handler.onNewTurn();
	setGold(handler, 200000);
	handler.onNewTurn(); // Each active holder is capped at 1,000 Gold.
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 15u);
	EXPECT_EQ(packetGold(*server.lastNewTurn), dailyGold + 2000);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], 202000 + dailyGold);
}

TEST_F(NewHorizonsEstatesFinancierTest, RequiresExpertRankSelectionAndAnActiveSavedPerkRule)
{
	startGame();
	RecordingGameServer server(gameState());
	CGameHandler handler(server, gameState());
	prepareForEstatesProgression(handler, firstHero);
	prepareForEstatesProgression(handler, secondHero);
	advanceToAdvancedEstates(handler, firstHero);
	advanceToAdvancedEstates(handler, secondHero);

	EXPECT_FALSE(hasOfferedPerk(firstHero, FINANCIER));
	EXPECT_FALSE(hasOfferedPerk(secondHero, FINANCIER));
	advanceToExpertEstates(handler, firstHero);
	advanceToExpertEstates(handler, secondHero);
	EXPECT_FALSE(firstHero->hasActivePerk(ESTATES_SKILL, FINANCIER));
	EXPECT_FALSE(secondHero->hasActivePerk(ESTATES_SKILL, FINANCIER));

	// The second hero's own saved rule snapshot retains the planned status, as
	// happens for a perk that is not live in a saved ruleset.
	setFinancierPlannedInSavedRules(secondHero);
	EXPECT_FALSE(hasOfferedPerk(secondHero, FINANCIER));

	setGold(handler, 199);
	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(packetGold(*server.lastNewTurn), 0);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], 199);

	// A legal accepted Expert offer activates only the first hero. The planned
	// snapshot on the second hero must not stack a second interest payment.
	ASSERT_TRUE(selectOfferedPerk(handler, firstHero, FINANCIER, MasteryLevel::EXPERT));
	ASSERT_TRUE(firstHero->hasActivePerk(ESTATES_SKILL, FINANCIER));
	const int64_t dailyGold = ordinaryDailyGoldIncome();
	while(gameState()->day < 7)
		handler.onNewTurn();
	setGold(handler, 199);
	handler.onNewTurn();
	ASSERT_TRUE(server.lastNewTurn);
	EXPECT_EQ(gameState()->day, 8u);
	EXPECT_EQ(packetGold(*server.lastNewTurn), dailyGold + 1);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD], 199 + dailyGold + 1);
}
