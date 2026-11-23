/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/spells/NewHorizonsMetamagicResolution.h"

namespace
{
using namespace newHorizonsMetamagic;

Bonus effect(SpellID spell, PlayerColor caster = PlayerColor(1))
{
	Bonus bonus;
	bonus.type = BonusType::STACKS_SPEED;
	bonus.source = BonusSource::SPELL_EFFECT;
	bonus.sid = BonusSourceID(spell);
	bonus.spellCasterOwner = caster;
	bonus.duration = BonusDuration::N_TURNS;
	bonus.turnsRemain = 3;
	return bonus;
}

EnergyResolver definitions(bool negative = false)
{
	return [negative](SpellID spell) -> std::optional<EnergyDefinition>
	{
		if(spell == SpellID(100))
			return std::nullopt;
		const auto family = spell == SpellID(11) ? SpellID(1) : spell;
		return EnergyDefinition{family, family == SpellID(1) ? 4 : 11, true, !negative, negative, true};
	};
}

std::vector<RemovalCandidate> collect(const battle::BattleEffectSnapshot & snapshot,
	ResolutionKind kind = ResolutionKind::OFFENSE, uint32_t recipient = 7)
{
	return collectRemovalCandidates(recipient, PlayerColor(0), snapshot, kind,
		definitions(kind == ResolutionKind::RESTORATION));
}
}

TEST(NewHorizonsMetamagicResolution, CompositeAndMassAliasesFormOneLogicalEffect)
{
	battle::BattleEffectSnapshot snapshot;
	snapshot.effects = {effect(SpellID(1)), effect(SpellID(11)), effect(SpellID(2))};
	snapshot.effects[1].type = BonusType::PRIMARY_SKILL;
	const auto candidates = collect(snapshot);
	ASSERT_EQ(candidates.size(), 2);
	EXPECT_EQ(candidates[0].bonuses.size(), 2);
	EXPECT_EQ(candidates[0].family, SpellID(1));
	EXPECT_EQ(candidates[0].listedMana, 4);
	EXPECT_EQ(candidates[1].listedMana, 11);
}

TEST(NewHorizonsMetamagicResolution, PhysicalPermanentIntrinsicAndUnknownEffectsSupplyNoEnergy)
{
	battle::BattleEffectSnapshot snapshot;
	auto physical = effect(SpellID(1));
	physical.type = BonusType::PHYSICAL_AFFLICTION;
	auto permanent = effect(SpellID(2));
	permanent.duration = BonusDuration::PERMANENT;
	auto intrinsic = effect(SpellID(3));
	intrinsic.source = BonusSource::CREATURE_ABILITY;
	snapshot.effects = {physical, permanent, intrinsic, effect(SpellID(100))};
	EXPECT_TRUE(collect(snapshot).empty());
}

TEST(NewHorizonsMetamagicResolution, FriendlyCleansingRequiresKnownOpposingCaster)
{
	battle::BattleEffectSnapshot snapshot;
	snapshot.effects = {effect(SpellID(1), PlayerColor(0)),
		effect(SpellID(2), PlayerColor::CANNOT_DETERMINE), effect(SpellID(3), PlayerColor(1))};
	const auto candidates = collect(snapshot, ResolutionKind::RESTORATION);
	ASSERT_EQ(candidates.size(), 1);
	EXPECT_EQ(candidates.front().spell, SpellID(3));
}

TEST(NewHorizonsMetamagicResolution, EachRecipientEffectRollsOnceAcrossRepeatedHitsAndFailedRolls)
{
	battle::BattleEffectSnapshot snapshot;
	snapshot.effects = {effect(SpellID(1)), effect(SpellID(1)), effect(SpellID(2))};
	const auto first = collect(snapshot);
	const auto second = collect(snapshot, ResolutionKind::OFFENSE, 8);
	ResolutionLedger ledger;
	int rolls = 0;
	const auto succeed = [&](int chance) { ++rolls; EXPECT_EQ(chance, 40); return true; };
	EXPECT_TRUE(ledger.attempt(first[0], 3, succeed));
	EXPECT_FALSE(ledger.attempt(first[0], 3, succeed));
	EXPECT_TRUE(ledger.attempt(second[0], 3, succeed));
	EXPECT_FALSE(ledger.attempt(first[1], 3, [&](int) { ++rolls; return false; }));
	EXPECT_FALSE(ledger.attempt(first[1], 3, succeed));
	EXPECT_EQ(rolls, 3);
	EXPECT_EQ(ledger.removedMana(), 0);
}

