/*
 * NewHorizonsLeadershipAdmissionTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include <gtest/gtest.h>

#include "../../lib/GameConstants.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../server/CGameHandler.h"
#include "../../server/IGameServer.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyH3MBuilder.h"
#include "../mock/TinyMapGameTest.h"

namespace
{
class LeadershipRecordingServer final : public IGameServer
{
public:
	explicit LeadershipRecordingServer(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{}

	void setState(EServerState value) override { serverState = value; }
	EServerState getState() const override { return serverState; }
	bool isPlayerHost(const PlayerColor &) const override { return true; }
	bool hasPlayerAt(PlayerColor player, GameConnectionID connection) const override
	{
		return player == PlayerColor(0) && connection == GameConnectionID::FIRST_CONNECTION;
	}
	bool hasBothPlayersAtSameConnection(PlayerColor, PlayerColor) const override { return false; }

	void applyPack(CPackForClient & pack) override
	{
		if(dynamic_cast<SystemMessage *>(&pack))
			++systemMessages;
		state->apply(pack);
	}

	void sendPack(CPackForClient & pack, GameConnectionID) override
	{
		if(const auto * applied = dynamic_cast<const PackageApplied *>(&pack))
			responses.push_back(*applied);
	}

	std::vector<PackageApplied> responses;
	int systemMessages = 0;

private:
	EServerState serverState = EServerState::GAMEPLAY;
	std::shared_ptr<CGameState> state;
};

class NewHorizonsLeadershipAdmissionTest : public TinyMapGameTest
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
    }

    static HeroTypeID heroType(const char * id)
    {
        return HeroTypeID(HeroTypeID::decode(id));
    }

    static FactionID faction(const char * id)
    {
        return FactionID(FactionID::decode(id));
    }
};
}

TEST_F(NewHorizonsLeadershipAdmissionTest, JoiningArmyClampsDuplicateStacksAndLeavesFullArmyRemainder)
{
    const CreatureID pikeman(CreatureID::decode("core:pikeman"));
    const CreatureID archer(CreatureID::decode("core:archer"));
    const CreatureID swordsman(CreatureID::decode("core:swordsman"));
    const CreatureID griffin(CreatureID::decode("core:griffin"));
    const CreatureID monk(CreatureID::decode("core:monk"));
    const CreatureID cavalier(CreatureID::decode("core:cavalier"));
    const CreatureID angel(CreatureID::decode("core:angel"));
    const CreatureID centaur(CreatureID::decode("core:centaur"));

    TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
    builder.size(48, false).playerActive(PlayerColor(0))
        .town({12, 12, 0}, faction("core:castle"), PlayerColor(0))
        // Duplicate incoming types are legal in an armed source.  The archer
        // is deliberately absent from the full destination army, exercising
        // the manual garrison fallback as well.
        .townGarrison({{pikeman, 3}, {pikeman, 4}, {archer, 2}})
        .hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
        .heroGarrison({
            {pikeman, 16}, {swordsman, 1}, {griffin, 1}, {monk, 1},
            {cavalier, 1}, {angel, 1}, {centaur, 1}
        });
    startWithMap(std::move(builder));

    auto * hero = findHeroByOwner(PlayerColor(0));
    auto * source = findFirst<CGTownInstance>();
    ASSERT_NE(hero, nullptr);
    ASSERT_NE(source, nullptr);

    const auto pikemanCapacity = hero->getLeadershipSlotCapacity(pikeman);
    ASSERT_TRUE(pikemanCapacity);
    ASSERT_EQ(pikemanCapacity->maximum, 17);
    ASSERT_EQ(hero->stacksCount(), GameConstants::ARMY_SIZE);
    ASSERT_EQ(source->stacksCount(), 3);

    GameHandlerTestServer server(gameState(), PlayerColor(0));
    CGameHandler gameHandler(server, gameState());

    // A full-count transfer would exceed the existing stack's Leadership
    // limit.  The authoritative join path must take only the one legal unit,
    // carry that clamp across the duplicate second pikeman stack, and leave
    // the unmatched archer for the garrison fallback.
    gameHandler.tryJoiningArmy(source, hero, false, true);

    EXPECT_EQ(hero->getStackCount(SlotID(0)), 17);
    EXPECT_EQ(source->getStackCount(SlotID(0)), 2);
    EXPECT_EQ(source->getStackCount(SlotID(1)), 4);
    EXPECT_EQ(source->getStackCount(SlotID(2)), 2);
    EXPECT_EQ(hero->stacksCount(), GameConstants::ARMY_SIZE);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, FreeTierOneDwellingKeepsUnitsAboveRemainingCapacity)
{
    TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
    builder.size(36, false).playerActive(PlayerColor(0))
        // CREATURE_GENERATOR1 subtype 56 is the core Pikeman dwelling, a
        // guaranteed tier-one source for the free recruitment path.
        .dwelling({12, 12, 0}, MapObjectSubID(56), PlayerColor(0))
        .hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0));
    startWithMap(std::move(builder));

    auto * hero = findHeroByOwner(PlayerColor(0));
    auto * dwelling = findFirst<CGDwelling>();
    ASSERT_NE(hero, nullptr);
    ASSERT_NE(dwelling, nullptr);
    ASSERT_FALSE(dwelling->creatures.empty());
    ASSERT_FALSE(dwelling->creatures.front().second.empty());

    const CreatureID creature = dwelling->creatures.front().second.front();
    ASSERT_EQ(creature.toCreature()->getLevel(), 1);
    const auto capacity = hero->getLeadershipSlotCapacity(creature);
    ASSERT_TRUE(capacity);

    hero->clearSlots();
    ASSERT_TRUE(hero->setCreature(SlotID(0), creature, capacity->maximum - 1));
    dwelling->creatures.front().first = 3;

    GameHandlerTestServer server(gameState(), PlayerColor(0));
    CGameHandler gameHandler(server, gameState());
    static_cast<const IObjectInterface *>(dwelling)->blockingDialogAnswered(gameHandler, hero, 1);

    EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
    EXPECT_EQ(dwelling->creatures.front().first, 2u);
}

TEST_F(NewHorizonsLeadershipAdmissionTest, ArrangeStacksRejectsOverCapacityAtomicallyAndAcknowledgesFailure)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));

	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, heroType("core:christian"), PlayerColor(0))
		.heroGarrison({{pikeman, 17}, {pikeman, 2}});
	startWithMap(std::move(builder));

	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto capacity = hero->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	ASSERT_EQ(hero->getStackCount(SlotID(1)), 2);

	LeadershipRecordingServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameState()->actingPlayers.insert(PlayerColor(0));

	ArrangeStacks rejected(2, SlotID(1), SlotID(0), hero->id, hero->id, 0);
	rejected.player = PlayerColor(0);
	rejected.requestID = 41;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, rejected);

	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_EQ(hero->getStackCount(SlotID(1)), 2);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_FALSE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 1);

	// A legal merge remains a normal authoritative mutation after the rejected
	// request; this guards against a client-side rollback that would block valid
	// transfers or discard the source stack.
	hero->setStackCount(SlotID(0), capacity->maximum - 2);
	server.responses.clear();
	server.systemMessages = 0;

	ArrangeStacks accepted(2, SlotID(1), SlotID(0), hero->id, hero->id, 0);
	accepted.player = PlayerColor(0);
	accepted.requestID = 42;
	gameHandler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, accepted);

	EXPECT_EQ(hero->getStackCount(SlotID(0)), capacity->maximum);
	EXPECT_FALSE(hero->hasStackAtSlot(SlotID(1)));
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.systemMessages, 0);
}
