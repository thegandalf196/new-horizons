/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeSpellLuaParityFixture.h"

namespace test
{
namespace
{
class CapturedPlagueMechanics : public MechanicsMock
{
public:
	JsonNode capture;
	JsonNode getCapturedMdrPenetration() const override { return capture; }
};
class SpellRecipientServer : public ServerCallbackMock
{
public:
	std::vector<uint32_t> recipients;
	void recordResolvedSpellRecipient(uint32_t id) override { recipients.push_back(id); }
};
}

TEST_F(NativeSpellLuaParity, AttachedCombatScriptPreservesNestedParametersAndFractionalMagnitudeConversion)
{
	spell(SpellID::HASTE);
	auto & recipient = unit(42);
	JsonNode config;
	config["eventScript"].String() = "core:destruction";
	config["eventValue"].Float() = 47.75;
	config["eventParameters"]["killBy"].String() = "count";
	config["eventParameters"]["amount"].Integer() = 3;
	config["eventParameters"]["nested"]["fraction"].Float() = 1.25;
	config["eventParameters"]["nested"]["absent"] = JsonNode();
	prepare("attachCombatScript", config);
	expectApplyParity({::battle::Destination(&recipient)});
}

TEST_F(NativeSpellLuaParity, FrailtyPreservesAccumulatedDefenseLossAndReplacementPayload)
{
	const SpellID id(SpellID::decode("new-horizons:frailty"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	auto & recipient = unit(42);
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	recipient.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
		BonusSource::CREATURE_ABILITY, 29, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE)));
	auto previous = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
		BonusSource::SPELL_EFFECT, -4, BonusSourceID(id), BonusSubtypeID(PrimarySkill::DEFENSE));
	previous->parameters = std::make_shared<BonusParameters>(int32_t(1250));
	recipient.addNewBonus(previous);
	prepare("frailty", JsonNode());
	expectApplyParity({::battle::Destination(&recipient)});
	EXPECT_GE(trace.packets.size(), 2u) << "Must remove the previous source before adding its replacement";
}

