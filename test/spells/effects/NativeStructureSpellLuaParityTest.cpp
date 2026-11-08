/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeSpellLuaParityFixture.h"
#include "../../mock/TownFake.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/filesystem/ResourcePath.h"

namespace test
{
namespace
{
class StructureRulesBattle : public battle::BattleFake
{
public:
	JsonNode rules{JsonPath::builtin("config/newHorizonsMagic")};
	const JsonNode & getMagicRules() const override { return rules; }
};
class StructureRecipientServer : public ServerCallbackMock
{
public:
	std::vector<uint32_t> recipients;
	void recordResolvedSpellRecipient(uint32_t id) override { recipients.push_back(id); }
};
std::shared_ptr<SpellCreatedObstacle> spellObstacle(int id, BattleHex position, SpellID source)
{
	auto result = std::make_shared<SpellCreatedObstacle>();
	result->uniqueID = id;
	result->ID = source.getNum();
	result->pos = position;
	result->customSize.insert(position);
	return result;
}
}

TEST_F(NativeSpellLuaParity, RemoveObstaclePreservesSpecificSpellFilteringAndMoatExclusion)
{
	spell(SpellID::REMOVE_OBSTACLE);
	auto mine = spellObstacle(7, BattleHex(77), SpellID::LAND_MINE);
	auto fire = spellObstacle(8, BattleHex(78), SpellID::FIRE_WALL);
	auto moat = spellObstacle(9, BattleHex(79), SpellID::LAND_MINE);
	moat->obstacleType = CObstacleInstance::MOAT;
	EXPECT_CALL(*battleFake, getAllObstacles()).Times(AnyNumber()).WillRepeatedly(Return(IBattleInfo::ObstacleCList{mine, fire, moat}));
	EXPECT_CALL(mechanicsMock, isMassive()).Times(AnyNumber()).WillRepeatedly(Return(true));
	for(const bool all : {false, true})
	{
		SCOPED_TRACE(all);
		JsonNode config;
		config["removeAllSpells"].Bool() = all;
		config["removeSpells"].Vector().emplace_back("core:landMine");
		prepare("removeObstacle", config);
		ASSERT_TRUE(oracle->applicableTarget(problemMock, &mechanicsMock, {}));
		ASSERT_TRUE(subject->applicableTarget(problemMock, &mechanicsMock, {}));
		expectApplyParity({});
		EXPECT_EQ(trace.packets.size(), all ? 2u : 1u);
	}
}

TEST_F(NativeSpellLuaParity, MoatPreservesGroupedFootprintsDispellabilityAndBattleBonusLimiterPacket)
{
	spell(SpellID::CURSE);
	mechanicsMock.casterSide = BattleSide::DEFENDER;
	TownFake town;
	town.withBuilding(BuildingID::FORT);
	town.withBuilding(BuildingID::CITADEL);
	EXPECT_CALL(*battleFake, getDefendedTown()).Times(AnyNumber()).WillRepeatedly(Return(town.get()));
	EXPECT_CALL(*battleFake, getAllObstacles()).Times(AnyNumber()).WillRepeatedly(Return(IBattleInfo::ObstacleCList()));
	EXPECT_CALL(mechanicsMock, isMassive()).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(serverMock, apply(Matcher<CPackForClient &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](CPackForClient & packet)
	{
		CPackForClient * pointer = &packet;
		capture("generic", pointer);
	}));
	for(const bool dispellable : {false, true})
	{
		SCOPED_TRACE(dispellable);
		JsonNode config;
		config["dispellable"].Bool() = dispellable;
		config["moatDamage"].Integer() = 73;
		for(const auto & footprint : {std::vector<int>{77, 78}, std::vector<int>{95}})
		{
			JsonNode patch;
			for(const auto hex : footprint) patch.Vector().emplace_back(hex);
			config["moatHexes"].Vector().push_back(patch);
		}
		config["bonus"]["slow"]["type"].String() = "STACKS_SPEED";
		config["bonus"]["slow"]["val"].Integer() = -2;
		prepare("moat", config);
		expectApplyParity({});
		EXPECT_EQ(trace.packets.size(), 3u);
	}
}

