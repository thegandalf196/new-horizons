/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeSpellLuaParityFixture.h"
#include "../../mock/TownFake.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include <iostream>

namespace test
{
TEST_F(NativeSpellLuaParity, ChainDamageMatchesResultingUnitStatesAndPacketOrder)
{
	JsonNode config;
	config["chainLength"].Integer() = 5;
	config["chainFactor"].Float() = 0.53;
	config["indirect"].Bool() = true;
	prepare("damage", config);
	spell(SpellID::CHAIN_LIGHTNING);
	EXPECT_CALL(mechanicsMock, adjustEffectValue(_)).Times(AnyNumber()).WillRepeatedly(Return(337));
	Target target;
	for(int index = 0; index < 5; ++index) target.emplace_back(&unit(20 + index, BattleHex(40 + index)));
	const auto expectedPreview = oracle->getHealthChange(&mechanicsMock, target);
	const auto actualPreview = subject->getHealthChange(&mechanicsMock, target);
	EXPECT_EQ(actualPreview.hpDelta, expectedPreview.hpDelta);
	EXPECT_EQ(actualPreview.unitsDelta, expectedPreview.unitsDelta);
	expectApplyParity(target);
}

TEST_F(NativeSpellLuaParity, ShieldPatchAndConfiguredDurationMatch)
{
	spell(SpellID::SHIELD);
	auto & recipient = unit(42);
	for(const int configuredTurns : {0, 2, 19})
	{
		SCOPED_TRACE(configuredTurns);
		JsonNode config;
		auto & bonus = config["bonus"]["shield"];
		bonus["type"].String() = "GENERAL_DAMAGE_REDUCTION";
		bonus["val"].Float() = 37.5;
		bonus["duration"].String() = "N_TURNS";
		bonus["turns"].Integer() = configuredTurns;
		prepare("timed", config);
		expectApplyParity({::battle::Destination(&recipient)});
	}
}

TEST_F(NativeSpellLuaParity, BindPatchKeepsCastingUnitIdentity)
{
	spell(SpellID::BIND);
	auto & caster = unit(73);
	auto & recipient = unit(42);
	EXPECT_CALL(mechanicsMock, getUnitCaster()).Times(AnyNumber()).WillRepeatedly(Return(&caster));
	JsonNode config;
	config["bonus"]["bind"]["type"].String() = "BIND_EFFECT";
	config["bonus"]["bind"]["val"].Integer() = 1;
	prepare("timed", config);
	expectApplyParity({::battle::Destination(&recipient)});
}

TEST_F(NativeSpellLuaParity, DiagnosticTimedSiblingPacketOrderIsReportedWithoutNormalization)
{
	auto & recipient = unit(42);
	const Target target{::battle::Destination(&recipient)};
	const auto order = [](const Trace & value)
	{
		std::string text;
		for(const auto & record : value.bonusOrder) text += record + " ";
		return text;
	};
	for(const auto id : {SpellID::PRAYER, SpellID::BLIND, SpellID::STONE_GAZE, SpellID::PARALYZE, SpellID::DISEASE, SpellID::POISON})
	{
		spell(SpellID(id));
		const auto * source = SpellID(id).toSpell();
		SCOPED_TRACE(source->getJsonKey());
		const auto & level = source->getLevelInfo(3);
		JsonNode config;
		if(level.battleEffects.isNull()) config["bonus"] = level.effects;
		else
		{
			ASSERT_EQ(level.battleEffects.Struct().size(), 1u);
			config = level.battleEffects.Struct().begin()->second;
		}
		prepare("timed", config);
		const auto expected = run(oracle, target);
		const auto actual = run(subject, target);
		RecordProperty(source->identifier + "_lua_bonus_order", order(expected));
		RecordProperty(source->identifier + "_native_bonus_order", order(actual));
		RecordProperty(source->identifier + "_raw_packet_traces_equal", actual == expected ? "true" : "false");
		std::cout << "TIMED_RAW_ORDER family=" << source->getJsonKey() << " Lua=" << order(expected)
			<< "; native=" << order(actual) << "; packet_traces_equal=" << (actual == expected) << '\n';
		ASSERT_FALSE(expected.packets.empty());
		ASSERT_EQ(actual.packets.size(), expected.packets.size());
		// Compare every complete pre-stamping packet, including bonus metadata.
		// Only this explicitly inventoried Timed-family test disregards sequence
		// for this separate assertion; the untouched raw traces are reported above.
		auto expectedRecords = expected.packets, actualRecords = actual.packets;
		std::sort(expectedRecords.begin(), expectedRecords.end());
		std::sort(actualRecords.begin(), actualRecords.end());
		EXPECT_EQ(actualRecords, expectedRecords);
		EXPECT_EQ(actual.random, expected.random);
	}
}

TEST_F(NativeSpellLuaParity, DispelPreservesSourceFilteringAndRemovedBonusPayload)
{
	spell(SpellID::DISPEL);
	auto & recipient = unit(42);
	for(const auto id : {SpellID::HASTE, SpellID::SLOW, SpellID::DISPEL})
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
			BonusSource::SPELL_EFFECT, 3, BonusSourceID(SpellID(id)));
		bonus->turnsRemain = 4;
		recipient.addNewBonus(bonus);
	}
	recipient.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::CREATURE_ABILITY, 2, BonusSourceID()));
	JsonNode config;
	config["dispelPositive"].Bool() = true;
	config["dispelNegative"].Bool() = true;
	prepare("dispel", config);
	expectApplyParity({::battle::Destination(&recipient)});
}