TEST(NewHorizonsMetamagicResolution, CreditRequiresCompleteActualRemovalAndCannotBeRepeated)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	battle::BattleEffectSnapshot before;
	before.effects = {effect(SpellID(1)), effect(SpellID(11)), effect(SpellID(2))};
	const auto candidates = collect(before);
	ResolutionLedger ledger;
	ASSERT_TRUE(ledger.attempt(candidates[0], 1, [](int chance) { EXPECT_EQ(chance, 20); return true; }));
	EXPECT_FALSE(ledger.confirmRemoval(candidates[0], before, before));
	auto partial = before;
	partial.effects.erase(partial.effects.begin());
	EXPECT_FALSE(ledger.confirmRemoval(candidates[0], before, partial));
	auto after = before;
	after.effects.erase(after.effects.begin(), after.effects.begin() + 2);
	EXPECT_TRUE(ledger.confirmRemoval(candidates[0], before, after));
	EXPECT_FALSE(ledger.confirmRemoval(candidates[0], before, after));
	EXPECT_EQ(ledger.removedMana(), 4);
	EXPECT_EQ(ledger.removedMana(7), 4);
	EXPECT_EQ(ledger.removedMana(8), 0);
	EXPECT_EQ(dischargeHitPoints(ledger.removedMana()), 40);
	EXPECT_EQ(restorationHitPoints(ledger.removedMana()), 60);
}

TEST(NewHorizonsMetamagicResolution, StaleOrUnattemptedRemovalNeverEarnsPayment)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	battle::BattleEffectSnapshot before;
	before.effects = {effect(SpellID(1))};
	const auto candidate = collect(before).front();
	ResolutionLedger ledger;
	const battle::BattleEffectSnapshot empty;
	EXPECT_FALSE(ledger.confirmRemoval(candidate, before, empty));
	ASSERT_TRUE(ledger.attempt(candidate, 2, [](int chance) { EXPECT_EQ(chance, 30); return true; }));
	auto stale = before;
	stale.effects.front().turnsRemain = 1;
	EXPECT_FALSE(ledger.confirmRemoval(candidate, stale, empty));
	EXPECT_EQ(ledger.removedMana(), 0);
}

TEST(NewHorizonsMetamagicResolution, TriggeredCastsAndMissingSkillCannotRoll)
{
	battle::BattleEffectSnapshot snapshot;
	snapshot.effects = {effect(SpellID(1))};
	const auto candidate = collect(snapshot).front();
	ResolutionLedger triggered(false);
	ResolutionLedger ordinary;
	const auto forbidden = [](int) { ADD_FAILURE(); return true; };
	EXPECT_FALSE(triggered.attempt(candidate, 3, forbidden));
	EXPECT_FALSE(ordinary.attempt(candidate, 0, forbidden));
	EXPECT_FALSE(ordinary.attempt(candidate, 4, forbidden));
	EXPECT_FALSE(ordinary.attempt(candidate, -1, forbidden));
}

TEST(NewHorizonsMetamagicResolution, RefundUsesGlobalEnergyAndActualPaymentWithCapacityAndCombinedCaps)
{
	EXPECT_EQ(recyclingRefund(12, 5, 0, 100), 2);
	EXPECT_EQ(recyclingRefund(28, 12, 0, 100), 6);
	EXPECT_EQ(recyclingRefund(4, 4, 0, 100), 1);
	EXPECT_EQ(recyclingRefund(25, 4, 0, 100), 2);
	EXPECT_EQ(recyclingRefund(100, 0, 0, 100), 0);
	EXPECT_EQ(recyclingRefund(100, 12, 11, 100), 1);
	EXPECT_EQ(recyclingRefund(100, 12, 0, 3), 3);
	EXPECT_EQ(conservedFormulaRefund(12, 0, 100, true, true), 12);
	EXPECT_EQ(conservedFormulaRefund(12, 6, 100, true, true), 6);
	EXPECT_EQ(conservedFormulaRefund(12, 0, 3, true, true), 3);
	EXPECT_EQ(conservedFormulaRefund(12, 0, 100, false, true), 0);
	EXPECT_EQ(conservedFormulaRefund(12, 0, 100, true, false), 0);
	EXPECT_EQ(recyclingRefund(-1, -1, -1, -1), 0);
}

