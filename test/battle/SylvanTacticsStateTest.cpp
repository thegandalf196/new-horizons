/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/NewHorizonsSylvanTactics.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace
{
using Version = ESerializationVersion;
template<class T> std::vector<std::byte> encode(const T & state, Version version)
{
	CMemorySerializer wire;
	wire.oser.version = version;
	wire.oser & state;
	return wire.extractBuffer();
}
SylvanLuckState decode(const std::vector<std::byte> & bytes, Version version)
{
	CMemorySerializer wire(bytes);
	wire.iser.version = version;
	SylvanLuckState state;
	wire.iser & state;
	return state;
}
}
TEST(SylvanTacticsStateTest, MissingPolicyRetainsLegacyAndPresentInvalidTypesFailClosed)
{
	JsonNode rules;
	EXPECT_FALSE(newHorizonsHeroes::usesSylvanTactics(rules));
	for(const auto & invalid : {JsonNode(), JsonNode(false), JsonNode(0), JsonNode(2), JsonNode(1.0), JsonNode("1")})
	{
		rules["sylvanTacticsPolicy"] = invalid;
		EXPECT_THROW(newHorizonsHeroes::usesSylvanTactics(rules), std::runtime_error);
	}
	rules["sylvanTacticsPolicy"] = JsonNode(1);
	EXPECT_TRUE(newHorizonsHeroes::usesSylvanTactics(rules));
	EXPECT_THROW(newHorizonsHeroes::validateSylvanTacticsSerialization(rules, false), std::runtime_error);
}
TEST(SylvanTacticsStateTest, FleetfootIsConsumedByFirstRealActivationIncludingWait)
{
	SylvanTacticsState state;
	state.enabled = state.fleetfoot = true;
	EXPECT_EQ(state.speedBonus(4), 1);
	state.beginActivation(4, true);
	EXPECT_EQ(state.speedBonus(4), 1);
	state.endActivation();
	EXPECT_EQ(state.speedBonus(4), 0);
	state.beginActivation(4, true);
	EXPECT_EQ(state.speedBonus(4), 0);
	state.nextRound();
	EXPECT_EQ(state.speedBonus(4), 0);
	EXPECT_EQ(state.speedBonus(5), 1);
}
TEST(SylvanTacticsStateTest, DefendRedeploymentIsOneActivationAndDoesNotRearmFleetfoot)
{
	SylvanTacticsState state;
	state.enabled = state.quickRedeployment = state.fleetfoot = true;
	state.beginActivation(1, true);
	state.endActivation();
	state.beginActivation(1, true, true);
	EXPECT_EQ(state.speedBonus(1), 2);
	state.endActivation();
	state.beginActivation(1, true);
	EXPECT_EQ(state.speedBonus(1), 0);
}
TEST(SylvanTacticsStateTest, CrossfireNeedsDifferentShooterAndResetsOnlyAtRoundBoundary)
{
	SylvanTacticsState state;
	state.enabled = state.crossfire = true;
	state.finishStrike(1, {10}, {}, {}, false);
	EXPECT_EQ(state.crossfireBonus(1, 10), 0);
	EXPECT_EQ(state.crossfireBonus(2, 10), 15);
	EXPECT_EQ(state.crossfireBonus(2, 11), 0);
	state.beginActivation(2, true);
	EXPECT_EQ(state.crossfireBonus(2, 10), 15);
	state.nextRound();
	EXPECT_EQ(state.crossfireBonus(2, 10), 0);
}
TEST(SylvanTacticsStateTest, SharedGuardRefreshesAndExpiresOnlyForRecipientRealActivation)
{
	SylvanTacticsState state;
	state.enabled = state.coveringFire = true;
	state.finishStrike(1, {10}, {}, {2, 3}, false);
	state.finishStrike(1, {10}, {}, {2}, false);
	EXPECT_EQ(state.covered, (std::set<uint32_t>{2, 3}));
	state.nextRound();
	EXPECT_TRUE(state.covered.contains(2));
	state.beginActivation(2, false);
	EXPECT_EQ(state.covered, (std::set<uint32_t>{3}));
}
TEST(SylvanTacticsStateTest, DisruptionRefreshesAndExpiresEvenAfterControlChanges)
{
	SylvanTacticsState state;
	state.enabled = state.disruptiveStrike = true;
	state.finishStrike(1, {}, {10}, {}, false);
	state.finishStrike(1, {}, {10}, {}, false);
	EXPECT_EQ(state.disrupted.size(), 1);
	state.nextRound();
	EXPECT_TRUE(state.disrupted.contains(10));
	state.beginActivation(10, true);
	EXPECT_TRUE(state.disrupted.empty());
}
TEST(SylvanTacticsStateTest, WildHuntDoesNotStackOrGrantActivationAndExpiresAtRoundBoundary)
{
	SylvanTacticsState state;
	state.enabled = state.wildHunt = true;
	state.finishStrike(1, {}, {}, {}, true);
	state.finishStrike(2, {}, {}, {}, true);
	EXPECT_EQ(state.speedBonus(1), 2);
	EXPECT_EQ(state.speedBonus(2), 2);
	EXPECT_TRUE(state.activated.empty());
	state.endActivation();
	EXPECT_EQ(state.speedBonus(1), 2);
	state.nextRound();
	EXPECT_EQ(state.speedBonus(1), 0);
}
TEST(SylvanTacticsStateTest, ProjectionOnlyRecordsActualPositiveDamageAndQualifiedDestruction)
{
	SylvanTacticsState state;
	state.enabled = state.coveringFire = state.crossfire = state.wildHunt = true;
	newHorizonsSylvanTactics::StrikeSnapshot snapshot;
	snapshot.valid = snapshot.shooting = true;
	snapshot.shooter = 1;
	snapshot.eligibleTargets = {10, 11};
	snapshot.adjacentFriends = {2, 3};
	newHorizonsSylvanTactics::finishStrike(snapshot, {{10, 0}, {11, 5}, {99, 10}}, {3, 99}, state);
	EXPECT_FALSE(state.huntTriggered);
	EXPECT_FALSE(state.shootersByTarget.contains(10));
	EXPECT_FALSE(state.shootersByTarget.contains(99));
	EXPECT_TRUE(state.shootersByTarget.contains(11));
	EXPECT_EQ(state.covered, (std::set<uint32_t>{2}));
}
TEST(SylvanTacticsStateTest, CurrentSerializationRoundtripAndDowngradeRejectBeforePrefix)
{
	SylvanLuckState state;
	state.secondChance = true;
	state.tactics.enabled = state.tactics.crossfire = state.tactics.perfectMoment = true;
	state.tactics.finishStrike(1, {10}, {}, {}, false);
	state.tactics.dappledCover = true;
	state.tactics.beginActivation(2, true, false, true);
	EXPECT_EQ(decode(encode(state, Version::CURRENT), Version::CURRENT), state);
	CMemorySerializer old;
	old.oser.version = Version::NEW_HORIZONS_DREAD_CAVALRY;
	EXPECT_THROW(old.oser & state, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}
TEST(SylvanTacticsStateTest, LegacySerializationDoesNotInstallCurrentFactionPolicy)
{
	SylvanLuckState legacy;
	legacy.perfectMoment = true;
	legacy.perfectMomentUsed = true;
	legacy.forestsFavor = true;
	legacy.recordStrike(3, true, false);
	const auto bytes = encode(legacy, Version::NEW_HORIZONS_DREAD_CAVALRY);
	const auto loaded = decode(bytes, Version::NEW_HORIZONS_DREAD_CAVALRY);
	EXPECT_EQ(loaded, legacy);
	EXPECT_FALSE(loaded.tactics.enabled);
	EXPECT_EQ(encode(loaded, Version::NEW_HORIZONS_DREAD_CAVALRY), bytes);
}
TEST(SylvanTacticsStateTest, DetachedCopiesCannotLeakNewHistoryToSiblingBranch)
{
	SylvanTacticsState source;
	source.enabled = source.crossfire = source.coveringFire = source.wildHunt = true;
	auto branch = source;
	branch.finishStrike(1, {10}, {}, {2}, true);
	branch.beginActivation(2, true);
	EXPECT_EQ(source, (SylvanTacticsState{.enabled=true, .coveringFire=true, .crossfire=true, .wildHunt=true}));
	EXPECT_NE(source, branch);
}
TEST(SylvanTacticsStateTest, MixedLegacyAndNewPolicyCannotBeSaved)
{
	SylvanLuckState state;
	state.tactics.enabled = true;
	state.perfectMoment = true;
	EXPECT_THROW(encode(state, Version::CURRENT), std::runtime_error);
}
TEST(SylvanTacticsStateTest, MissingCapturedDictionaryNeverUsesInstalledNewSkillLabels)
{
	JsonNode old;
	EXPECT_EQ(newHorizonsSylvanTactics::capturedSkillName(old), "Sylvan Luck");
	EXPECT_NE(newHorizonsSylvanTactics::capturedRankDescription(old, 3).find("3.00x"), std::string::npos);
	old["sylvanTacticsPolicy"] = JsonNode(1);
	EXPECT_THROW(newHorizonsSylvanTactics::capturedSkillName(old), std::runtime_error);
	EXPECT_THROW(newHorizonsSylvanTactics::capturedRankDescription(old, 3), std::runtime_error);
}

TEST(SylvanTacticsStateTest, EnclosingBattleAndBattleStartRejectDowngradeBeforeAnyPrefix)
{
	const auto target = static_cast<Version>(1142);
	BattleInfo battle(nullptr);
	battle.getSide(BattleSide::ATTACKER).sylvanLuck.tactics.enabled = true;
	CMemorySerializer battleWire;
	battleWire.oser.version = target;
	EXPECT_THROW(battleWire.oser & battle, std::runtime_error);
	EXPECT_TRUE(battleWire.extractBuffer().empty());
	BattleStart packet;
	packet.info = std::make_unique<BattleInfo>(nullptr);
	packet.info->getSide(BattleSide::DEFENDER).sylvanLuck.tactics.enabled = true;
	CMemorySerializer packetWire;
	packetWire.oser.version = target;
	EXPECT_THROW(packetWire.oser & packet, std::runtime_error);
	EXPECT_TRUE(packetWire.extractBuffer().empty());
}
