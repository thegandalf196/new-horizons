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
#include "../server/battles/BattleStartSnapshotFixture.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/bonuses/BonusGraphQuery.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../server/CGameHandler.h"
#include <algorithm>

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

class LocalExportProbe final : public CBonusSystemNode
{
public:
	LocalExportProbe() : CBonusSystemNode(BonusNodeType::PLAYER) {}
	void reexport() { exportBonuses(); }
};

size_t localPointerCount(const CBonusSystemNode & node, const std::shared_ptr<Bonus> & bonus)
{
	const auto & raw = liveBonusGraph().local(node);
	return std::count(raw.begin(), raw.end(), bonus);
}

std::shared_ptr<Bonus> localSpeedBonus()
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, 7, BonusSourceID());
}

TEST(LocalBonusExportTest, SamePointerReexportIsIdempotentAndOneRemovalClearsDescendant)
{
	LocalExportProbe root;
	CBonusSystemNode child(BonusNodeType::ARMY);
	child.attachTo(root);
	const auto bonus = localSpeedBonus();
	root.addNewBonus(bonus);
	ASSERT_EQ(localPointerCount(root, bonus), 1u);
	ASSERT_EQ(root.getExportedBonusList().size(), 1u);
	EXPECT_EQ(child.valOfBonuses(BonusType::STACKS_SPEED), 7);
	const auto version = child.getTreeVersion();
	root.reexport();
	root.reexport();
	EXPECT_EQ(localPointerCount(root, bonus), 1u);
	EXPECT_EQ(root.getExportedBonusList().size(), 1u);
	EXPECT_EQ(child.getTreeVersion(), version);
	root.removeBonus(bonus);
	EXPECT_EQ(localPointerCount(root, bonus), 0u);
	EXPECT_TRUE(root.getExportedBonusList().empty());
	EXPECT_GT(child.getTreeVersion(), version);
	EXPECT_EQ(child.valOfBonuses(BonusType::STACKS_SPEED), 0);
}

TEST(LocalBonusExportTest, DistinctEqualValuedPointersStillStackAndRemoveIndependently)
{
	LocalExportProbe root;
	CBonusSystemNode child(BonusNodeType::ARMY);
	child.attachTo(root);
	const auto first = localSpeedBonus();
	const auto second = localSpeedBonus();
	ASSERT_NE(first, second);
	root.addNewBonus(first);
	root.addNewBonus(second);
	root.reexport();
	EXPECT_EQ(localPointerCount(root, first), 1u);
	EXPECT_EQ(localPointerCount(root, second), 1u);
	EXPECT_EQ(root.getExportedBonusList().size(), 2u);
	EXPECT_EQ(child.valOfBonuses(BonusType::STACKS_SPEED), 14);
	root.removeBonus(first);
	EXPECT_EQ(child.valOfBonuses(BonusType::STACKS_SPEED), 7);
	root.removeBonus(second);
	EXPECT_EQ(child.valOfBonuses(BonusType::STACKS_SPEED), 0);
}

TEST(LocalBonusExportTest, DecodedFirstExportWorksWithAndWithoutGamestateAutoExport)
{
	for(const bool gamestate : {false, true})
	{
		LocalExportProbe source;
		source.addNewBonus(localSpeedBonus());
		CMemorySerializer wire;
		wire.oser & static_cast<const CBonusSystemNode &>(source);
		LocalExportProbe decoded;
		wire.iser.loadingGamestate = gamestate;
		wire.iser & static_cast<CBonusSystemNode &>(decoded);
		ASSERT_EQ(decoded.getExportedBonusList().size(), 1u);
		const auto bonus = decoded.getExportedBonusList().front();
		EXPECT_EQ(localPointerCount(decoded, bonus), gamestate ? 1u : 0u);
		const auto before = decoded.getTreeVersion();
		decoded.reexport();
		EXPECT_EQ(localPointerCount(decoded, bonus), 1u);
		if(gamestate)
		{
			EXPECT_EQ(decoded.getTreeVersion(), before);
		}
		else
		{
			EXPECT_GT(decoded.getTreeVersion(), before);
		}
		EXPECT_EQ(decoded.valOfBonuses(BonusType::STACKS_SPEED), 7);
		decoded.removeBonus(bonus);
		EXPECT_EQ(localPointerCount(decoded, bonus), 0u);
		EXPECT_EQ(decoded.valOfBonuses(BonusType::STACKS_SPEED), 0);
	}
}

