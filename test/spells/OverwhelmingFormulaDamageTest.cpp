/*
 * OverwhelmingFormulaDamageTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/battle/OverwhelmingFormulaState.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/MagicalDamageReduction.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../mock/BattleFake.h"

#include <array>
#include <limits>

namespace
{
class FormulaBattle final : public BattleStateMock
{
public:
	const OverwhelmingFormulaState & getOverwhelmingFormulaState(BattleSide side) const override
	{
		return states.at(static_cast<size_t>(side));
	}

	std::array<OverwhelmingFormulaState, 2> states;
};

JsonNode capture(BattleSide side, const std::string & token)
{
	JsonNode result;
	result["penetrations"].Vector() = {JsonNode(20), JsonNode(15)};
	result["overwhelmingFormulaSide"].Integer() = static_cast<int>(side);
	result["overwhelmingFormulaToken"].String() = token;
	return result;
}

std::shared_ptr<Bonus> reduction(BonusType type, int value, SpellSchool school)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
		BonusSource::OTHER, value, BonusSourceID(), BonusSubtypeID(school));
}

class OverwhelmingFormulaDamageTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		target.redirectBonusesToFake();
		target.expectAnyBonusSystemCall();
	}

	int64_t damage(int64_t raw, bool fractional, int hold = 0, int perk = 0,
		const std::vector<int> & penetrations = {})
	{
		return spell.adjustRawDamage(nullptr, &target, raw, 0, hold, 100,
			true, fractional, false, perk, penetrations);
	}

	CSpell spell;
	::testing::NiceMock<test::battle::UnitFake> target;
};
}

TEST(OverwhelmingFormulaCapture, CanonicalStringTokensPreserveFullUint64IdentityOnWire)
{
	for(const std::string token : {"1", "9007199254740993", "18446744073709551615"})
	{
		const auto payload = capture(BattleSide::DEFENDER, token);
		CMemorySerializer wire;
		wire.oser & payload;
		JsonNode restored;
		wire.iser & restored;
		EXPECT_EQ(restored["overwhelmingFormulaToken"].String(), token);
		const auto parsed = spells::capturedOverwhelmingFormula(restored);
		ASSERT_TRUE(parsed);
		EXPECT_EQ(parsed->side, BattleSide::DEFENDER);
		EXPECT_EQ(std::to_string(parsed->token), token);
	}
}

TEST(OverwhelmingFormulaCapture, MalformedMetadataNeverRemovesOrdinaryContributorsOrAddsFormula)
{
	FormulaBattle battle;
	battle.states[0].lastCandidateCastToken = std::numeric_limits<uint64_t>::max();
	const auto checkInvalid = [&battle](const JsonNode & payload)
	{
		EXPECT_FALSE(spells::capturedOverwhelmingFormula(payload));
		EXPECT_EQ(spells::capturedMdrPenetrations(payload, 7, &battle), (std::vector<int>{20, 15}));
	};
	for(const std::string token : {"", "0", "00", "01", "+1", "-1", " 1", "1 ", "1.0",
		"1x", "18446744073709551616", "999999999999999999999"})
		checkInvalid(capture(BattleSide::ATTACKER, token));
	for(const double side : {-1.0, 2.0, 0.5, std::numeric_limits<double>::infinity()})
	{
		auto payload = capture(BattleSide::ATTACKER, "1");
		payload["overwhelmingFormulaSide"].Float() = side;
		checkInvalid(payload);
	}
	auto numeric = capture(BattleSide::ATTACKER, "1");
	numeric["overwhelmingFormulaToken"].Integer() = 1;
	checkInvalid(numeric);
	for(const auto * field : {"overwhelmingFormulaSide", "overwhelmingFormulaToken"})
	{
		auto missing = capture(BattleSide::ATTACKER, "1");
		missing.Struct().erase(field);
		checkInvalid(missing);
	}
}

TEST(OverwhelmingFormulaCapture, DynamicContributorRespectsRegisteredTokenWinningCastAndCasterSide)
{
	FormulaBattle battle;
	battle.states[0].lastCandidateCastToken = 2;
	const auto first = capture(BattleSide::ATTACKER, "1");
	const auto second = capture(BattleSide::ATTACKER, "2");
	const auto before = battle.states;
	EXPECT_EQ(spells::capturedMdrPenetrations(first, 7), (std::vector<int>{20, 15}));
	EXPECT_EQ(spells::capturedMdrPenetrations(first, 7, &battle), (std::vector<int>{20, 15, 50}));
	EXPECT_EQ(spells::capturedMdrPenetrations(capture(BattleSide::ATTACKER, "3"), 7, &battle),
		(std::vector<int>{20, 15}));
	EXPECT_EQ(spells::capturedMdrPenetrations(capture(BattleSide::DEFENDER, "1"), 7, &battle),
		(std::vector<int>{20, 15}));
	EXPECT_EQ(battle.states, before);
	ASSERT_TRUE(battle.states[0].claimActualDamage(2, true, 1, true));
	EXPECT_EQ(spells::capturedMdrPenetrations(first, 7, &battle), (std::vector<int>{20, 15}));
	EXPECT_EQ(spells::capturedMdrPenetrations(second, 7, &battle), (std::vector<int>{20, 15, 50}));
}

TEST(OverwhelmingFormulaCapture, FormulaComposesIndependentlyWithWarcastingAndTargetQualifiedContributors)
{
	FormulaBattle battle;
	battle.states[0].lastCandidateCastToken = 1;
	auto payload = capture(BattleSide::ATTACKER, "1");
	const auto contributors = spells::capturedMdrPenetrations(payload, 7, &battle);
	EXPECT_EQ(spells::calculateMagicalDamageReduction(10000, {50, 20}, contributors).damageWithPenetration,
		7960);
	payload["focusedTargetUnitId"].Integer() = 7;
	payload["focusedPenetrationPercent"].Integer() = 20;
	EXPECT_EQ(spells::capturedMdrPenetrations(payload, 7, &battle), (std::vector<int>{20, 15, 20, 50}));
	EXPECT_EQ(spells::capturedMdrPenetrations(payload, 8, &battle), contributors);
	// Bad historical contributor arrays still obey their existing fail-closed rule.
	payload["penetrations"].Vector().emplace_back(101);
	EXPECT_TRUE(spells::capturedMdrPenetrations(payload, 7, &battle).empty());
}

TEST_F(OverwhelmingFormulaDamageTest, DetectsFractionalProtectionEvenWhenBothRoundedDamageValuesMatch)
{
	target.addNewBonus(reduction(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, 1, SpellSchool::ANY));
	EXPECT_TRUE(spell.hasApplicableMagicalDamageReduction(&target));
	EXPECT_FALSE(spell.hasApplicableMagicalDamageReduction(&target, 0, 0, false));
	EXPECT_EQ(damage(1, true), 0);
	EXPECT_EQ(damage(1, true, 0, 0, {50}), 0);
	EXPECT_EQ(damage(10000, true), 9999);
	EXPECT_EQ(damage(10000, true, 0, 0, {50}), 9999);
}

TEST_F(OverwhelmingFormulaDamageTest, DetectionAndDamageShareCurrentMatchingSchoolAndAnySources)
{
	spell.schools.insert(SpellSchool::AIR);
	target.addNewBonus(reduction(BonusType::SPELL_DAMAGE_REDUCTION, 80, SpellSchool::FIRE));
	EXPECT_FALSE(spell.hasApplicableMagicalDamageReduction(&target));
	EXPECT_EQ(damage(10000, true), 10000);
	target.addNewBonus(reduction(BonusType::SPELL_DAMAGE_REDUCTION, 50, SpellSchool::AIR));
	target.addNewBonus(reduction(BonusType::SPELL_DAMAGE_REDUCTION, 20, SpellSchool::ANY));
	EXPECT_TRUE(spell.hasApplicableMagicalDamageReduction(&target));
	EXPECT_EQ(damage(10000, true), 4000);
	EXPECT_EQ(damage(10000, true, 0, 0, {50}), 7000);
}

TEST_F(OverwhelmingFormulaDamageTest, ZeroSourcesAreInertWhileCurrentHoldAndPerkBasisPointsAreApplicable)
{
	target.addNewBonus(reduction(BonusType::SPELL_DAMAGE_REDUCTION, 0, SpellSchool::ANY));
	target.addNewBonus(reduction(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, -10, SpellSchool::ANY));
	EXPECT_FALSE(spell.hasApplicableMagicalDamageReduction(&target));
	EXPECT_FALSE(spell.hasApplicableMagicalDamageReduction(nullptr, 100, 100));
	EXPECT_TRUE(spell.hasApplicableMagicalDamageReduction(&target, 1, 0));
	EXPECT_TRUE(spell.hasApplicableMagicalDamageReduction(&target, 0, 1));
	EXPECT_EQ(damage(10000, true, 5000, 2000), 4000);
	EXPECT_EQ(damage(10000, true, 5000, 2000, {20, 15, 50}), 7960);
}
