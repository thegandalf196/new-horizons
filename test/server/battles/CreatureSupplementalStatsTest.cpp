/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../client/windows/CreatureDefensePresentation.h"
#include "../../../client/windows/CreatureStatLayout.h"
#include "../../../client/windows/CreatureSupplementalStats.h"
#include "../../../lib/battle/UnitMagicResistanceQuery.h"
#include "../../../lib/battle/ArcaneInspiration.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../../lib/combatScripts/NativePhysicalProtection.h"
#include <limits>

namespace
{
class CreatureSupplementalStatsTest : public HeroCommandFixture
{
protected:
	CStack * subject = nullptr;
	CreatureSupplementalStatsTest() { useRevisedOrders = true; }
	void prepare(bool boneward = false, bool unbroken = false)
	{
		startGame();
		if(boneward)
		{
			attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:galthran")));
			const SecondarySkill skill(SecondarySkill::decode("new-horizons:necromancy"));
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:necromancy", "new-horizons:necromancy.darkConversion"});
		}
		if(unbroken)
		{
			const SecondarySkill skill(SecondarySkill::decode("new-horizons:command"));
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:command", "new-horizons:command.battlePlan"});
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:command", "new-horizons:command.crisisCommand"});
		}
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:skeleton"), 60));
		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
				subject = battle()->getStack(unit->unitId(), false);
		ASSERT_NE(subject, nullptr);
	}
	creatureSupplementalStats::Readout readout(bool inBattle = true)
	{
		return creatureSupplementalStats::query(*attackerSideHero->getStackPtr(SlotID(0)),
			inBattle ? subject : nullptr, attackerSideHero, attackerSideHero->getMagicRules());
	}
	static void addProtection(CStack & unit, BonusType type, int value, BonusSubtypeID subtype,
		BonusLimitEffect range, int artifact)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::ARTIFACT,
			value, BonusSourceID(ArtifactID(artifact)), subtype, BonusValueType::ADDITIVE_VALUE);
		bonus->effectRange = range;
		unit.addNewBonus(bonus);
	}
};
}

TEST(CreatureStatLayout, SupplementalRowsFollowInitiativeWithoutChangingLegacyRows)
{
	using namespace creatureStatLayout;
	EXPECT_EQ(row(Stat::INITIATIVE, true), 7u);
	EXPECT_EQ(row(Stat::RETALIATIONS, true), 8u);
	EXPECT_EQ(row(Stat::PHYSICAL_RESISTANCE, true), 9u);
	EXPECT_EQ(row(Stat::MAGICAL_RESISTANCE, true), 10u);
	EXPECT_EQ(row(Stat::MANA, false), 7u);
	EXPECT_EQ(row(Stat::LEADERSHIP, false), 8u);
	EXPECT_EQ(row(Stat::RANK, false), 9u);
	EXPECT_EQ(panelHeight(true, true) - panelHeight(false, true), 4 * ROW_HEIGHT);
	EXPECT_EQ(panelHeight(true, true), 301);
	EXPECT_LT(iconY(Stat::RANK, true) + 20, panelHeight(true, true));
}

TEST(CreatureSupplementalStats, SharedMagicResistanceArithmeticRetainsFloorAndCapturedCap)
{
	EXPECT_EQ(battle::combinedMagicResistance(0, 50, 0, true), 50);
	EXPECT_EQ(battle::combinedMagicResistance(0, 50, 20, true), 60);
	EXPECT_EQ(battle::combinedMagicResistance(20, 50, 20, true), 75);
	EXPECT_EQ(battle::combinedMagicResistance(20, 50, 20, false), 76);
	EXPECT_EQ(battle::combinedMagicResistance(1, 0, 1, true), 1);
	EXPECT_EQ(battle::combinedMagicResistance(-20, 0, -10, true), 0);
	EXPECT_EQ(battle::combinedMagicResistance(std::numeric_limits<int>::max(), 50, 100, true), 75);
	EXPECT_EQ(battle::combinedMagicResistance(100, std::numeric_limits<int>::max(), 100, false), 100);
}

