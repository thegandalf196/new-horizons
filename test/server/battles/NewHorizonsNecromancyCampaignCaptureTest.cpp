/*
 * NewHorizonsNecromancyCampaignCaptureTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/StartInfo.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/mapping/CMapInfo.h"
#include "../../../lib/networkPacks/PacksForLobby.h"
#include "../../../lib/serializer/CMemorySerializer.h"

#include <type_traits>

namespace
{
// Inject one captured hero through the real typed codec, without exposing the
// private campaign pools or relaxing production validation.
struct CampaignPolicyPoolWriter
{
	using Version = ESerializationVersion;
	CMemorySerializer & wire;
	JsonNode hero;
	bool global;
	bool saving = true;
	bool hasFeature(Version feature) const { return wire.oser.hasFeature(feature); }
	template<typename T> CampaignPolicyPoolWriter & operator&(T & value)
	{
		if constexpr(std::is_same_v<T, std::map<CampaignScenarioID, std::vector<JsonNode>>>)
		{
			auto pool = value;
			if(!global)
				pool[CampaignScenarioID(0)] = {hero};
			wire.oser & pool;
		}
		else if constexpr(std::is_same_v<T, std::map<HeroTypeID, JsonNode>>)
		{
			auto pool = value;
			if(global)
				pool[HeroTypeID(0)] = hero;
			wire.oser & pool;
		}
		else
			wire.oser & value;
		return *this;
	}
};

template<typename T>
void refusesCampaignBeforePrefix(T & value)
{
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
	try
	{
		value.serialize(old.oser);
		FAIL() << "Expected captured Necromancy policy rejection";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "Cannot discard captured Necromancy policy");
	}
	EXPECT_TRUE(old.extractBuffer().empty());
}
}

class NewHorizonsNecromancyCampaignCaptureTest : public BattleTestFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		JsonNode absent(JsonPath::builtin("config/newHorizonsPerks"));
		absent.Struct().erase(newHorizonsNecromancy::POLICY_KEY);
		absent.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, absent);
	}

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
	}

	JsonNode heroSnapshot(bool current) const
	{
		CampaignState empty;
		auto hero = empty.crossoverSerialize(attackerSideHero);
		if(current)
		{
			JsonNode installed(JsonPath::builtin("config/newHorizonsPerks"));
			hero["perkState"]["rules"][newHorizonsNecromancy::POLICY_KEY] = installed[newHorizonsNecromancy::POLICY_KEY];
		}
		return hero;
	}

	std::shared_ptr<CampaignState> capturedPool(bool global, bool current) const
	{
		CampaignState empty;
		CMemorySerializer bytes;
		CampaignPolicyPoolWriter writer{bytes, heroSnapshot(current), global};
		empty.serialize(writer);
		auto captured = std::make_shared<CampaignState>();
		captured->serialize(bytes.iser);
		return captured;
	}
};

TEST_F(NewHorizonsNecromancyCampaignCaptureTest, BothPoolsGuardDirectAndStartLobbyWritersAndRejectOlderReaders)
{
	for(const bool global : {false, true})
	{
		auto captured = capturedPool(global, true);
		EXPECT_THROW(captured->validateNecromancyPolicySerialization(false), std::runtime_error);
		refusesCampaignBeforePrefix(*captured);
		StartInfo start;
		start.campState = captured;
		refusesCampaignBeforePrefix(start);
		LobbyState lobby;
		lobby.si = std::make_shared<StartInfo>(start);
		refusesCampaignBeforePrefix(lobby);
		LobbyStartGame starting;
		starting.initializedStartInfo = std::make_shared<StartInfo>(start);
		refusesCampaignBeforePrefix(starting);

		// Forge only the writer envelope to prove the real older reader rejects
		// the policy after loading both pools, before realizing any hero.
		CampaignState empty;
		CMemorySerializer forged;
		forged.oser.version = forged.iser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
		CampaignPolicyPoolWriter writer{forged, heroSnapshot(true), global};
		empty.serialize(writer);
		CampaignState refused;
		try
		{
			refused.serialize(forged.iser);
			FAIL() << "Expected captured Necromancy policy rejection";
		}
		catch(const std::runtime_error & error)
		{
			EXPECT_STREQ(error.what(), "Cannot discard captured Necromancy policy");
		}

		auto historical = capturedPool(global, false);
		CMemorySerializer legacy;
		legacy.oser.version = legacy.iser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
		ASSERT_NO_THROW(historical->serialize(legacy.oser));
		CampaignState restored;
		ASSERT_NO_THROW(restored.serialize(legacy.iser));
		EXPECT_NO_THROW(restored.validateNecromancyPolicySerialization(false));
	}
}

TEST_F(NewHorizonsNecromancyCampaignCaptureTest, WorldPreflightSeesBothMetadataCapturesWithPolicyAbsentFromMapHeroes)
{
	ASSERT_FALSE(newHorizonsNecromancy::policy(gameState()->getHeroPerkRules()));
	ASSERT_FALSE(newHorizonsNecromancy::policy(attackerSideHero->getPerkState().rules));
	ASSERT_NE(gameState()->getInitialStartInfo(), gameState()->getStartInfo());
	for(const bool global : {false, true})
		for(const bool initial : {false, true})
		{
			auto * start = initial ? const_cast<StartInfo *>(gameState()->getInitialStartInfo())
				: gameState()->getStartInfo();
			ASSERT_NE(start, nullptr);
			const auto previous = start->campState;
			start->campState = capturedPool(global, true);
			refusesCampaignBeforePrefix(*gameState());
			start->campState = previous;
		}
}

TEST_F(NewHorizonsNecromancyCampaignCaptureTest, LobbyPacketChecksCurrentWorldWithNullOrHistoricalStartInfo)
{
	ASSERT_FALSE(newHorizonsNecromancy::policy(gameState()->getHeroPerkRules()));
	JsonNode installed(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_TRUE(newHorizonsNecromancy::policy(installed));
	const_cast<JsonNode &>(gameState()->getHeroPerkRules()) = installed;
	LobbyStartGame packet;
	packet.initializedGameState = gameState();
	ASSERT_EQ(packet.initializedStartInfo, nullptr);
	refusesCampaignBeforePrefix(packet);
	packet.initializedStartInfo = std::make_shared<StartInfo>();
	packet.initializedStartInfo->campState = capturedPool(false, false);
	ASSERT_NO_THROW(packet.initializedStartInfo->validateNecromancyPolicySerialization(false));
	refusesCampaignBeforePrefix(packet);
}
