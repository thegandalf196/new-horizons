/*
 * NewHorizonsMisfortuneTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../server/CGameHandler.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/CSpell.h"

namespace
{
constexpr auto CHAOS_MAGIC = "new-horizons:chaosMagic";
constexpr auto MISFORTUNE_WEAVER = "new-horizons:chaosMagic.misfortuneWeaver";

SpellID misfortuneSpell()
{
	return SpellID(SpellID::MISFORTUNE);
}

JsonNode certainLuck()
{
	JsonNode result;
	for(int i = 0; i < 10; ++i)
		result.Vector().emplace_back(100);
	return result;
}

JsonNode savedV2Rules()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("selectedPlacement");
		spell.Struct().erase("earthquake");
		if(spell.Struct().contains("variant"))
		{
			spell.Struct().erase("variant");
			spell["active"].Bool() = false;
		}
	}
	newHorizonsMagic::validateRules(rules);
	return rules;
}

class NewHorizonsMisfortuneTest : public BattleTestFixture
{
protected:
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	CStack * target = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		const JsonNode rules = magicVersion == newHorizonsMagic::CURRENT_RULESET_VERSION
			? JsonNode(JsonPath::builtin("config/newHorizonsMagic")) : savedV2Rules();
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, certainLuck());
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, certainLuck());
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
	}

	void prepare(int spellPower = 0, MasteryLevel::Type chaosRank = MasteryLevel::NONE,
		MasteryLevel::Type spellcraftRank = MasteryLevel::NONE, bool selectWeaver = false,
		bool legacyV2 = false)
	{
		magicVersion = legacyV2 ? newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION
			: newHorizonsMagic::CURRENT_RULESET_VERSION;
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(misfortuneSpell());
		attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
		defenderSideHero->addSpellToSpellbook(SpellID::DISPEL);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		const SecondarySkill chaos(SecondarySkill::decode(std::string(CHAOS_MAGIC)));
		const SecondarySkill spellcraft(SecondarySkill::decode("new-horizons:spellcraft"));
		attackerSideHero->setSecSkillLevel(chaos, chaosRank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(spellcraft, spellcraftRank, ChangeValueMode::ABSOLUTE);
		if(selectWeaver)
			attackerSideHero->applyPerkSelection({CHAOS_MAGIC, MISFORTUNE_WEAVER});
		setTestSpellPointTotal(attackerSideHero, 1000);
		setTestSpellPointTotal(defenderSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 20);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 20);
		beginCombat();
	}

	bool castMisfortune(const CStack * unit) const
	{
		return castOn(attackerSideHero, misfortuneSpell(), unit);
	}

	TConstBonusListPtr misfortuneBonuses(const CStack * unit) const
	{
		return unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(misfortuneSpell())));
	}

	const Bonus * misfortuneBonus(const CStack * unit, BonusType type) const
	{
		const auto bonuses = misfortuneBonuses(unit);
		if(!bonuses)
			return nullptr;
		for(const auto & bonus : *bonuses)
			if(bonus && bonus->type == type)
				return bonus.get();
		return nullptr;
	}

	void addLuck(CStack * unit, int value)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LUCK, BonusSource::OTHER, value, BonusSourceID()));
	}
};
}

TEST_F(NewHorizonsMisfortuneTest, SavedV3FormulaAddsFractionalChanceMultiplierAndUsesCappedDuration)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100));
	ASSERT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		attackerSideHero->getMagicRules(), attackerSideHero, misfortuneSpell()), 10000);
	ASSERT_TRUE(castMisfortune(target));

	const auto bonuses = misfortuneBonuses(target);
	ASSERT_NE(bonuses, nullptr);
	ASSERT_EQ(bonuses->size(), 2u);
	const auto * luckCap = misfortuneBonus(target, BonusType::MAXIMUM_LUCK);
	const auto * chance = misfortuneBonus(target, BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS);
	ASSERT_NE(luckCap, nullptr);
	ASSERT_NE(chance, nullptr);
	EXPECT_EQ(luckCap->val, 0);
	EXPECT_EQ(chance->val, 5000) << "75% minus 0.25 percentage points per Spell Power";
	EXPECT_EQ(luckCap->duration, BonusDuration::N_TURNS);
	EXPECT_EQ(chance->duration, BonusDuration::N_TURNS);
	EXPECT_EQ(luckCap->turnsRemain, 3);
	EXPECT_EQ(chance->turnsRemain, 3) << "2 + floor(100 / 80), capped at four before cast adjustments";
	EXPECT_EQ(target->favorableCreatureAbilityChanceBasisPoints(40), 2000);
}

TEST_F(NewHorizonsMisfortuneTest, SpellHelpExplainsTheCurrentMechanicAndSelectedWeaver)
{
	ASSERT_NO_FATAL_FAILURE(prepare(70, MasteryLevel::BASIC, MasteryLevel::NONE, true));
	const auto help = newHorizonsMagic::spellDescriptionForHero(attackerSideHero,
		misfortuneSpell().toSpell(), MasteryLevel::BASIC);
	EXPECT_NE(help.find("Positive Luck cannot trigger"), std::string::npos);
	EXPECT_NE(help.find("negative Luck is unchanged"), std::string::npos);
	EXPECT_NE(help.find("Deterministic abilities remain deterministic"), std::string::npos);
	EXPECT_NE(help.find("Misfortune Weaver subtracts another ten percentage points"), std::string::npos);
}

TEST_F(NewHorizonsMisfortuneTest, SchoolAndSpellcraftScaleOnlyTheSpellPowerTerms)
{
	ASSERT_NO_FATAL_FAILURE(prepare(70, MasteryLevel::BASIC, MasteryLevel::BASIC));
	ASSERT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		attackerSideHero->getMagicRules(), attackerSideHero, misfortuneSpell()), 12650);
	ASSERT_TRUE(castMisfortune(target));

	const auto * chance = misfortuneBonus(target, BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS);
	const auto * luckCap = misfortuneBonus(target, BonusType::MAXIMUM_LUCK);
	ASSERT_NE(chance, nullptr);
	ASSERT_NE(luckCap, nullptr);
	EXPECT_EQ(chance->val, 5287)
		<< "Only the floor(25 x Spell Power) term receives the 126.5% coefficient";
	EXPECT_EQ(luckCap->val, 0) << "School and Spellcraft do not change Misfortune's fixed Luck cap";
	EXPECT_EQ(chance->turnsRemain, 3);
}

TEST_F(NewHorizonsMisfortuneTest, UnselectedMisfortuneWeaverDoesNotChangeTheBaseCurve)
{
	ASSERT_NO_FATAL_FAILURE(prepare(70, MasteryLevel::BASIC));
	ASSERT_FALSE(attackerSideHero->hasActivePerk(CHAOS_MAGIC, MISFORTUNE_WEAVER));
	ASSERT_TRUE(castMisfortune(target));
	const auto * chance = misfortuneBonus(target, BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS);
	ASSERT_NE(chance, nullptr);
	EXPECT_EQ(chance->val, 5488) << "Unselected Weaver contributes no additional reduction";
	EXPECT_EQ(target->favorableCreatureAbilityChanceBasisPoints(40), 2195);
}

TEST_F(NewHorizonsMisfortuneTest, SelectedMisfortuneWeaverSubtractsTenMultiplierPercentagePoints)
{
	ASSERT_NO_FATAL_FAILURE(prepare(70, MasteryLevel::BASIC, MasteryLevel::NONE, true));
	ASSERT_TRUE(attackerSideHero->hasActivePerk(CHAOS_MAGIC, MISFORTUNE_WEAVER));
	ASSERT_TRUE(castMisfortune(target));
	const auto * chance = misfortuneBonus(target, BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS);
	ASSERT_NE(chance, nullptr);
	EXPECT_EQ(chance->val, 4488) << "Basic Chaos Magic Weaver subtracts ten multiplier percentage points";
	EXPECT_EQ(target->favorableCreatureAbilityChanceBasisPoints(40), 1795);
}

TEST_F(NewHorizonsMisfortuneTest, SelectedMisfortuneWeaverCannotPushChanceMultiplierBelowTwentyFivePercent)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1000, MasteryLevel::BASIC, MasteryLevel::NONE, true));
	ASSERT_TRUE(castMisfortune(target));
	const auto * chance = misfortuneBonus(target, BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS);
	ASSERT_NE(chance, nullptr);
	EXPECT_EQ(chance->val, 2500);
	EXPECT_EQ(chance->turnsRemain, 4);
}

TEST_F(NewHorizonsMisfortuneTest, DeathStareUsesMisfortuneAdjustedPerCreatureProbabilityAndCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100));
	CStack * victim = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100000);
	CStack * gorgons = addStack(BattleSide::DEFENDER, creatureByName("core:mightyGorgon"), BattleHex(rightHex), 1000);
	ASSERT_NE(victim, nullptr);
	ASSERT_NE(gorgons, nullptr);
	blockRetaliation(gorgons);
	ASSERT_TRUE(castMisfortune(gorgons));
	EXPECT_EQ(gorgons->favorableCreatureAbilityChanceBasisPoints(10), 500)
		<< "A ten-percent Death Stare becomes a fractional five-percent chance per creature";

	ASSERT_TRUE(attack(gorgons, victim->getPosition()));
	const auto casts = server.castsOf(SpellID::DEATH_STARE);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_GT(casts.front().killed, 0u);
	EXPECT_LE(casts.front().killed, 50u)
		<< "The per-creature binomial result is capped at ceil(1000 x 5%)";
}

TEST_F(NewHorizonsMisfortuneTest, ZeroLuckCapSuppressesPositiveLuckAndMaxLuck)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addLuck(target, 2);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAX_LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_GT(battle()->battleGetAttackLuck(target, nullptr, false), 0);
	ASSERT_TRUE(castMisfortune(target));
	EXPECT_EQ(battle()->battleGetAttackLuck(target, nullptr, false), 0)
		<< "The cap applies after the MAX_LUCK shortcut";
	EXPECT_EQ(target->valOfBonuses(BonusType::LUCK), 2) << "Misfortune must not erase Luck bonuses";
}

TEST_F(NewHorizonsMisfortuneTest, ZeroLuckCapPreservesNegativeLuck)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addLuck(target, -2);
	ASSERT_LT(battle()->battleGetAttackLuck(target, nullptr, false), 0);
	ASSERT_TRUE(castMisfortune(target));
	EXPECT_EQ(battle()->battleGetAttackLuck(target, nullptr, false), -2)
		<< "A zero maximum is an upper bound, not NO_LUCK";
}

TEST_F(NewHorizonsMisfortuneTest, DispelByTargetOwnerRemovesBothTimedEffects)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castMisfortune(target));
	ASSERT_EQ(misfortuneBonuses(target)->size(), 2u);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::DISPEL, target));
	EXPECT_TRUE(misfortuneBonuses(target)->empty());
}

TEST_F(NewHorizonsMisfortuneTest, OrdinaryExpiryRemovesBothTimedEffects)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castMisfortune(target));
	const auto * initialLuck = misfortuneBonus(target, BonusType::MAXIMUM_LUCK);
	const auto * initialChance = misfortuneBonus(target, BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS);
	ASSERT_NE(initialLuck, nullptr);
	ASSERT_NE(initialChance, nullptr);
	ASSERT_EQ(initialLuck->turnsRemain, 2);
	ASSERT_EQ(initialChance->turnsRemain, 2);
	const auto startingRound = battle()->getRound();
	ASSERT_GE(startingRound, 1) << "beginCombat has already started the first real round";
	endRound();
	ASSERT_EQ(battle()->getRound(), startingRound + 1);
	const auto * remainingLuck = misfortuneBonus(target, BonusType::MAXIMUM_LUCK);
	const auto * remainingChance = misfortuneBonus(target, BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS);
	ASSERT_NE(remainingLuck, nullptr);
	ASSERT_NE(remainingChance, nullptr);
	EXPECT_EQ(remainingLuck->turnsRemain, 1);
	EXPECT_EQ(remainingChance->turnsRemain, 1);
	endRound();
	EXPECT_EQ(battle()->getRound(), startingRound + 2);
	EXPECT_TRUE(misfortuneBonuses(target)->empty());
}

TEST_F(NewHorizonsMisfortuneTest, SavedV2KeepsLegacyLuckPenaltyWithoutChanceMultiplier)
{
	ASSERT_NO_FATAL_FAILURE(prepare(70, MasteryLevel::BASIC, MasteryLevel::NONE, false, true));
	addLuck(target, 2);
	ASSERT_TRUE(castMisfortune(target));
	EXPECT_NE(misfortuneBonus(target, BonusType::LUCK), nullptr);
	EXPECT_EQ(misfortuneBonus(target, BonusType::MAXIMUM_LUCK), nullptr);
	EXPECT_EQ(misfortuneBonus(target, BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS), nullptr);
	EXPECT_EQ(target->favorableCreatureAbilityChanceBasisPoints(40), 4000);
	EXPECT_EQ(target->valOfBonuses(BonusType::LUCK), 1) << "Legacy Misfortune still applies its original -1 Luck";
}