TEST(CreatureSupplementalStats, ReadableCounterAndPercentageFormatting)
{
	using namespace creatureSupplementalStats;
	EXPECT_EQ(retaliationText({1, 1, false}), "1");
	EXPECT_EQ(retaliationText({0, 1, false}), "0/1");
	EXPECT_EQ(retaliationText({1, 2, false}), "1/2");
	EXPECT_EQ(retaliationText({0, 0, false}), "0");
	EXPECT_EQ(retaliationText({0, 1, true}), "∞");
	EXPECT_EQ(percentageText(0), "0%");
	EXPECT_EQ(percentageText(50), "0.5%");
	EXPECT_EQ(percentageText(1250), "12.5%");
	EXPECT_EQ(percentageText(7354), "73.54%");
	EXPECT_EQ(percentageText(10000), "100%");
}

TEST_F(CreatureSupplementalStatsTest, BonewardChanceMatchesBattleAndAdventureWithoutBecomingDamageReduction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	EXPECT_EQ(readout(false).magicResistancePercent, 50);
	EXPECT_EQ(readout().magicResistancePercent, 50);
	EXPECT_EQ(readout().magicResistancePercent, subject->magicResistance());
	subject->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PHYSICAL_DEFENSE,
		BonusSource::OTHER, 6, BonusSourceID()));
	ASSERT_TRUE(readout().physicalReductionBasisPoints);
	EXPECT_EQ(*readout().physicalReductionBasisPoints, 0);
	EXPECT_EQ(creatureDefensePresentation::query(*subject, false).physical, 10);
	auto frenzy = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::IN_FRENZY,
		BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::FRENZY)));
	subject->addNewBonus(frenzy);
	const auto defense = creatureDefensePresentation::query(*subject, false);
	EXPECT_EQ(defense.general, 0);
	EXPECT_EQ(defense.physical, 0);
}

TEST_F(CreatureSupplementalStatsTest, RetaliationsFollowSpentCounterAdditionalAllowanceAndRestrictions)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(creatureSupplementalStats::retaliationText(readout().retaliations), "1");
	subject->counterAttacks.use();
	EXPECT_EQ(creatureSupplementalStats::retaliationText(readout().retaliations), "0/1");
	subject->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::ADDITIONAL_RETALIATION,
		BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(creatureSupplementalStats::retaliationText(readout().retaliations), "1/2");
	EXPECT_EQ(creatureSupplementalStats::retaliationText(readout().retaliations), "1/2");
	subject->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::UNLIMITED_RETALIATIONS,
		BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_TRUE(readout().retaliations.unlimited);
	subject->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NO_RETALIATION,
		BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_FALSE(readout().retaliations.unlimited);
	EXPECT_EQ(creatureSupplementalStats::retaliationText(readout().retaliations), "0");
	EXPECT_EQ(creatureSupplementalStats::retaliationText(readout(false).retaliations), "1");
}

TEST_F(CreatureSupplementalStatsTest, UniversalPhysicalCaptureCannotPoisonWarmedCombatQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const bool baselineFirst : {false, true})
	{
		auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:skeleton"), BattleHex(6, baselineFirst ? 4 : 5), 60);
		ASSERT_NE(unit, nullptr);
		const BonusSubtypeID all(BonusCustomSubtype::damageTypeAll);
		addProtection(*unit, BonusType::GENERAL_DAMAGE_REDUCTION, 20, all, BonusLimitEffect::NO_LIMIT, 0);
		addProtection(*unit, BonusType::GENERAL_DAMAGE_REDUCTION, 10, all, BonusLimitEffect::NO_LIMIT, 0);
		addProtection(*unit, BonusType::GENERAL_DAMAGE_REDUCTION, 50, all, BonusLimitEffect::ONLY_MELEE_FIGHT, 1);
		addProtection(*unit, BonusType::GENERAL_DAMAGE_REDUCTION, 40, all, BonusLimitEffect::ONLY_DISTANCE_FIGHT, 1);
		addProtection(*unit, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, 1000, {}, BonusLimitEffect::NO_LIMIT, 0);
		addProtection(*unit, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, 2000, {}, BonusLimitEffect::ONLY_MELEE_FIGHT, 1);
		addProtection(*unit, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, 3000, {}, BonusLimitEffect::ONLY_DISTANCE_FIGHT, 1);
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
			BonusSource::SPELL_EFFECT, 90, BonusSourceID(SpellID(SpellID::SHIELD)), all, BonusValueType::ADDITIVE_VALUE));
		DamageAttackInfo melee;
		melee.defender = unit;
		melee.physicalDamageReductionCapPercent = 80;
		auto ranged = melee;
		ranged.shooting = true;
		if(baselineFirst)
			static_cast<void>(nativeDamage::captureUniversalPhysicalProtection(*unit));
		const auto beforeMelee = nativeDamage::capturePhysicalProtection(melee, true, true);
		const auto beforeRanged = nativeDamage::capturePhysicalProtection(ranged, true, true);
		const auto base = nativeDamage::captureUniversalPhysicalProtection(*unit);
		EXPECT_EQ(base.allDamageSourceValues, std::vector<double>{30});
		EXPECT_EQ(base.physicalBasisSourceValues, std::vector<double>{1000});
		EXPECT_TRUE(base.subtypeSourceValues.empty());
		EXPECT_NEAR(nativeDamage::physicalDamageReductionFactor(melee, base), 0.63, 1e-12);
		const auto afterMelee = nativeDamage::capturePhysicalProtection(melee, true, true);
		const auto afterRanged = nativeDamage::capturePhysicalProtection(ranged, true, true);
		EXPECT_EQ(afterMelee.allDamageSourceValues, beforeMelee.allDamageSourceValues);
		EXPECT_EQ(afterMelee.physicalBasisSourceValues, beforeMelee.physicalBasisSourceValues);
		EXPECT_EQ(afterRanged.allDamageSourceValues, beforeRanged.allDamageSourceValues);
		EXPECT_EQ(afterRanged.physicalBasisSourceValues, beforeRanged.physicalBasisSourceValues);
		EXPECT_NEAR(nativeDamage::physicalDamageReductionFactor(melee, afterMelee), 0.252, 1e-12);
		EXPECT_NEAR(nativeDamage::physicalDamageReductionFactor(ranged, afterRanged), 0.2646, 1e-12);
	}
}