class NativeStructureSpellLuaParity : public NativeSpellLuaParity
{
protected:
	void currentRules()
	{
		battleFake = std::make_shared<StructureRulesBattle>();
		ON_CALL(*battleFake, getUnitsIf(_)).WillByDefault(Invoke(&unitsFake, &battle::UnitsFake::getUnitsIf));
		EXPECT_CALL(*battleFake, getUnitsIf(_)).Times(AnyNumber());
		EXPECT_CALL(mechanicsMock, battle()).Times(AnyNumber()).WillRepeatedly(Return(battleFake.get()));
		EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
		EXPECT_CALL(mechanicsMock, usesNewHorizonsMagicV3()).Times(AnyNumber()).WillRepeatedly(Return(true));
		battleFake->setupEmptyBattlefield();
	}
};

TEST_F(NativeStructureSpellLuaParity, EarthquakePreservesGroundDamageAndEveryRubbleHexPacket)
{
	currentRules();
	spell(SpellID::EARTHQUAKE);
	EXPECT_CALL(mechanicsMock, isMassive()).Times(AnyNumber()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, adjustEffectValue(_)).Times(AnyNumber()).WillRepeatedly(Return(173));
	EXPECT_CALL(mechanicsMock, isSmart()).Times(AnyNumber()).WillRepeatedly(Return(false));
	EXPECT_CALL(mechanicsMock, alwaysHitFirstTarget()).Times(AnyNumber()).WillRepeatedly(Return(false));
	auto & recipient = unit(42, BattleHex(77));
	prepare("earthquake", JsonNode());
	ASSERT_TRUE(mechanicsMock.usesNewHorizonsEarthquake());
	const Target center{::battle::Destination(BattleHex(77))};
	const auto expected = oracle->transformTarget(&mechanicsMock, center, center);
	const auto actual = subject->transformTarget(&mechanicsMock, center, center);
	EXPECT_EQ(actual, expected);
	ASSERT_TRUE(std::ranges::any_of(actual, [&](const auto & destination) { return destination.unitValue == &recipient; }));
	StrictMock<StructureRecipientServer> recorder;
	EXPECT_CALL(recorder, describeChanges()).Times(AnyNumber()).WillRepeatedly(Return(false));
	EXPECT_CALL(recorder, getRNG()).Times(AnyNumber()).WillRepeatedly(Return(&rngMock));
	EXPECT_CALL(recorder, apply(Matcher<StacksInjured &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](StacksInjured & p) { serverMock.apply(p); }));
	EXPECT_CALL(recorder, apply(Matcher<BattleLogMessage &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](BattleLogMessage & p) { serverMock.apply(p); }));
	EXPECT_CALL(recorder, apply(Matcher<BattleObstaclesChanged &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](BattleObstaclesChanged & p) { serverMock.apply(p); }));
	const auto expectedTrace = run(oracle, actual, 1234, nullptr, &recorder);
	const auto expectedRecipients = recorder.recipients;
	EXPECT_EQ(expectedRecipients, std::vector<uint32_t>{42});
	recorder.recipients.clear();
	const auto actualTrace = run(subject, actual, 1234, nullptr, &recorder);
	EXPECT_EQ(actualTrace, expectedTrace);
	EXPECT_EQ(recorder.recipients, expectedRecipients);
	EXPECT_EQ(recorder.recipients, std::vector<uint32_t>{42});
	EXPECT_GT(trace.packets.size(), 1u);
}

TEST_F(NativeStructureSpellLuaParity, HavocStructuresPreservesArmageddonObstacleSelectionAndRemovalOrder)
{
	currentRules();
	spell(SpellID::ARMAGEDDON);
	EXPECT_CALL(mechanicsMock, getEffectValue()).Times(AnyNumber()).WillRepeatedly(Return(337));
	EXPECT_CALL(mechanicsMock, isMassive()).Times(AnyNumber()).WillRepeatedly(Return(true));
	auto ordinary = std::make_shared<CObstacleInstance>();
	ordinary->obstacleType = CObstacleInstance::USUAL;
	ordinary->uniqueID = 7;
	ordinary->ID = 0;
	ordinary->pos = BattleHex(77);
	auto magic = spellObstacle(8, BattleHex(95), SpellID::FIRE_WALL);
	EXPECT_CALL(*battleFake, getAllObstacles()).Times(AnyNumber()).WillRepeatedly(Return(IBattleInfo::ObstacleCList{ordinary, magic}));
	prepare("havocStructures", JsonNode());
	ASSERT_TRUE(mechanicsMock.usesNewHorizonsHavocStructures());
	expectApplyParity({});
	EXPECT_EQ(trace.packets.size(), mechanicsMock.destroysNewHorizonsHavocMagicalObstacles() ? 2u : 1u);
}
}
