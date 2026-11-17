/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeSpellLuaParityFixture.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/spells/NewHorizonsBlink.h"

namespace test
{
namespace
{
class UtilityRulesBattle : public battle::BattleFake
{
public:
	JsonNode rules{JsonPath::builtin("config/newHorizonsMagic")};
	const JsonNode & getMagicRules() const override { return rules; }
};
class ShadowGiftMechanics : public MechanicsMock
{
public:
	int32_t getShadowGiftSacrificePercent() const override { return 20; }
};
}

class NativeUtilitySpellLuaParity : public NativeSpellLuaParity
{
protected:
	void currentRules()
	{
		battleFake = std::make_shared<UtilityRulesBattle>();
		ON_CALL(*battleFake, getUnitsIf(_)).WillByDefault(Invoke(&unitsFake, &battle::UnitsFake::getUnitsIf));
		EXPECT_CALL(*battleFake, getUnitsIf(_)).Times(AnyNumber());
		EXPECT_CALL(mechanicsMock, battle()).Times(AnyNumber()).WillRepeatedly(Return(battleFake.get()));
		EXPECT_CALL(mechanicsMock, isReceptive(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
		EXPECT_CALL(mechanicsMock, ownerMatches(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
		EXPECT_CALL(mechanicsMock, ownerMatches(_, _)).Times(AnyNumber()).WillRepeatedly(Return(true));
		EXPECT_CALL(mechanicsMock, usesNewHorizonsMagicV3()).Times(AnyNumber()).WillRepeatedly(Return(true));
	}
};



TEST_F(NativeUtilitySpellLuaParity, TheChosenRemovesPreviousLivingAndCorpseRecipientsBeforeAddingAbilities)
{
	currentRules();
	const SpellID id(SpellID::decode("new-horizons:theChosen"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	auto & recipient = unit(42);
	recipient.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LIVING,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID()));
	for(uint32_t index = 0; index < 2; ++index)
	{
		auto & previous = unit(50 + index);
		if(index == 1) EXPECT_CALL(previous, alive()).Times(AnyNumber()).WillRepeatedly(Return(false));
		previous.addNewBonus(std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::FLYING,
			BonusSource::SPELL_EFFECT, 1, BonusSourceID(id)));
	}
	prepare("theChosen", JsonNode());
	expectApplyParity({::battle::Destination(&recipient)});
	EXPECT_EQ(trace.packets.size(), 6u) << "Two old recipients clear before the four ordered ability additions";
}

TEST_F(NativeUtilitySpellLuaParity, ShadowGiftPreservesAcceptedSacrificeProfileAndCasterSideCapture)
{
	currentRules();
	const SpellID id(SpellID::decode("new-horizons:shadowGift"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	auto & recipient = unit(42);
	prepare("shadowGift", JsonNode());
	NiceMock<ShadowGiftMechanics> mechanics;
	mechanics.casterSide = BattleSide::ATTACKER;
	ON_CALL(mechanics, battle()).WillByDefault(Return(battleFake.get()));
	ON_CALL(mechanics, getSpellId()).WillByDefault(Return(id));
	ON_CALL(mechanics, getSpell()).WillByDefault(Return(id.toSpell()));
	ON_CALL(mechanics, getEffectPower()).WillByDefault(Return(11));
	ON_CALL(mechanics, getEffectLevel()).WillByDefault(Return(2));
	ON_CALL(mechanics, isReceptive(_)).WillByDefault(Return(true));
	ON_CALL(mechanics, ownerMatches(_)).WillByDefault(Return(true));
	ON_CALL(mechanics, usesNewHorizonsMagicV3()).WillByDefault(Return(true));
	const Target target{::battle::Destination(&recipient)};
	const auto expected = run(oracle, target, 1234, &mechanics);
	const auto actual = run(subject, target, 1234, &mechanics);
	EXPECT_EQ(actual, expected);
	EXPECT_FALSE(actual.packets.empty());
}

TEST_F(NativeUtilitySpellLuaParity, CurrentFocusUsesNativeEnchantmentAndRejectsTheRetiredMarkEffectPath)
{
	ScriptTypeDescription description;
	description.kind = ScriptKind::SPELL_EFFECT;
	description.modScope = "core";
	description.identifier = "focusMagicEnchantment";
	description.scriptId = "core:focusMagicEnchantment";
	description.sourcePath = "spells/focusMagicEnchantment";
	const auto current = nativeSpells::create(description);
	ASSERT_NE(current, nullptr);
	description.sourcePath = "spells/focusMagic";
	EXPECT_EQ(nativeSpells::create(description), nullptr);
}

TEST_F(NativeUtilitySpellLuaParity, HydrasVitalityPreservesCapacityHealthBeforeApplyingMaximumAndRegeneration)
{
	currentRules();
	const SpellID id(SpellID::decode("new-horizons:hydrasVitality"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	auto & recipient = unit(42);
	recipient.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LIVING,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID()));
	EXPECT_CALL(mechanicsMock, getEffectValue()).Times(AnyNumber()).WillRepeatedly(Return(37'500'000));
	prepare("hydrasVitality", JsonNode());
	expectApplyParity({::battle::Destination(&recipient)});
	ASSERT_EQ(trace.packets.size(), 3u);
	EXPECT_EQ(trace.packets.front().first, "units");
}

TEST_F(NativeUtilitySpellLuaParity, BlinkPreservesUniformDestinationDrawAndMovementWithoutChangingSearch)
{
	currentRules();
	const SpellID id(SpellID::decode("new-horizons:blink"));
	ASSERT_NE(id, SpellID::NONE);
	spell(id);
	battleFake->setupEmptyBattlefield();
	auto & recipient = unit(42, BattleHex(77));
	EXPECT_CALL(mechanicsMock, isSmart()).Times(AnyNumber()).WillRepeatedly(Return(false));
	prepare("blink", JsonNode());
	const Target target{::battle::Destination(&recipient)};
	const auto preview = newHorizonsBlink::preview(mechanicsMock, &recipient);
	ASSERT_TRUE(preview.has_value());
	ASSERT_GT(preview->legalDestinations.size(), 1u);
	for(uint32_t seed = 0; seed < 16; ++seed)
	{
		SCOPED_TRACE(seed);
		expectApplyParity(target, seed);
		EXPECT_EQ(trace.packets.size(), 1u);
		EXPECT_EQ(trace.random.size(), 1u);
	}
}
}