TEST_F(CreatureSupplementalStatsTest, UnbrokenProtectionUsesCurrentBenefitOnceAfterPhysicalCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	addProtection(*subject, BonusType::GENERAL_DAMAGE_REDUCTION, 100,
		BonusSubtypeID(BonusCustomSubtype::damageTypeAll), BonusLimitEffect::NO_LIMIT, 0);
	EXPECT_EQ(readout().physicalReductionBasisPoints, 8000);
	beginCombat();
	// This fixture replaces the attacker's token with slower Skeletons. Select
	// their ordinary turn explicitly instead of assuming they win initiative.
	BattleSetActiveStack activation;
	activation.battleID = BattleID(0);
	activation.stack = subject->unitId();
	activation.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activation);
	ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(readout().physicalReductionBasisPoints, 8400);
	auto spent = *battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE);
	spent.consumedUnitIds = {subject->unitId()};
	battle()->setHeroOrderState(BattleSide::ATTACKER, spent);
	EXPECT_EQ(readout().physicalReductionBasisPoints, 8000);
}

TEST_F(CreatureSupplementalStatsTest, ArcaneBulwarkAndArmorerFollowCurrentController)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SecondarySkill armorer(SecondarySkill::decode(std::string(newHorizonsCombatSkills::ARMORER_SKILL_ID)));
	attackerSideHero->setSecSkillLevel(armorer, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(newHorizonsCombatSkills::armorerRank(attackerSideHero), MasteryLevel::EXPERT);
	const auto owner = battle()->battleGetOwner(subject);
	const auto other = battle()->otherPlayer(owner);
	subject->addNewBonus(std::make_shared<Bonus>(arcaneInspiration::activeMarker(
		{10, arcaneInspiration::BULWARK, false}, owner, battle()->getRound())));
	const int armorerPercent = newHorizonsCombatSkills::armorerReductionPercent(attackerSideHero);
	ASSERT_GT(armorerPercent, 0);
	EXPECT_EQ(readout().physicalReductionBasisPoints, 10000 - (100 - armorerPercent) * 90);
	subject->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(subject), other);
	ASSERT_EQ(battle()->battleGetOwnerHero(subject), defenderSideHero);
	EXPECT_EQ(readout().physicalReductionBasisPoints, 0)
		<< "Original army hero and original-owner inspiration must not leak through control changes";
	subject->addNewBonus(std::make_shared<Bonus>(arcaneInspiration::activeMarker(
		{10, arcaneInspiration::BULWARK, false}, other, battle()->getRound())));
	EXPECT_EQ(readout().physicalReductionBasisPoints, 1000);
}