TEST_F(NativeSpellLuaParity, PhysicalCureRemovesOnlyTypedFrozenMarkers)
{
	class FrozenBattle final : public battle::BattleFake
	{
	public:
		JsonNode rules;
		const JsonNode & getMagicRules() const override { return rules; }
	};
	class FrozenCure : public spells::MechanicsMock
	{
	public:
		bool isNewHorizonsCure() const override { return true; }
		std::string getCurePhysicalAffliction() const override { return "frozen"; }
	};
	auto world = std::make_shared<FrozenBattle>();
	world->rules["rulesetVersion"].Integer() = 3;
	world->rules["schemaVersion"].Integer() = 1;
	world->rules["creatureAbilities"]["rulesetVersion"].Integer() = 1;
	world->rules["creatureAbilities"]["freezingTouchChancePercent"].Integer() = 20;
	world->rules["creatureAbilities"]["shatterBonusPercent"].Integer() = 50;
	ON_CALL(*world, getUnitsIf(_)).WillByDefault(Invoke(&unitsFake, &battle::UnitsFake::getUnitsIf));
	EXPECT_CALL(*world, getUnitsIf(_)).Times(AnyNumber());
	battleFake = world;
	prepare("dispel", JsonNode());
	NiceMock<FrozenCure> mechanics;
	ON_CALL(mechanics, battle()).WillByDefault(Return(world.get()));
	ON_CALL(mechanics, getSpell()).WillByDefault(Return(SpellID(SpellID::CURE).toSpell()));
	auto & recipient = unit(42);
	recipient.addNewBonus(std::make_shared<Bonus>(newHorizonsFrozen::marker(BonusSourceID(CreatureID(1)), 5)));
	recipient.addNewBonus(std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 3, BonusSourceID(SpellID(SpellID::HASTE))));
	const Target target{::battle::Destination(&recipient)};
	const auto expected = run(oracle, target, 1234, &mechanics);
	const auto actual = run(subject, target, 1234, &mechanics);
	EXPECT_EQ(actual, expected);
	ASSERT_EQ(actual.packets.size(), 1u);
}

TEST_F(NativeSpellLuaParity, CatapultMatchesQualityRollsTargetDrawsAndGroupedPacketOrder)
{
	TownFake town;
	town.withBuilding(BuildingID::FORT);
	EXPECT_CALL(*battleFake, getDefendedTown()).Times(AnyNumber()).WillRepeatedly(Return(town.get()));
	EXPECT_CALL(*battleFake, getWallState(_)).Times(AnyNumber()).WillRepeatedly(Return(EWallState::INTACT));
	EXPECT_CALL(mechanicsMock, isMassive()).Times(AnyNumber()).WillRepeatedly(Return(true));
	JsonNode config;
	config["targetsToAttack"].Integer() = 12;
	config["chanceToCrit"].Float() = 33.5;
	config["chanceToNormalHit"].Float() = 42.25;
	prepare("catapult", config);
	for(uint32_t seed = 0; seed < 32; ++seed)
	{
		SCOPED_TRACE(seed);
		expectApplyParity({}, seed);
	}
}

TEST_F(NativeSpellLuaParity, ObstacleVisibilityFootprintAndRandomPlacementMatch)
{
	spell(SpellID::LAND_MINE);
	battleFake->setupEmptyBattlefield();
	EXPECT_CALL(mechanicsMock, isMassive()).Times(AnyNumber()).WillRepeatedly(Return(true));
	for(const bool hideNative : {false, true})
	{
		SCOPED_TRACE(hideNative);
		JsonNode config;
		config["patchCount"].Integer() = 4;
		config["hidden"].Bool() = true;
		config["hideNative"].Bool() = hideNative;
		config["trap"].Bool() = true;
		config["passable"].Bool() = true;
		config["turnsRemaining"].Integer() = 3;
		config["attacker"]["animation"].String() = "C17SPE1";
		prepare("obstacle", config);
		expectApplyParity({});
	}
}
}
