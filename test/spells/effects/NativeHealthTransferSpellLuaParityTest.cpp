/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeSpellLuaParityFixture.h"

namespace test
{
namespace
{
class TransferRecipientServer : public ServerCallbackMock
{
public:
	std::vector<uint32_t> recipients;
	void recordResolvedSpellRecipient(uint32_t id) override { recipients.push_back(id); }
};
}

TEST_F(NativeSpellLuaParity, LifeDrainPreservesEnemyFirstSelectionAndResolvedDamageThenFriendlyHealing)
{
	const SpellID id(SpellID::decode("new-horizons:lifeDrain"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	auto & enemy = unit(42);
	auto & friendly = unit(43);
	for(auto * unit : {&enemy, &friendly})
	{
		EXPECT_CALL(*unit, creatureId()).Times(AnyNumber()).WillRepeatedly(Return(CreatureID(0)));
		EXPECT_CALL(*unit, unitType()).Times(AnyNumber()).WillRepeatedly(Return(CreatureID(0).toCreature()));
	}
	EXPECT_CALL(enemy, unitOwner()).Times(AnyNumber()).WillRepeatedly(Return(PlayerColor(1)));
	EXPECT_CALL(friendly, unitOwner()).Times(AnyNumber()).WillRepeatedly(Return(PlayerColor(0)));
	EXPECT_CALL(enemy, unitSide()).Times(AnyNumber()).WillRepeatedly(Return(BattleSide::DEFENDER));
	EXPECT_CALL(enemy, isHypnotized()).Times(AnyNumber()).WillRepeatedly(Return(false));
	EXPECT_CALL(friendly, isHypnotized()).Times(AnyNumber()).WillRepeatedly(Return(false));
	EXPECT_CALL(*battleFake, getSidePlayer(_)).Times(AnyNumber()).WillRepeatedly(Invoke([](BattleSide side)
		{ return PlayerColor(side == BattleSide::ATTACKER ? 0 : 1); }));
	ASSERT_EQ(battleFake->battleGetOwner(&enemy), PlayerColor(1));
	ASSERT_EQ(battleFake->battleGetOwner(&friendly), PlayerColor(0));
	EXPECT_CALL(friendly, getCount()).Times(AnyNumber()).WillRepeatedly(Return(9));
	EXPECT_CALL(friendly, getFirstHPleft()).Times(AnyNumber()).WillRepeatedly(Return(50));
	EXPECT_CALL(friendly, acquireState()).Times(AnyNumber()).WillRepeatedly(Invoke([this, &friendly]
	{
		auto state = std::make_shared<::battle::CUnitStateDetached>(&friendly, &friendly);
		state->localInit(&unitEnvironment);
		int64_t damage = 350;
		state->damage(damage);
		return state;
	}));
	EXPECT_CALL(mechanicsMock, getCasterColor()).Times(AnyNumber()).WillRepeatedly(Return(PlayerColor(0)));
	EXPECT_CALL(mechanicsMock, adjustEffectValue(_)).Times(AnyNumber()).WillRepeatedly(Return(337));
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, usesNewHorizonsMagicV3()).Times(AnyNumber()).WillRepeatedly(Return(true));
	prepare("lifeDrain", JsonNode());
	const Target target{::battle::Destination(&enemy), ::battle::Destination(&friendly)};
	ASSERT_TRUE(oracle->applicableTarget(problemMock, &mechanicsMock, target));
	ASSERT_TRUE(subject->applicableTarget(problemMock, &mechanicsMock, target));
	const auto expectedPreview = oracle->getHealthChange(&mechanicsMock, target);
	const auto actualPreview = subject->getHealthChange(&mechanicsMock, target);
	EXPECT_EQ(actualPreview.hpDelta, expectedPreview.hpDelta);
	EXPECT_EQ(actualPreview.unitsDelta, expectedPreview.unitsDelta);
	EXPECT_EQ(actualPreview.unitType, expectedPreview.unitType);
	EXPECT_LT(actualPreview.hpDelta, 0);
	StrictMock<TransferRecipientServer> recorder;
	EXPECT_CALL(recorder, getRNG()).Times(AnyNumber()).WillRepeatedly(Return(&rngMock));
	EXPECT_CALL(recorder, apply(Matcher<StacksInjured &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](StacksInjured & p) { serverMock.apply(p); }));
	EXPECT_CALL(recorder, apply(Matcher<BattleUnitsChanged &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](BattleUnitsChanged & p) { serverMock.apply(p); }));
	EXPECT_CALL(recorder, apply(Matcher<BattleLogMessage &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](BattleLogMessage & p) { serverMock.apply(p); }));
	const auto expected = run(oracle, target, 1234, nullptr, &recorder);
	const auto expectedRecipients = recorder.recipients;
	EXPECT_EQ(expectedRecipients, (std::vector<uint32_t>{42, 43}));
	recorder.recipients.clear();
	const auto actual = run(subject, target, 1234, nullptr, &recorder);
	EXPECT_EQ(actual, expected);
	EXPECT_EQ(recorder.recipients, expectedRecipients);
	EXPECT_EQ(recorder.recipients, (std::vector<uint32_t>{42, 43}));
	ASSERT_GE(actual.packets.size(), 2u);
	EXPECT_EQ(actual.packets[0].first, "injury");
	EXPECT_EQ(actual.packets[1].first, "units");
}
}