TEST_F(NativeSpellLuaParity, HexOfPainPreservesCasterSideAndRefreshSource)
{
	const SpellID id(SpellID::decode("new-horizons:hexOfPain"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	auto & recipient = unit(42);
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, ownerMatches(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, usesNewHorizonsMagicV3()).Times(AnyNumber()).WillRepeatedly(Return(true));
	auto previous = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER,
		BonusSource::SPELL_EFFECT, 7, BonusSourceID(id), BonusSubtypeID(ScriptID(ScriptID::decode("core:hexOfPain"))));
	recipient.addNewBonus(previous);
	prepare("hexOfPain", JsonNode());
	expectApplyParity({::battle::Destination(&recipient)});
	EXPECT_GE(trace.packets.size(), 2u);
}

TEST_F(NativeSpellLuaParity, PlaguePreservesOrderedSourceIdentityAndInitialPropagationCapture)
{
	const SpellID id(SpellID::decode("new-horizons:plague"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	unit(11);
	auto & recipient = unit(42);
	EXPECT_CALL(recipient, unitSide()).Times(AnyNumber()).WillRepeatedly(Return(BattleSide::DEFENDER));
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, usesNewHorizonsMagicV3()).Times(AnyNumber()).WillRepeatedly(Return(true));
	prepare("plague", JsonNode());
	expectApplyParity({::battle::Destination(&recipient)});
	NiceMock<CapturedPlagueMechanics> captured;
	captured.casterSide = BattleSide::ATTACKER;
	captured.capture["sourceHeroCasterId"].Integer() = 17;
	captured.capture["penetrations"].Vector().emplace_back(20);
	captured.capture["overwhelmingFormulaSide"].Integer() = 0;
	captured.capture["overwhelmingFormulaToken"].String() = "18446744073709551615";
	captured.capture.setModScope("source", true);
	ON_CALL(captured, battle()).WillByDefault(Return(battleFake.get()));
	ON_CALL(captured, getSpellId()).WillByDefault(Return(id));
	ON_CALL(captured, getSpell()).WillByDefault(Return(id.toSpell()));
	ON_CALL(captured, getEffectPower()).WillByDefault(Return(11));
	ON_CALL(captured, getEffectLevel()).WillByDefault(Return(2));
	ON_CALL(captured, getEffectDuration()).WillByDefault(Return(7));
	ON_CALL(captured, isReceptive(_)).WillByDefault(Return(true));
	ON_CALL(captured, usesNewHorizonsMagicV3()).WillByDefault(Return(true));
	const Target target{::battle::Destination(&recipient)};
	const auto expected = run(oracle, target, 1234, &captured);
	const auto actual = run(subject, target, 1234, &captured);
	EXPECT_EQ(actual, expected);
	EXPECT_FALSE(actual.packets.empty());
}

TEST_F(NativeSpellLuaParity, VampirismPreservesHealMagnitudeAndRefreshSource)
{
	const SpellID id(SpellID::decode("new-horizons:vampirism"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	auto & recipient = unit(42);
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, ownerMatches(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, usesNewHorizonsMagicV3()).Times(AnyNumber()).WillRepeatedly(Return(true));
	auto previous = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER,
		BonusSource::SPELL_EFFECT, 800, BonusSourceID(id), BonusSubtypeID(ScriptID(ScriptID::decode("core:vampirism"))));
	recipient.addNewBonus(previous);
	prepare("vampirism", JsonNode());
	expectApplyParity({::battle::Destination(&recipient)});
	EXPECT_GE(trace.packets.size(), 2u);
}

TEST_F(NativeSpellLuaParity, DoomPreservesExplicitInitiativeAndFallbackSpeedDistinction)
{
	const SpellID id(SpellID::decode("new-horizons:doom"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, usesNewHorizonsMagicV3()).Times(AnyNumber()).WillRepeatedly(Return(true));
	for(const bool explicitInitiative : {false, true})
	{
		SCOPED_TRACE(explicitInitiative);
		auto & recipient = unit(explicitInitiative ? 43 : 42);
		EXPECT_CALL(recipient, unitSide()).Times(AnyNumber()).WillRepeatedly(Return(BattleSide::DEFENDER));
		if(explicitInitiative) recipient.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACKS_INITIATIVE_BASE, BonusSource::CREATURE_ABILITY, 8, BonusSourceID()));
		prepare("doom", JsonNode());
		expectApplyParity({::battle::Destination(&recipient)});
		EXPECT_EQ(trace.packets.size(), explicitInitiative ? 4u : 3u);
	}
}

TEST_F(NativeSpellLuaParity, SoulChainPreservesExplicitPrimaryAndEchoRecipientOrder)
{
	const SpellID id(SpellID::decode("new-horizons:soulChain"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
	EXPECT_CALL(mechanicsMock, usesNewHorizonsMagicV3()).Times(AnyNumber()).WillRepeatedly(Return(true));
	Target target;
	for(uint32_t index = 0; index < 3; ++index)
	{
		auto & recipient = unit(40 + index);
		EXPECT_CALL(recipient, unitSide()).Times(AnyNumber()).WillRepeatedly(Return(BattleSide::DEFENDER));
		target.emplace_back(&recipient);
	}
	prepare("soulChain", JsonNode());
	EXPECT_EQ(subject->applicableTarget(problemMock, &mechanicsMock, target),
		oracle->applicableTarget(problemMock, &mechanicsMock, target));
	StrictMock<SpellRecipientServer> recorder;
	EXPECT_CALL(recorder, apply(Matcher<SetStackEffect &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](SetStackEffect & packet)
		{ serverMock.apply(packet); }));
	const auto expected = run(oracle, target, 1234, nullptr, &recorder);
	const auto expectedRecipients = recorder.recipients;
	EXPECT_EQ(expectedRecipients, std::vector<uint32_t>{40});
	recorder.recipients.clear();
	const auto actual = run(subject, target, 1234, nullptr, &recorder);
	EXPECT_EQ(actual, expected);
	EXPECT_EQ(recorder.recipients, expectedRecipients);
	EXPECT_EQ(recorder.recipients, std::vector<uint32_t>{40});
	EXPECT_EQ(trace.packets.size(), 2u) << "Primary is provenance; only the two echo recipients gain markers";
}

TEST_F(NativeSpellLuaParity, SpellLockPreservesFriendlyAndHostilePolarityFiltering)
{
	const SpellID id(SpellID::decode("new-horizons:spellLock"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	EXPECT_CALL(mechanicsMock, getCasterColor()).Times(AnyNumber()).WillRepeatedly(Return(PlayerColor(0)));
	EXPECT_CALL(*battleFake, getSidePlayer(_)).Times(AnyNumber()).WillRepeatedly(Invoke([](BattleSide side)
		{ return PlayerColor(side == BattleSide::ATTACKER ? 0 : 1); }));
	for(const bool friendly : {false, true})
	{
		SCOPED_TRACE(friendly);
		auto & recipient = unit(friendly ? 43 : 42);
		EXPECT_CALL(recipient, unitOwner()).Times(AnyNumber()).WillRepeatedly(Return(PlayerColor(friendly ? 0 : 1)));
		EXPECT_CALL(recipient, unitSide()).Times(AnyNumber()).WillRepeatedly(Return(friendly ? BattleSide::ATTACKER : BattleSide::DEFENDER));
		EXPECT_CALL(recipient, isHypnotized()).Times(AnyNumber()).WillRepeatedly(Return(false));
		ASSERT_EQ(battleFake->battleGetOwner(&recipient), PlayerColor(friendly ? 0 : 1));
		for(const auto source : {SpellID::HASTE, SpellID::SLOW})
			recipient.addNewBonus(std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
				BonusSource::SPELL_EFFECT, 3, BonusSourceID(SpellID(source))));
		prepare("spellLock", JsonNode());
		expectApplyParity({::battle::Destination(&recipient)});
		EXPECT_EQ(trace.packets.size(), 3u) << "Remove only opposing polarity, then add resistance and polarity marker";
	}
}
}