class DoubleLocalValueUpdater final : public IUpdater
{
public:
	std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
		const BonusEvaluationContext &) const override
	{
		auto updated = std::make_shared<Bonus>(*bonus);
		updated->val *= 2;
		return updated;
	}
};

TEST(LocalBonusExportTest, ReexportDoesNotMultiplyUpdaterApplicationsOrMutateSource)
{
	LocalExportProbe root;
	const auto bonus = localSpeedBonus();
	bonus->updater = std::make_shared<DoubleLocalValueUpdater>();
	root.addNewBonus(bonus);
	const auto before = bonus->toJsonNode();
	root.reexport();
	EXPECT_EQ(localPointerCount(root, bonus), 1u);
	EXPECT_EQ(root.valOfBonuses(BonusType::STACKS_SPEED), 14);
	EXPECT_EQ(bonus->toJsonNode(), before);
	root.removeBonus(bonus);
	EXPECT_EQ(root.valOfBonuses(BonusType::STACKS_SPEED), 0);
}

TEST(LocalBonusExportTest, OrdinaryPropagatedAddAndRemovalRemainUnchanged)
{
	// Creature sources propagate down to attached stacks in the red graph.
	CBonusSystemNode root(BonusNodeType::CREATURE);
	CBonusSystemNode child(BonusNodeType::STACK_BATTLE);
	child.attachTo(root);
	const auto bonus = localSpeedBonus();
	bonus->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::STACK_BATTLE);
	root.addNewBonus(bonus);
	EXPECT_EQ(child.valOfBonuses(BonusType::STACKS_SPEED), 7);
	root.removeBonus(bonus);
	EXPECT_EQ(child.valOfBonuses(BonusType::STACKS_SPEED), 0);
}

void checkSingleNativeDefenseAndRemoval(BattleInfo & battle, CStack & unit)
{
	const auto selector = Selector::sourceType()(BonusSource::TERRAIN_NATIVE)
		.And(Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)));
	const auto bonus = battle.getExportedBonusList().getFirst(selector);
	ASSERT_NE(bonus, nullptr);
	EXPECT_EQ(localPointerCount(battle, bonus), 1u);
	EXPECT_EQ(std::count(battle.getExportedBonusList().begin(), battle.getExportedBonusList().end(), bonus), 1);
	const auto baseDefense = unit.unitType()->getDefense(false);
	EXPECT_EQ(unit.getDefense(false), baseDefense + 1);
	battle.removeBonus(bonus);
	EXPECT_EQ(localPointerCount(battle, bonus), 0u);
	EXPECT_EQ(std::count(battle.getExportedBonusList().begin(), battle.getExportedBonusList().end(), bonus), 0);
	EXPECT_EQ(unit.getDefense(false), baseDefense);
}

TEST_F(TerrainLimiterSerializationTest, InProcessBattleStartKeepsOneNativePointerAndOneRemovalClearsItsEffect)
{
	ASSERT_NO_FATAL_FAILURE(checkSingleNativeDefenseAndRemoval(*battle(), *subject));
}

TEST_F(TerrainLimiterSerializationTest, DecodedBattleStartFirstExportKeepsNativeEffectAndOrdinaryRemoval)
{
	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(gameState()->saveToMemory());
	BattleStart outgoing;
	outgoing.battleID = replica->nextBattleID;
	outgoing.info = battleStartFixture::snapshot(*battle(), replica.get());
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = replica.get();
	BattleStart decoded;
	wire.iser & decoded;
	ASSERT_NE(decoded.info, nullptr);
	EXPECT_TRUE(liveBonusGraph().local(*decoded.info).empty());
	RecordingGameServer recording;
	recording.gameState = replica;
	CGameHandler handler(recording, replica);
	handler.sendAndApply(decoded);
	auto * restored = replica->getBattle(outgoing.battleID);
	ASSERT_NE(restored, nullptr);
	auto * restoredUnit = restored->getStack(subject->unitId());
	ASSERT_NE(restoredUnit, nullptr);
	ASSERT_NO_FATAL_FAILURE(checkSingleNativeDefenseAndRemoval(*restored, *restoredUnit));
}
}
