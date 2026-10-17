/*
 * TerrainLimiterSerializationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/TerrainHandler.h"
#include "../../lib/bonuses/BonusEvaluationContext.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/json/JsonBonus.h"
#include <stdexcept>

namespace
{
class TerrainLimiterSerializationTest : public BattleTestFixture
{
protected:
	CStack * subject = nullptr;
	Bonus input{BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 7, BonusSourceID()};
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		startBattle();
		subject = addStack(BattleSide::ATTACKER, CreatureID(0), BattleHex(leftHex), 1);
		ASSERT_NE(subject, nullptr);
	}
	ILimiter::EDecision decision(const ILimiter & limiter)
	{
		BonusList accepted, pending;
		const BonusEvaluationContext context(*subject);
		return limiter.limit({input, *subject, accepted, pending, context});
	}
	std::shared_ptr<Bonus> roundtrip(const Bonus & source)
	{
		// Public exported bonus parser; do not require an internal parseLimiter symbol.
		return JsonUtils::parseBonus(source.toJsonNode());
	}
};

TEST_F(TerrainLimiterSerializationTest, NativeSentinelUsesExistingPredefinedDialectBeforeAnyEntityLookup)
{
	const TerrainLimiter native;
	EXPECT_EQ(native.toString(), "CreatureTerrainLimiter(terrainType=native)");
	EXPECT_EQ(native.toJsonNode(), JsonNode("CREATURE_NATIVE_TERRAIN"));
	EXPECT_EQ(native.terrainType, TerrainId(ETerrainId::NATIVE_TERRAIN));
}

TEST_F(TerrainLimiterSerializationTest, NativeBonusRoundtripRetainsRuleAndActualBoundDecision)
{
	auto source = input;
	source.limiter = std::make_shared<TerrainLimiter>();
	const auto encoded = source.toJsonNode();
	const auto parsed = roundtrip(source);
	ASSERT_NE(parsed, nullptr);
	ASSERT_NE(parsed->limiter, nullptr);
	const auto * native = dynamic_cast<const TerrainLimiter *>(parsed->limiter.get());
	ASSERT_NE(native, nullptr);
	EXPECT_EQ(native->terrainType, TerrainId(ETerrainId::NATIVE_TERRAIN));
	EXPECT_EQ(parsed->toJsonNode(), encoded);
	EXPECT_EQ(decision(*parsed->limiter), decision(*source.limiter));
}

TEST_F(TerrainLimiterSerializationTest, ExplicitRegularTerrainStringAndJsonKeepExactExistingShape)
{
	const TerrainId grass(ETerrainId::GRASS);
	const TerrainLimiter regular(grass);
	const auto key = LIBRARY->terrainTypeHandler->getById(grass)->getJsonKey();
	JsonNode expected;
	expected["type"].String() = "CREATURE_TERRAIN_LIMITER";
	expected["parameters"].Vector().emplace_back(key);
	EXPECT_EQ(regular.toJsonNode(), expected);
	EXPECT_EQ(regular.toString(), "CreatureTerrainLimiter(terrainType=" + key + ")");
	auto source = input;
	source.limiter = std::make_shared<TerrainLimiter>(grass);
	const auto parsed = roundtrip(source);
	ASSERT_NE(parsed, nullptr);
	ASSERT_NE(parsed->limiter, nullptr);
	EXPECT_EQ(parsed->limiter->toJsonNode(), expected);
}

TEST_F(TerrainLimiterSerializationTest, AllOfNativeCreatureBonusRoundtripRetainsBothLimiters)
{
	auto limiter = std::make_shared<AllOfLimiter>();
	limiter->add(std::make_shared<TerrainLimiter>());
	limiter->add(std::make_shared<CreatureLevelLimiter>());
	auto source = input;
	source.limiter = limiter;
	const auto encoded = source.toJsonNode();
	const auto & children = encoded["limiters"].Vector();
	ASSERT_EQ(children.size(), 3);
	EXPECT_EQ(children[0], JsonNode("allOf"));
	EXPECT_EQ(children[1], JsonNode("CREATURE_NATIVE_TERRAIN"));
	const auto parsed = roundtrip(source);
	ASSERT_NE(parsed, nullptr);
	ASSERT_NE(parsed->limiter, nullptr);
	EXPECT_EQ(parsed->toJsonNode(), encoded);
	EXPECT_EQ(decision(*parsed->limiter), decision(*limiter));
}

TEST_F(TerrainLimiterSerializationTest, RealAliveBattleBonusSnapshotsRetainAllNativeEffectsAndRepeatExactly)
{
	const auto original = subject->save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	size_t nativeCount = 0;
	const auto snapshot = [&]
	{
		JsonNode result;
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			if(!unit->alive())
				continue;
			JsonNode entry;
			entry["id"].Integer() = unit->unitId();
			const auto bonuses = unit->getAllBonuses(CSelector([](const Bonus *) { return true; }));
			for(const auto & bonus : *bonuses)
			{
				entry["bonuses"].Vector().push_back(bonus->toJsonNode());
				if(bonus->source == BonusSource::TERRAIN_NATIVE)
					++nativeCount;
			}
			EXPECT_EQ(entry["bonuses"].Vector().size(), bonuses->size());
			result.Vector().push_back(std::move(entry));
		}
		return result;
	};
	const auto before = snapshot();
	const auto firstNativeCount = nativeCount;
	ASSERT_GT(firstNativeCount, 0);
	EXPECT_EQ(snapshot(), before);
	EXPECT_EQ(nativeCount, 2 * firstNativeCount);
	EXPECT_EQ(subject->save(), original);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}

TEST_F(TerrainLimiterSerializationTest, OtherNegativeTerrainIdsStillRefuseRatherThanBecomingNativeOrFallback)
{
	for(const int value : {-1, -3, -12345})
	{
		const TerrainLimiter invalid{TerrainId(value)};
		EXPECT_THROW(invalid.toString(), std::runtime_error);
		EXPECT_THROW(invalid.toJsonNode(), std::runtime_error);
	}
}
}