TEST(NewHorizonsMetamagicResolution, PayoutsUseOnlyRemovedManaAndSaturateWithoutOverflow)
{
	EXPECT_EQ(dischargeHitPoints(15), 150);
	EXPECT_EQ(restorationHitPoints(15), 225);
	EXPECT_EQ(dischargeHitPoints(-1), 0);
	EXPECT_EQ(restorationHitPoints(0), 0);
	EXPECT_EQ(dischargeHitPoints(std::numeric_limits<int64_t>::max()), std::numeric_limits<int64_t>::max());
	EXPECT_EQ(restorationHitPoints(std::numeric_limits<int64_t>::max()), std::numeric_limits<int64_t>::max());
}

TEST(NewHorizonsMetamagicResolution, CurrentPolicyIsExplicitAndOwnsRankChanceHalonAndPayoutNumbers)
{
	JsonNode rules;
	EXPECT_FALSE(resolutionPolicy(rules));
	auto & policy = rules["metamagicPolicy"];
	policy["version"].Integer() = 4;
	policy["removalChancePercent"].Vector() = {JsonNode(0), JsonNode(20), JsonNode(30), JsonNode(40)};
	policy["damagePerMana"].Integer() = 10;
	policy["restorationPerMana"].Integer() = 15;
	policy["halonRemovalChanceBonusPercent"].Integer() = 5;
	const auto captured = resolutionPolicy(rules);
	ASSERT_TRUE(captured);
	EXPECT_EQ(removalChancePercent(*captured, 1, false), 20);
	EXPECT_EQ(removalChancePercent(*captured, 2, false), 30);
	EXPECT_EQ(removalChancePercent(*captured, 3, false), 40);
	EXPECT_EQ(removalChancePercent(*captured, 3, true), 45);
	EXPECT_EQ(removalChancePercent(*captured, 0, true), 0);
	EXPECT_EQ(energyHitPoints(15, captured->damagePerMana), 150);
	EXPECT_EQ(energyHitPoints(15, captured->restorationPerMana), 225);
	policy.Struct().erase("damagePerMana");
	EXPECT_FALSE(resolutionPolicy(rules));
	policy["damagePerMana"].Integer() = 10;
	policy["version"].Integer() = 3;
	EXPECT_FALSE(resolutionPolicy(rules));
}
TEST(NewHorizonsMetamagicResolution, CreatureAbilityWrappersCannotSupplySpellRemovalEnergy)
{
	battle::BattleEffectSnapshot snapshot;
	auto bonus = effect(SpellID(1));
	bonus.abilityDescriptor = creatureAbility::spellEffect(SpellID(1), creatureAbility::Family::CREATURE_ABILITY,
		creatureAbility::BENEFICIAL, creatureAbility::REMOVABLE | creatureAbility::DISPELLABLE, PlayerColor(1));
	snapshot.effects = {bonus};
	EXPECT_TRUE(collect(snapshot).empty());
	bonus.abilityDescriptor = creatureAbility::spellEffect(SpellID(1), creatureAbility::Family::HERO_SPELL,
		creatureAbility::BENEFICIAL, creatureAbility::REMOVABLE | creatureAbility::DISPELLABLE, PlayerColor(1));
	snapshot.effects = {bonus};
	const auto candidates = collect(snapshot);
	ASSERT_EQ(candidates.size(), 1u);
	EXPECT_EQ(candidates.front().listedMana, 4);
}
TEST(NewHorizonsMetamagicResolution, DistinctCapturedProducerOriginsRemainSeparateLogicalSpellGroups)
{
	auto hero = effect(SpellID(1));
	auto creature = hero;
	hero.abilityDescriptor = creatureAbility::spellEffect(SpellID(1), creatureAbility::Family::HERO_SPELL,
		creatureAbility::BENEFICIAL, creatureAbility::REMOVABLE, PlayerColor(1));
	creature.abilityDescriptor = creatureAbility::spellEffect(SpellID(1), creatureAbility::Family::CREATURE_SPELL,
		creatureAbility::BENEFICIAL, creatureAbility::REMOVABLE, PlayerColor(1));
	battle::BattleEffectSnapshot snapshot;
	snapshot.effects = {hero, creature};
	const auto candidates = collect(snapshot);
	ASSERT_EQ(candidates.size(), 2u);
	ResolutionLedger ledger;
	int draws = 0;
	for(const auto & candidate : candidates)
		EXPECT_TRUE(ledger.attempt(candidate, 3, [&](int) { ++draws; return true; }));
	EXPECT_EQ(draws, 2);
}
