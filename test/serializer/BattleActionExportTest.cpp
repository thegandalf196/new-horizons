/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../tools/native/BattleActionExport.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/gameState/ReplayLog.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
ReplayLog log()
{
	ReplayLog result;
	result.configure(true, 1);
	result.beginDay(1, {});
	return result;
}
void start(ReplayLog & replay, int id, bool descriptor = true)
{
	BattleStart packet;
	packet.battleID = BattleID(id);
	if(descriptor)
	{
		packet.info = std::make_unique<BattleInfo>(nullptr);
		packet.info->battleID = packet.battleID;
		packet.info->replayAllowed = false;
		packet.info->battlefieldType = BattleField(BattleField::decode("core:sand_shore"));
		packet.info->terrainType = TerrainId(TerrainId::GRASS);
		packet.info->tile = int3(4, 4, 0);
		packet.info->getSide(BattleSide::ATTACKER).color = PlayerColor(0);
		packet.info->getSide(BattleSide::DEFENDER).color = PlayerColor(1);
	}
	replay.addPack(ReplayPackSerializer::write(packet));
}
void finish(ReplayLog & replay, int id, bool withResult = true)
{
	if(withResult)
	{
		BattleResult result;
		result.battleID = BattleID(id);
		result.winner = BattleSide::ATTACKER;
		replay.addPack(ReplayPackSerializer::write(result));
	}
	BattleEnded ended;
	ended.battleID = BattleID(id);
	ended.victor = PlayerColor(0);
	ended.loser = PlayerColor(1);
	replay.addPack(ReplayPackSerializer::write(ended));
}
std::vector<std::byte> bytes(const ReplayLog & replay)
{
	CMemorySerializer memory;
	memory.oser & replay;
	return memory.extractBuffer();
}
}
TEST(BattleActionExportTest, SelectedIntervalRetainsExplicitOrderAndSeparateCastWithoutApplyingPackets)
{
	auto replay = log();
	start(replay, 3);
	StartAction action(BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	action.battleID = BattleID(3);
	replay.addPack(ReplayPackSerializer::write(action));
	BattleSpellCast cast;
	cast.battleID = BattleID(3);
	cast.spellID = SpellID::MAGIC_ARROW;
	cast.side = BattleSide::DEFENDER;
	cast.counterspellNegated = true;
	cast.counterspellSide = BattleSide::ATTACKER;
	cast.paidCounterspellManaCost = 3;
	cast.paidHeroManaCost = 5;
	cast.affectedCres = {4, 2};
	replay.addPack(ReplayPackSerializer::write(cast));
	finish(replay, 3);
	const auto before = bytes(replay);
	const auto output = battleActionExport::inspect(replay, nullptr, 0);
	ASSERT_TRUE(output["interval"]["complete"].Bool());
	ASSERT_EQ(output["events"].Vector().size(), 5u);
	const auto & intent = output["events"].Vector()[1];
	EXPECT_EQ(intent["command"].Integer(), static_cast<int>(HeroCommand::CHARGE));
	EXPECT_EQ(intent["kind"].String(), "StartAction");
	const auto & announced = output["events"].Vector()[2];
	EXPECT_EQ(announced["kind"].String(), "BattleSpellCast");
	EXPECT_TRUE(announced["counterspellNegated"].Bool());
	EXPECT_EQ(announced["paidHeroManaCost"].Integer(), 5);
	EXPECT_EQ(announced["affectedCres"].Vector()[0].Integer(), 4);
	EXPECT_EQ(intent["packetVersion"].Integer(), static_cast<int>(ESerializationVersion::CURRENT));
	EXPECT_EQ(bytes(replay), before);
	EXPECT_FALSE(output["requestedActionPayloadsRecorded"].Bool());
}
TEST(BattleActionExportTest, SelectionIsAnExplicitRetainedStartIndex)
{
	auto replay = log();
	start(replay, 5);
	finish(replay, 5);
	start(replay, 8);
	finish(replay, 8);
	const auto catalog = battleActionExport::inspect(replay, nullptr, std::nullopt);
	ASSERT_EQ(catalog["intervals"].Vector().size(), 2u);
	const auto selected = battleActionExport::inspect(replay, nullptr, 1);
	EXPECT_EQ(selected["interval"]["battleID"].Integer(), 8);
	EXPECT_EQ(selected["events"].Vector()[0]["position"]["packet"].Integer(), 3);
	EXPECT_THROW(battleActionExport::inspect(replay, nullptr, 2), std::runtime_error);
}
TEST(BattleActionExportTest, IncompleteMissingDescriptorResultOrEndedCannotBecomeExhaustiveEvidence)
{
	for(const int missing : {0, 1, 2})
	{
		auto replay = log();
		start(replay, 0, missing != 0);
		if(missing != 2)
			finish(replay, 0, missing != 1);
		const auto catalog = battleActionExport::inspect(replay, nullptr, std::nullopt);
		ASSERT_EQ(catalog["intervals"].Vector().size(), 1u);
		EXPECT_FALSE(catalog["intervals"].Vector()[0]["complete"].Bool());
		EXPECT_THROW(battleActionExport::inspect(replay, nullptr, 0), std::runtime_error);
	}
}
TEST(BattleActionExportTest, UnknownProvenanceMalformedPayloadAndDuplicateStartsRefuse)
{
	auto unknown = log();
	unknown.addPack({{std::byte{1}}, ESerializationVersion::NONE});
	EXPECT_THROW(battleActionExport::inspect(unknown, nullptr, std::nullopt), std::runtime_error);
	auto malformed = log();
	malformed.addPack({{}, ESerializationVersion::CURRENT});
	EXPECT_THROW(battleActionExport::inspect(malformed, nullptr, std::nullopt), std::runtime_error);
	auto duplicate = log();
	start(duplicate, 0, false);
	start(duplicate, 0, false);
	EXPECT_THROW(battleActionExport::inspect(duplicate, nullptr, std::nullopt), std::runtime_error);
}
TEST(BattleActionExportTest, PacketIntervalAndOutputBudgetsFailClosed)
{
	auto replay = log();
	start(replay, 0);
	finish(replay, 0);
	battleActionExport::Limits limits;
	limits.packets = 1;
	EXPECT_THROW(battleActionExport::inspect(replay, nullptr, 0, limits), std::runtime_error);
	limits = {};
	limits.packetBytes = 1;
	EXPECT_THROW(battleActionExport::inspect(replay, nullptr, 0, limits), std::runtime_error);
	limits = {};
	limits.intervalPackets = 1;
	EXPECT_THROW(battleActionExport::inspect(replay, nullptr, 0, limits), std::runtime_error);
	limits = {};
	limits.outputBytes = 1;
	EXPECT_THROW(battleActionExport::inspect(replay, nullptr, 0, limits), std::runtime_error);
}
TEST(BattleActionExportTest, RequestAcknowledgmentDoesNotInventRequestedActionOrCast)
{
	PackageApplied rejected(PlayerColor(0), 7, 123, false);
	const auto output = battleActionExport::describe(rejected);
	EXPECT_EQ(output["kind"].String(), "PackageApplied");
	EXPECT_FALSE(output["result"].Bool());
	EXPECT_TRUE(output["command"].isNull());
	EXPECT_TRUE(output["spellID"].isNull());
	EXPECT_FALSE(output["association"].String().empty());
}
TEST(BattleActionExportTest, CatalogRetainsOrphansAndEmptyHistoryDoesNotInventAnEncounter)
{
	auto replay = log();
	finish(replay, 0);
	const auto output = battleActionExport::inspect(replay, nullptr, std::nullopt);
	EXPECT_TRUE(output["intervals"].Vector().empty());
	EXPECT_EQ(output["orphanBoundaryCount"].Integer(), 2);
	EXPECT_THROW(battleActionExport::inspect(replay, nullptr, 0), std::runtime_error);
}

TEST(BattleActionExportTest, CancelledBattleIsListedButNotExportedAsComplete)
{
	auto replay = log();
	start(replay, 0);
	BattleCancelled cancelled;
	cancelled.battleID = BattleID(0);
	replay.addPack(ReplayPackSerializer::write(cancelled));
	const auto catalog = battleActionExport::inspect(replay, nullptr, std::nullopt);
	EXPECT_FALSE(catalog["intervals"].Vector()[0]["complete"].Bool());
	EXPECT_THROW(battleActionExport::inspect(replay, nullptr, 0), std::runtime_error);
}
TEST(BattleActionExportTest, ChapterDayOrderCannotConcealAMissingInterval)
{
	auto replay = log();
	replay.beginDay(1, {});
	EXPECT_THROW(battleActionExport::inspect(replay, nullptr, std::nullopt), std::runtime_error);
}

class BattleActionExportContextTest : public BattleTestFixture
{
};
TEST_F(BattleActionExportContextTest, CompleteSelectedIntervalExportsActualNonemptyInitialStackDescriptors)
{
	startGame();
	gameState()->replayLog = ReplayLog();
	gameState()->replayLog.configure(true, 1);
	gameState()->replayLog.beginDay(gameState()->day, gameState()->saveReplaySnapshot());
	startBattle();
	std::map<uint32_t, std::pair<int32_t, int32_t>> expected;
	for(const auto & unit : battle()->stacks)
	{
		ASSERT_GT(unit->getCount(), 0);
		ASSERT_TRUE(unit->getPosition().isValid());
		expected.emplace(unit->unitId(), std::pair{unit->getCount(), unit->getPosition().toInt()});
	}
	ASSERT_FALSE(expected.empty());
	auto source = gameState()->replayLog;
	finish(source, battle()->getBattleID().getNum());
	const auto sourceBefore = bytes(source);
	const auto liveBattles = gameState()->currentBattles.size();
	CGameState historical;
	historical.preInit(LIBRARY);
	const auto output = battleActionExport::inspect(source, &historical, 0);
	ASSERT_TRUE(output["interval"]["complete"].Bool());
	ASSERT_FALSE(output["events"].Vector().empty());
	const auto & initial = output["events"].Vector().front();
	EXPECT_EQ(initial["stackStateKind"].String(), "serialized_initial_descriptor");
	ASSERT_EQ(initial["stacks"].Vector().size(), expected.size());
	for(const auto & unit : initial["stacks"].Vector())
	{
		const auto found = expected.find(static_cast<uint32_t>(unit["unitID"].Integer()));
		ASSERT_NE(found, expected.end());
		EXPECT_EQ(unit["count"].Integer(), found->second.first);
		EXPECT_EQ(unit["hex"].Integer(), found->second.second);
	}
	EXPECT_EQ(historical.currentBattles.size(), 0u);
	EXPECT_EQ(gameState()->currentBattles.size(), liveBattles);
	EXPECT_EQ(bytes(source), sourceBefore);
	EXPECT_EQ(historical.getMapEventDispatcher(), nullptr);
	EXPECT_FALSE(historical.replayLog.isRecordingPacks());
	EXPECT_EQ(output["decodeContext"].String(), "historical snapshot and recorded effects");
}
TEST(BattleActionExportTest, MissingHistoricalSnapshotRefusesInsteadOfUsingFinalWorld)
{
	NativeBonusGraphAccess access;
	auto replay = log();
	CGameState historical;
	historical.preInit(LIBRARY);
	EXPECT_THROW(battleActionExport::inspect(replay, &historical, std::nullopt), std::runtime_error);
}

TEST(BattleActionExportTest, UnsummarizedBattleOwnershipAndInterleavingRemainExplicit)
{
	auto replay = log();
	start(replay, 0);
	start(replay, 9);
	for(const int id : {0, 9})
	{
		BattleStackMoved moved;
		moved.battleID = BattleID(id);
		replay.addPack(ReplayPackSerializer::write(moved));
		BattleUnitsChanged units;
		units.battleID = BattleID(id);
		replay.addPack(ReplayPackSerializer::write(units));
		BattleHeroOrderStateChanged orders;
		orders.battleID = BattleID(id);
		orders.side = BattleSide::ATTACKER;
		orders.states = std::vector<HeroOrderState>{};
		orders.state.reset();
		replay.addPack(ReplayPackSerializer::write(orders));
		BattleResultsApplied applied;
		applied.battleID = BattleID(id);
		applied.victor = PlayerColor(0);
		applied.loser = PlayerColor(1);
		replay.addPack(ReplayPackSerializer::write(applied));
	}
	PackageApplied rejected(PlayerColor(0), 17, 123, false);
	replay.addPack(ReplayPackSerializer::write(rejected));
	SetResources unclassified;
	unclassified.player = PlayerColor(0);
	replay.addPack(ReplayPackSerializer::write(unclassified));
	finish(replay, 9);
	finish(replay, 0);
	const auto output = battleActionExport::inspect(replay, nullptr, 0);
	size_t ownershipOnly = 0;
	for(const auto & event : output["events"].Vector())
	{
		EXPECT_TRUE(event["position"]["ordinal"].isNumber());
		EXPECT_TRUE(event["packetVersion"].isNumber());
		EXPECT_TRUE(event["packetTypeID"].isNumber());
		if(event["detailScope"].isString())
		{
			++ownershipOnly;
			const auto id = event["battleID"].Integer();
			ASSERT_TRUE(id == 0 || id == 9);
			EXPECT_EQ(event["scope"].String(), id == 0 ? "selected_battle" : "other_battle_interleaved");
		}
		else if(event["kind"].String() == "PackageApplied")
		{
			EXPECT_EQ(event["scope"].String(), "interval_context");
			EXPECT_TRUE(event["battleID"].isNull());
			EXPECT_TRUE(event["command"].isNull());
		}
		else if(event["kind"].String() == "other_recorded_packet")
		{
			EXPECT_EQ(event["scope"].String(), "unclassified");
			EXPECT_TRUE(event["battleID"].isNull());
		}
	}
	EXPECT_EQ(ownershipOnly, 8u);
}
