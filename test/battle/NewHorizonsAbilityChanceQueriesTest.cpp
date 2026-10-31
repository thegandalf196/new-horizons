/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../mock/mock_vstd_RNG.h"
#include "../../lib/battle/AttackAbilityQueries.h"
#include "../../lib/battle/CreatureAbilityChance.h"
#include "../../lib/battle/NewHorizonsToxicSting.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/spells/CSpell.h"
#if ENABLE_BATTLE_AI
#include "../../AI/BattleAI/NominalAbilityChance.h"
#include "../../AI/BattleAI/AttackAbilityPrefix.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#endif
namespace
{
class AbilityQueryTest : public BattleTestFixture
{
protected:
	CStack * source = nullptr;
	CStack * target = nullptr;
	void prepare(bool scorpicore = false)
	{
		startGame(); startBattle();
		source = addStack(BattleSide::ATTACKER, creatureByName(scorpicore ? "core:scorpicore" : "core:manticore"), BattleHex(leftHex), 10);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
		ASSERT_NE(source, nullptr); ASSERT_NE(target, nullptr);
	}
	void spellEffect(SpellID spell, BonusType type = BonusType::STACKS_SPEED)
	{
		target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::SPELL_EFFECT,
			1, BonusSourceID(spell)));
	}
	std::shared_ptr<Bonus> layered(SpellID id, int layer)
	{
		auto result = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_AFTER_ATTACK,
			BonusSource::OTHER, 20, BonusSourceID(), BonusSubtypeID(id));
		result->parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{0, 2, layer});
		return result;
	}
};
}
TEST_F(AbilityQueryTest, ActiveCombatSpellQueryPreservesDistinctEffectsAndExcludesNoneAndAdventure)
{
	prepare(); spellEffect(SpellID::HASTE); spellEffect(SpellID::HASTE, BonusType::STACKS_INITIATIVE);
	spellEffect(SpellID::SHIELD); spellEffect(SpellID::SLOW, BonusType::NONE); spellEffect(SpellID::DIMENSION_DOOR);
	const auto actual = attackAbilityQueries::activeSpells(*target);
	EXPECT_EQ(actual, target->activeSpells());
	EXPECT_EQ(std::set<SpellID>(actual.begin(), actual.end()), (std::set<SpellID>{SpellID::HASTE, SpellID::SHIELD}));
	EXPECT_EQ(actual.size(), 2u);
}
TEST_F(AbilityQueryTest, BackupLayersRetainUnlayeredAndSelectFirstMissingOrFinalLayer)
{
	prepare(); auto bonuses = std::make_shared<BonusList>();
	auto unlayered = layered(SpellID::HASTE, -1); bonuses->push_back(unlayered);
	bonuses->push_back(layered(SpellID::PARALYZE, 0)); bonuses->push_back(layered(SpellID::SLOW, 1));
	bonuses->push_back(layered(SpellID::POISON, 2));
	EXPECT_EQ(attackAbilityQueries::spellsForAttackCasting(bonuses, *target), (std::set<SpellID>{SpellID::HASTE, SpellID::PARALYZE}));
	spellEffect(SpellID::PARALYZE);
	EXPECT_EQ(attackAbilityQueries::spellsForAttackCasting(bonuses, *target), (std::set<SpellID>{SpellID::HASTE, SpellID::SLOW}));
	spellEffect(SpellID::SLOW); spellEffect(SpellID::POISON);
	EXPECT_EQ(attackAbilityQueries::spellsForAttackCasting(bonuses, *target), (std::set<SpellID>{SpellID::HASTE, SpellID::POISON}));
}
TEST_F(AbilityQueryTest, PerSpellParametersPreserveRangeUnionMaximumLevelAndChanceCap)
{
	prepare(); auto melee = layered(SpellID::PARALYZE, 0); melee->val = 60;
	melee->stacking = "query-test:melee"; melee->parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{3, 2, 0});
	source->addNewBonus(melee);
	const auto first = attackAbilityQueries::parameters(*source, BonusType::SPELL_AFTER_ATTACK, SpellID::PARALYZE, false);
	EXPECT_TRUE(first.castMe); EXPECT_EQ(first.spellLevel, 3); EXPECT_EQ(first.chance, 60);
	EXPECT_FALSE(attackAbilityQueries::parameters(*source, BonusType::SPELL_AFTER_ATTACK, SpellID::PARALYZE, true).castMe);
	auto ranged = layered(SpellID::PARALYZE, 0); ranged->val = 60;
	ranged->stacking = "query-test:ranged"; ranged->parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{2, 1, 0});
	source->addNewBonus(ranged);
	const auto combined = attackAbilityQueries::parameters(*source, BonusType::SPELL_AFTER_ATTACK, SpellID::PARALYZE, true);
	EXPECT_TRUE(combined.castMe); EXPECT_EQ(combined.spellLevel, 3); EXPECT_EQ(combined.chance, 100);
}
TEST_F(AbilityQueryTest, SharedFavorableQueryRetainsIntegerIdentityAndNativeSingleFractionalDraw)
{
	prepare(); GameRandomizer actual(*gameState()); actual.setSeed(31); CRandomGenerator control(31);
	const auto integer = creatureAbilityChance::rounding(*source, 20);
	EXPECT_EQ(integer.wholePercent, 20); EXPECT_EQ(integer.remainderBasisPoints, 0);
	EXPECT_EQ(actual.prepareFavorableCreatureAbilityChance(*source, 20), 20);
	EXPECT_EQ(actual.getDefault().nextInt(0, 999), control.nextInt(0, 999));
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS, BonusSource::OTHER, 4125, BonusSourceID()));
	const auto fractional = creatureAbilityChance::rounding(*source, 20);
	EXPECT_EQ(fractional.wholePercent, 8); EXPECT_EQ(fractional.remainderBasisPoints, 25);
	actual.setSeed(31); control.setSeed(31);
	const auto expected = 8 + (control.nextInt(0, 99) < 25 ? 1 : 0);
	EXPECT_EQ(actual.prepareFavorableCreatureAbilityChance(*source, 20), expected);
	EXPECT_EQ(actual.getDefault().nextInt(0, 999), control.nextInt(0, 999));
}
TEST_F(AbilityQueryTest, NativeRoundingIdentityKeepsHistoricalOutOfRangeReturnWithoutADraw)
{
	prepare(); GameRandomizer actual(*gameState()); actual.setSeed(31); CRandomGenerator control(31);
	EXPECT_EQ(actual.prepareFavorableCreatureAbilityChance(*source, -7), -7);
	EXPECT_EQ(actual.prepareFavorableCreatureAbilityChance(*source, 107), 107);
	EXPECT_EQ(actual.getDefault().nextInt(0, 999), control.nextInt(0, 999));
}
TEST(AbilityChanceCharacterization, ActualBiasedRerollIsDistinctFromNominalIndependentSquare)
{
	vstd::RNGMock draws;
	EXPECT_CALL(draws, nextInt(1, 100)).WillOnce(::testing::Return(1)).WillOnce(::testing::Return(1));
	RandomizationBias bias;
	const auto first = bias.rollReceipt(draws, 20, 100, 25); ASSERT_TRUE(first.triggered());
	const auto second = bias.rollReceipt(draws, 20, 100, 25);
	EXPECT_EQ(second.priorBias, -2000); EXPECT_FALSE(second.triggered());
	// Even the minimum second draw cannot succeed at this actual saved bias.
	for(int draw = 1; draw <= 100; ++draw)
	{
		auto proof = second; proof.draw = draw; EXPECT_FALSE(proof.triggered());
	}
}
#if ENABLE_BATTLE_AI
TEST(NominalAbilityChanceTest, ConditionalResourceMassPreservesZeroHundredAndNominalTwenty)
{
	const auto zero = nominalAbilityChance::harmful({0, 0}, true), certain = nominalAbilityChance::harmful({100, 0}, true);
	EXPECT_DOUBLE_EQ(zero[0], 1); EXPECT_DOUBLE_EQ(certain[1], 1);
	const auto twenty = nominalAbilityChance::harmful({20, 0}, true);
	EXPECT_NEAR(twenty[0], .8, 1e-12); EXPECT_NEAR(twenty[2], .16, 1e-12); EXPECT_NEAR(twenty[3], .04, 1e-12);
	EXPECT_NEAR(nominalAbilityChance::success({20, 0}, 0), .2, 1e-12);
}
TEST(NominalAbilityChanceTest, FractionalChanceUsesWeightedIntegerSquaresInsteadOfSquareOfMean)
{
	EXPECT_NEAR(nominalAbilityChance::success({8, 25}, 1), .006825, 1e-12);
	EXPECT_NEAR(nominalAbilityChance::availableAfter({8, 25}, 1), .9175, 1e-12);
	EXPECT_NE(nominalAbilityChance::success({8, 25}, 1), .0825 * .0825);
	EXPECT_NEAR(nominalAbilityChance::success({0, 50}, 1), .00005, 1e-12);
	EXPECT_NEAR(nominalAbilityChance::availableAfter({0, 50}, 1), .995, 1e-12);
	EXPECT_NEAR(nominalAbilityChance::success({99, 50}, 1), .99005, 1e-12);
	EXPECT_NEAR(nominalAbilityChance::availableAfter({99, 50}, 1), .505, 1e-12);
}
TEST(NominalAbilityChanceTest, ScorpicorePrefixConsumesVisibleAllowanceBeforeThePoisonModel)
{
	const auto afterParalysis = nominalAbilityChance::availableAfter({20, 0}, 1);
	EXPECT_NEAR(afterParalysis, .8, 1e-12);
	EXPECT_NEAR(nominalAbilityChance::success({20, 0}, afterParalysis), .072, 1e-12);
	// These are nominal marginal model values, not default abilityBias25 native probabilities.
	EXPECT_NEAR(nominalAbilityChance::success({20, 0}, 1), .04, 1e-12);
}
TEST_F(AbilityQueryTest, SharedNativeScorpicoreSelectionAndMechanicsDriveReadOnlyNominalPrefix)
{
	prepare(true); ASSERT_TRUE(SpellID(SpellID::PARALYZE).toSpell()->isNegative());
	AdverseCombatRerollState adverse; adverse.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, adverse);
	const auto sourceBefore = source->save(), targetBefore = target->save();
	const auto prefix = attackAbilityPrefix::query(*battle(), *source, *target, false);
	EXPECT_TRUE(prefix.adverseFactVisible); EXPECT_TRUE(prefix.eligibilityStableForPoison);
	EXPECT_EQ(prefix.eligibleHarmfulAttempts, (std::vector<SpellID>{SpellID::PARALYZE}));
	EXPECT_NEAR(prefix.nominalRerollAvailability, .8, 1e-12);
	EXPECT_EQ(source->save(), sourceBefore); EXPECT_EQ(target->save(), targetBefore);
	EXPECT_EQ(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER), adverse);
}
TEST_F(AbilityQueryTest, ActualParalysisSpellImmunitySkipsThePrefixAttemptButNotPhysicalPoisonEligibility)
{
	prepare(true); AdverseCombatRerollState adverse; adverse.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, adverse);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(SpellID(SpellID::PARALYZE))));
	const auto prefix = attackAbilityPrefix::query(*battle(), *source, *target, false);
	EXPECT_TRUE(prefix.eligibleHarmfulAttempts.empty()); EXPECT_DOUBLE_EQ(prefix.nominalRerollAvailability, 1);
	EXPECT_TRUE(newHorizonsToxicSting::livingTarget(*target));
}
TEST_F(AbilityQueryTest, OtherEligiblePassiveCastsAreReportedAsUnvaluedEligibilityChanges)
{
	prepare(true); AdverseCombatRerollState adverse; adverse.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, adverse);
	auto curse = layered(SpellID::CURSE, -1); source->addNewBonus(curse);
	const auto prefix = attackAbilityPrefix::query(*battle(), *source, *target, false);
	EXPECT_FALSE(prefix.eligibilityStableForPoison);
	EXPECT_EQ(prefix.eligibleHarmfulAttempts, (std::vector<SpellID>{SpellID::CURSE, SpellID::PARALYZE}));
	EXPECT_NEAR(prefix.nominalRerollAvailability, .64, 1e-12);
	EXPECT_EQ(battle()->getAdverseCombatRerollState(BattleSide::DEFENDER), adverse);
}
TEST_F(AbilityQueryTest, HiddenRecipientPolicyDoesNotExposeItsRerollAndNoAbilityCreatesNoPrefix)
{
	prepare(); AdverseCombatRerollState adverse; adverse.enabled = true;
	battle()->setAdverseCombatRerollState(BattleSide::DEFENDER, adverse);
	CPlayerBattleCallback visible(battle(), PlayerColor(0));
	const auto hidden = attackAbilityPrefix::query(visible, *source, *target, false);
	EXPECT_FALSE(hidden.adverseFactVisible); EXPECT_DOUBLE_EQ(hidden.nominalRerollAvailability, 0);
	EXPECT_TRUE(hidden.eligibleHarmfulAttempts.empty());
	const auto known = attackAbilityPrefix::query(*battle(), *source, *target, false);
	EXPECT_TRUE(known.adverseFactVisible); EXPECT_DOUBLE_EQ(known.nominalRerollAvailability, 1);
}
#endif
