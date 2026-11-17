/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../server/IGameServer.h"
#include "../mock/TinyMapGameTest.h"

namespace armyAdmissionTest
{
class LeadershipRecordingServer final : public IGameServer
{
public:
	struct RecordedInfoWindow
	{
		PlayerColor player;
		EInfoWindowMode type;
		std::string text;
	};

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
		if(const auto * ended = dynamic_cast<PlayerEndsGame *>(&pack))
			playerEndsGamePacks.push_back(*ended);
		if(const auto * ended = dynamic_cast<PlayerEndsTurn *>(&pack))
			playerEndsTurnPacks.push_back(*ended);
		if(const auto * dialog = dynamic_cast<BlockingDialog *>(&pack))
			blockingDialogQuery = dialog->queryID;
		if(const auto * dialog = dynamic_cast<GarrisonDialog *>(&pack))
		{
			++garrisonDialogs;
			garrisonDialogQuery = dialog->queryID;
			garrisonObject = dialog->objid;
			garrisonHero = dialog->hid;
		}
		if(const auto * message = dynamic_cast<SystemMessage *>(&pack))
		{
			++systemMessages;
			systemMessageTexts.push_back(message->text.toString(LIBRARY->generaltexth.get()));
		}
		if(const auto * window = dynamic_cast<InfoWindow *>(&pack))
			infoWindows.push_back({window->player, window->type, window->text.toString(LIBRARY->generaltexth.get())});
		if(dynamic_cast<RebalanceStacks *>(&pack))
			++rebalancePacks;
		if(dynamic_cast<SetStackType *>(&pack))
			++stackTypePacks;
		if(dynamic_cast<SetResources *>(&pack))
			++resourcePacks;
		state->apply(pack);
	}

	void sendPack(CPackForClient & pack, GameConnectionID) override
	{
		if(const auto * applied = dynamic_cast<const PackageApplied *>(&pack))
			responses.push_back(*applied);
		if(const auto * message = dynamic_cast<const SystemMessage *>(&pack))
			targetedSystemMessageTexts.push_back(message->text.toString(LIBRARY->generaltexth.get()));
	}

	std::vector<PackageApplied> responses;
	std::vector<PlayerEndsGame> playerEndsGamePacks;
	std::vector<PlayerEndsTurn> playerEndsTurnPacks;
	std::vector<std::string> systemMessageTexts;
	std::vector<std::string> targetedSystemMessageTexts;
	std::vector<RecordedInfoWindow> infoWindows;
	QueryID blockingDialogQuery = QueryID::NONE;
	QueryID garrisonDialogQuery = QueryID::NONE;
	ObjectInstanceID garrisonObject = ObjectInstanceID::NONE;
	ObjectInstanceID garrisonHero = ObjectInstanceID::NONE;
	int systemMessages = 0;
	int rebalancePacks = 0;
	int stackTypePacks = 0;
	int resourcePacks = 0;
	int garrisonDialogs = 0;

	bool hasLeadershipLimitMessage() const
	{
		const auto isLeadershipLimitMessage = [](const std::string & text)
		{
			return text.find("Leadership limit exceeded") != std::string::npos;
		};
		return std::any_of(systemMessageTexts.begin(), systemMessageTexts.end(), isLeadershipLimitMessage)
			|| std::any_of(targetedSystemMessageTexts.begin(), targetedSystemMessageTexts.end(), isLeadershipLimitMessage);
	}

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
        // Preserve the authored follower counts in these admission regressions.
        loaded->overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT, JsonNode(100));
        loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
            JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
        loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
            JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
    }

    // A read-only offensive threshold, never an army admission limit.
    static std::optional<newHorizonsHeroes::LeadershipStackReadout> coordination(
        const CGHeroInstance & hero, CreatureID creature)
    {
        const auto readout = newHorizonsHeroes::leadershipStackReadout(&hero, creature, 0);
        if(!readout.active) return std::nullopt;
        return readout;
    }

    static void expectOversizedEfficiency(const CGHeroInstance & hero, SlotID slot)
    {
        const auto & stack = hero.getStack(slot);
        const auto readout = newHorizonsHeroes::leadershipStackReadout(
            &hero, stack.getCreatureID(), stack.getCount());
        ASSERT_TRUE(readout.active);
        EXPECT_GT(readout.count, readout.capacity);
        EXPECT_GT(readout.efficiency, 0.0);
        EXPECT_LT(readout.efficiency, 1.0);
        EXPECT_EQ(readout.count, stack.getCount());
    }

    static auto snapshotArmy(const CArmedInstance & army)
    {
        std::vector<std::tuple<int, int, TQuantity>> snapshot;
        for(const auto & [slot, stack] : army.Slots())
            snapshot.emplace_back(slot.getNum(), stack->getCreatureID().getNum(), stack->getCount());
        return snapshot;
    }

    static void expectWizardCoordination(const CGHeroInstance & hero, CreatureID creature,
        const JsonNode & capturedCapabilities)
    {
        EXPECT_EQ(hero.getHeroClassID(), HeroClassID(HeroClassID::decode("core:wizard")));
        EXPECT_EQ(hero.level, 1);
        const auto readout = coordination(hero, creature);
        ASSERT_TRUE(readout);
        EXPECT_EQ(readout->effectiveLeadership, 650);
        EXPECT_EQ(readout->requirement, 300);
        EXPECT_EQ(readout->capacity, 2);
        EXPECT_EQ(hero.getCapabilityRules(), capturedCapabilities);
    }

    static HeroTypeID heroType(const char * id)
    {
        return HeroTypeID(HeroTypeID::decode(id));
    }

    static FactionID faction(const char * id)
    {
        return FactionID(FactionID::decode(id));
    }

    bool startRecruitmentAdmissionMap()
    {
        const PlayerColor player(0);
        const CreatureID pikeman(CreatureID::decode("core:pikeman"));
        TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
        builder.size(36, false).playerActive(player)
            .town({12, 12, 0}, faction("core:castle"), player)
            .hero({5, 5, 0}, heroType("core:christian"), player);
        startWithMap(std::move(builder));

        recruitmentTown = findFirst<CGTownInstance>();
        recruitmentHero = findHeroByOwner(player);
        if(!recruitmentTown || !recruitmentHero)
            return false;

        recruitmentHero->clearSlots();
        recruitmentTown->addBuilding(BuildingID::getDwellingFromLevel(0, 0));
        recruitmentTown->creatures = {{10, {pikeman}}};
        recruitmentTown->setVisitingHero(recruitmentHero);
        gameState()->getPlayerState(player)->resources[EGameResID::GOLD] = 100000;
        gameState()->actingPlayers.insert(player);
        return true;
    }

    CGTownInstance * recruitmentTown = nullptr;
    CGHeroInstance * recruitmentHero = nullptr;
};

class NewHorizonsCampaignUpgradeLeadershipTest : public NewHorizonsLeadershipAdmissionTest,
	public ::testing::WithParamInterface<int> {};

class LegacyLeadershipAdmissionTest : public NewHorizonsLeadershipAdmissionTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsLeadershipAdmissionTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode());
	}
};

class NewHorizonsLeadershipSaveTransferTest : public NewHorizonsLeadershipAdmissionTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
};
}
