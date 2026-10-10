/*
 * NewHorizonsRecruitersContactsTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/entities/creature/NewHorizonsMusterRules.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/NewHorizonsRecruitersContacts.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../server/queries/CQuery.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../battles/FullGameSnapshotTypes.h"

namespace
{
constexpr PlayerColor PLAYER(0);
CreatureID creature(const char * name) { return CreatureID(CreatureID::decode(name)); }
constexpr auto RECRUITMENT = "new-horizons:recruitment";
constexpr auto CONTACTS = "new-horizons:recruitment.recruiterSContacts";

// Modifier lifetime is confined to the case, including fatal-assert returns.
class ScopedCreatureGrowth
{
	CCreature * definition;
	std::shared_ptr<Bonus> bonus;
public:
	ScopedCreatureGrowth(CreatureID id, int percent)
		: definition(const_cast<CCreature *>(id.toCreature()))
		, bonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CREATURE_GROWTH_PERCENT,
			BonusSource::OTHER, percent, BonusSourceID()))
	{
		definition->addNewBonus(bonus);
	}
	~ScopedCreatureGrowth() { definition->removeBonus(bonus); }
};

class NewHorizonsRecruitersContactsTest : public TinyMapGameTest
{
protected:
	CGHeroInstance * hero = nullptr;
	CGDwelling * dwelling = nullptr;
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		map->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES, JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare(bool selected = true)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(0), PLAYER)
			.hero({9, 9, 0}, HeroTypeID(1), PlayerColor(1))
			.town({12, 12, 0}, FactionID(FactionID::decode("core:castle")), PLAYER)
			.dwelling({8, 8, 0}, MapObjectSubID(56), PLAYER);
		startWithMap(std::move(builder));
		hero = findHeroAt({5, 5, 0});
		for(auto * candidate : findAll<CGDwelling>())
			if(candidate->ID != Obj::TOWN)
				dwelling = candidate;
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(dwelling, nullptr);
		dwelling->clearSlots();
		dwelling->creatures = {{0, {creature("core:pikeman"), creature("core:halberdier")}}};
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(RECRUITMENT)), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		if(selected)
		{
			hero->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.volunteerNetwork"});
			hero->applyPerkSelection({RECRUITMENT, CONTACTS});
		}
		EXPECT_EQ(hero->hasActivePerk(RECRUITMENT, CONTACTS), selected);
	}
	void visit(CGameHandler & handler)
	{
		handler.objectVisited(dwelling, hero);
		const auto query = handler.queries->topQuery(PLAYER);
		ASSERT_NE(query, nullptr);
		ASSERT_EQ(query->getType(), QueryType::BlockingDialog);
		ASSERT_TRUE(handler.queryReply(query->queryID, 0, PLAYER));
		EXPECT_EQ(handler.queries->topQuery(PLAYER), nullptr);
	}
	SetAvailableCreatures stock() const
	{
		const auto calendar = gameState()->getCalendar();
		const int week = newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
		const auto award = newHorizonsRecruitment::recruitersContactsAward(*hero, *dwelling, week);
		if(!award)
			throw std::runtime_error("Expected an eligible Contacts fixture");
		SetAvailableCreatures pack;
		pack.tid = dwelling->id;
		pack.creatures = dwelling->creatures;
		pack.creatures[award->row].first = award->amount;
		pack.recruitersContacts = SetAvailableCreatures::RecruitersContactsReceipt{hero->id, week, award->row, award->amount};
		return pack;
	}
};
}

TEST_F(NewHorizonsRecruitersContactsTest, ActualOwnedVisitAddsNormalWeekOnceAndNextAbsoluteWeekCanTriggerAgain)
{
	prepare();
	const auto alternatives = dwelling->creatures[0].second;
	const auto growth = dwelling->normalWeeklyGrowth(0);
	ASSERT_GT(growth, 0);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_EQ(dwelling->creatures[0].first, growth);
	EXPECT_EQ(dwelling->creatures[0].second, alternatives);
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), 0);
	EXPECT_EQ(hero->getNewHorizonsMusterUsesThisWeek(0), 0);
	EXPECT_EQ(dwelling->getNewHorizonsMusterLastWeek(), -1);
	dwelling->creatures[0].first = 0;
	visit(handler);
	EXPECT_EQ(dwelling->creatures[0].first, 0);
	gameState()->day = 8;
	visit(handler);
	EXPECT_EQ(dwelling->creatures[0].first, growth);
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), 1);
}

TEST_F(NewHorizonsRecruitersContactsTest, MixedRowsReplenishOnlyFirstEligibleEmptyPoolAndKeepUpgradeSharedPool)
{
	prepare();
	dwelling->creatures = {{4, {creature("core:griffin")}},
		{0, {creature("core:pikeman"), creature("core:halberdier")}}, {0, {creature("core:angel")}}};
	const auto original = dwelling->creatures;
	const auto expected = dwelling->normalWeeklyGrowth(1);
	const auto award = newHorizonsRecruitment::recruitersContactsAward(*hero, *dwelling, 0);
	ASSERT_TRUE(award);
	EXPECT_EQ(award->row, 1);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_EQ(dwelling->creatures[0], original[0]);
	EXPECT_EQ(dwelling->creatures[1].first, expected);
	EXPECT_EQ(dwelling->creatures[1].second, original[1].second);
	EXPECT_EQ(dwelling->creatures[2], original[2]);
}

TEST_F(NewHorizonsRecruitersContactsTest, EmptyZeroGrowthDoesNotSpendAndFirstPositiveRowIsChosenAfterIt)
{
	prepare();
	dwelling->creatures = {{0, {creature("core:griffin")}}};
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	{
		ScopedCreatureGrowth zero(creature("core:griffin"), -100);
		ASSERT_EQ(dwelling->normalWeeklyGrowth(0), 0);
		visit(handler);
		EXPECT_EQ(dwelling->creatures[0].first, 0);
		EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), -1);
		dwelling->creatures.push_back({0, {creature("core:pikeman")}});
		const auto award = newHorizonsRecruitment::recruitersContactsAward(*hero, *dwelling, 0);
		ASSERT_TRUE(award);
		EXPECT_EQ(award->row, 1);
		visit(handler);
		EXPECT_EQ(dwelling->creatures[0].first, 0);
		EXPECT_GT(dwelling->creatures[1].first, 0);
	}
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), 0);
}

TEST_F(NewHorizonsRecruitersContactsTest, EnemyOrNeutralAtEntryDoesNotPayOnCapture)
{
	prepare();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	handler.setOwner(dwelling, PlayerColor(1));
	EXPECT_FALSE(newHorizonsRecruitment::recruitersContactsAward(*hero, *dwelling, 0));
	visit(handler);
	EXPECT_EQ(dwelling->getOwner(), PLAYER);
	EXPECT_EQ(dwelling->creatures[0].first, 0);
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), -1);
	handler.setOwner(dwelling, PlayerColor::NEUTRAL);
	visit(handler);
	EXPECT_EQ(dwelling->getOwner(), PLAYER);
	EXPECT_EQ(dwelling->creatures[0].first, 0);
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), -1);
	visit(handler);
	EXPECT_GT(dwelling->creatures[0].first, 0);
}

TEST_F(NewHorizonsRecruitersContactsTest, NoPerkFullPoolAndTownOrSpecialSourcesDoNotConsumeTheWeeklyUse)
{
	prepare(false);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_EQ(dwelling->creatures[0].first, 0);
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), -1);
	hero->applyPerkSelection({RECRUITMENT, "new-horizons:recruitment.volunteerNetwork"});
	hero->applyPerkSelection({RECRUITMENT, CONTACTS});
	dwelling->creatures[0].first = 3;
	visit(handler);
	EXPECT_EQ(dwelling->creatures[0].first, 3);
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), -1);
	const auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	EXPECT_FALSE(newHorizonsRecruitment::recruitersContactsAward(*hero, *town, 0));
	dwelling->creatures[0].first = 0;
	const auto originalType = dwelling->ID;
	for(const auto excluded : {Obj::WAR_MACHINE_FACTORY, Obj::REFUGEE_CAMP})
	{
		dwelling->ID = excluded;
		EXPECT_FALSE(newHorizonsRecruitment::recruitersContactsAward(*hero, *dwelling, 0));
	}
	dwelling->ID = originalType;
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), -1);
}

TEST_F(NewHorizonsRecruitersContactsTest, GrowthModifiersAndOrdinaryWeeklyRefreshUseTheSameProductionCalculation)
{
	prepare();
	ScopedCreatureGrowth doubled(creature("core:pikeman"), 100);
	const auto expected = static_cast<int64_t>(gameState()->getCreatureBaseGrowth(creature("core:pikeman"))) * 2;
	ASSERT_EQ(dwelling->normalWeeklyGrowth(0), expected);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	EXPECT_EQ(dwelling->creatures[0].first, expected);
	dwelling->creatures[0].first = 0;
	gameState()->day = 8;
	static_cast<const IObjectInterface *>(dwelling)->newTurn(handler, *handler.randomizer);
	EXPECT_EQ(dwelling->creatures[0].first, expected);
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), 0);
}

TEST_F(NewHorizonsRecruitersContactsTest, StaleWrongRowWrongGrowthAndUnrelatedStockChangesRejectWithoutMutation)
{
	prepare();
	dwelling->creatures.push_back({0, {creature("core:griffin")}});
	const auto original = dwelling->creatures;
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	const auto unchanged = [&]()
	{
		EXPECT_EQ(dwelling->creatures, original);
		EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), -1);
	};
	auto pack = stock();
	pack.recruitersContacts->week = 1;
	EXPECT_THROW(handler.sendAndApply(pack), std::runtime_error);
	unchanged();
	pack = stock();
	pack.recruitersContacts->row = 1;
	EXPECT_THROW(handler.sendAndApply(pack), std::runtime_error);
	unchanged();
	pack = stock();
	++pack.recruitersContacts->amount;
	++pack.creatures[0].first;
	EXPECT_THROW(handler.sendAndApply(pack), std::runtime_error);
	unchanged();
	pack = stock();
	pack.creatures[1].first = 1;
	EXPECT_THROW(handler.sendAndApply(pack), std::runtime_error);
	unchanged();
	pack = stock();
	handler.sendAndApply(pack);
	EXPECT_THROW(handler.sendAndApply(pack), std::runtime_error);
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), 0);
}

TEST_F(NewHorizonsRecruitersContactsTest, CurrentStockRoundtripOldOptionalDefaultsAndEnclosingTurnRejectsVisitReceipts)
{
	prepare();
	auto pack = stock();
	CMemorySerializer current;
	current.oser & pack;
	SetAvailableCreatures restored;
	current.iser & restored;
	ASSERT_TRUE(restored.recruitersContacts);
	EXPECT_EQ(restored.creatures, pack.creatures);
	EXPECT_EQ(restored.recruitersContacts->hero, hero->id);
	EXPECT_EQ(restored.recruitersContacts->week, 0);
	EXPECT_EQ(restored.recruitersContacts->row, 0);
	EXPECT_EQ(restored.recruitersContacts->amount, pack.recruitersContacts->amount);
	const auto previous = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_RECRUITERS_CONTACTS) - 1);
	CMemorySerializer old;
	old.oser.version = previous;
	EXPECT_THROW(old.oser & pack, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	auto plain = pack;
	plain.recruitersContacts.reset();
	CMemorySerializer legacy;
	legacy.oser.version = previous;
	legacy.iser.version = previous;
	legacy.oser & plain;
	legacy.iser & restored;
	EXPECT_FALSE(restored.recruitersContacts);
	EXPECT_EQ(restored.creatures, plain.creatures);
	NewTurn turn;
	turn.day = 8;
	turn.availableCreatures = {pack};
	for(const auto version : {previous, ESerializationVersion::CURRENT})
	{
		CMemorySerializer writer;
		writer.oser.version = version;
		EXPECT_THROW(writer.oser & turn, std::runtime_error);
		EXPECT_TRUE(writer.extractBuffer().empty());
	}
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	const auto originalDay = gameState()->day;
	EXPECT_THROW(handler.sendAndApply(turn), std::runtime_error);
	EXPECT_EQ(gameState()->day, originalDay);
	EXPECT_EQ(dwelling->creatures[0].first, 0);
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), -1);
}

TEST_F(NewHorizonsRecruitersContactsTest, HeroWorldLobbyAndActualOffMapPoolSaveGuardsPreserveTheWeeklyReceipt)
{
	prepare();
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	const auto previous = static_cast<ESerializationVersion>(static_cast<int>(ESerializationVersion::NEW_HORIZONS_RECRUITERS_CONTACTS) - 1);
	CMemorySerializer unused;
	unused.oser.version = previous;
	EXPECT_NO_THROW(unused.oser & *gameState());
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	visit(handler);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	ASSERT_NE(restored.getHero(hero->id), nullptr);
	EXPECT_EQ(restored.getHero(hero->id)->getNewHorizonsRecruitersContactsLastWeek(), 0);
	const auto reject = [&](auto & value)
	{
		CMemorySerializer writer;
		writer.oser.version = previous;
		EXPECT_THROW(writer.oser & value, std::runtime_error);
		EXPECT_TRUE(writer.extractBuffer().empty());
	};
	reject(*hero);
	reject(*map());
	reject(*gameState());
	reject(lobby);
	const auto pooled = std::dynamic_pointer_cast<CGHeroInstance>(map()->eraseObject(hero->id));
	ASSERT_NE(pooled, nullptr);
	map()->addToHeroPool(pooled);
	ASSERT_EQ(map()->tryGetFromHeroPool(pooled->getHeroTypeID()), hero);
	reject(*map());
	reject(*gameState());
	reject(lobby);
	EXPECT_NO_THROW(gameState()->validateNewHorizonsRecruitersContactsSerialization(true));
	EXPECT_THROW(hero->markNewHorizonsRecruitersContactsUsed(-2), std::runtime_error);
	EXPECT_EQ(hero->getNewHorizonsRecruitersContactsLastWeek(), 0);
}
