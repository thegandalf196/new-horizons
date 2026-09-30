/*
 * NewHorizonsShieldOfChaosTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr auto shieldOfChaosKey = "new-horizons:shieldOfChaos";
constexpr auto holyWrathKey = "new-horizons:holyWrath";
constexpr auto chaosMagicSkill = "new-horizons:chaosMagic";
constexpr auto paradoxShieldPerk = "new-horizons:chaosMagic.paradoxShield";

SpellID spellId(const std::string & key)
{
	return SpellID(SpellID::decode(key));
}

SpellID shieldOfChaosSpell()
{
	return spellId(shieldOfChaosKey);
}

SpellID holyWrathSpell()
{
	return spellId(holyWrathKey);
}
}

class NewHorizonsShieldOfChaosTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * otherFriendly = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare(int spellPower = 50, int chaosRank = MasteryLevel::NONE)
	{
		startGame();
		ASSERT_NE(shieldOfChaosSpell(), SpellID::NONE);
		ASSERT_NE(holyWrathSpell(), SpellID::NONE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(shieldOfChaosSpell());
		attackerSideHero->addSpellToSpellbook(holyWrathSpell());
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		if(chaosRank != MasteryLevel::NONE)
		{
			const SecondarySkill chaos(SecondarySkill::decode(chaosMagicSkill));
			ASSERT_TRUE(chaos.hasValue());
			attackerSideHero->setSecSkillLevel(chaos, chaosRank, ChangeValueMode::ABSOLUTE);
		}

		startBattle();
		removeDeployedUnits();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
		otherFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(5, 5), 1000);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000);
		beginCombat();
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	BattleAction shieldAction(const CStack * target) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = shieldOfChaosSpell();
		action.aimToUnit(target);
		return action;
	}

	bool castShield(const CStack * target)
	{
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), PlayerColor(0), shieldAction(target));
	}

	bool castHolyWrath(const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = holyWrathSpell();
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool castDispel(const CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::DISPEL;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	TConstBonusListPtr shieldBonuses(const CStack * unit) const
	{
		return unit->getAllBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(shieldOfChaosSpell())));
	}

	std::shared_ptr<const Bonus> shieldBonus(const CStack * unit, BonusType type,
		BonusSubtypeID subtype = {}) const
	{
		const auto bonuses = unit->getAllBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(shieldOfChaosSpell()))
			.And(Selector::typeSubtype(type, subtype)));
		if(!bonuses || bonuses->empty())
			return {};
		return bonuses->front();
	}

	DamageRange physicalDamage(const CStack * attacker, const CStack * defender) const
	{
		return battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, false)).damage;
	}

	int64_t holyWrathDamage(const CStack * target) const
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, holyWrathSpell().toSpell());
		const auto mechanics = holyWrathSpell().toSpell()->battleMechanics(&cast);
		return mechanics->adjustEffectValue(target);
	}

	void selectParadoxShield()
	{
		const SecondarySkill chaos(SecondarySkill::decode(chaosMagicSkill));
		ASSERT_TRUE(chaos.hasValue());
		attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

		// Seed the saved Expert selection directly: this test isolates the perk's
		// combat effect from the independently tested tier-offer/progression path.
		auto & state = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
		std::erase_if(state.selected, [&](const auto & selection)
		{
			return selection.skillId == chaosMagicSkill;
		});
		state.selected.push_back({chaosMagicSkill, paradoxShieldPerk});
		state.validate();
		ASSERT_TRUE(attackerSideHero->hasActivePerk(chaosMagicSkill, paradoxShieldPerk));
	}
};

TEST_F(NewHorizonsShieldOfChaosTest, AuthoritativeCastAppliesFractionalPhysicalAndMagicalReductionToOneFriendly)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = shieldOfChaosSpell();
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spell));
	ASSERT_TRUE(castShield(friendly));

	const auto physical = shieldBonus(friendly, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	const auto magical = shieldBonus(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	const auto morale = shieldBonus(friendly, BonusType::MORALE);
	const auto luck = shieldBonus(friendly, BonusType::LUCK);
	ASSERT_NE(physical, nullptr);
	ASSERT_NE(magical, nullptr);
	ASSERT_NE(morale, nullptr);
	ASSERT_NE(luck, nullptr);
	EXPECT_EQ(physical->val, 5750)
		<< "50% fixed reduction plus 0.15% x 50 Spell Power preserves half-percentage precision";
	EXPECT_EQ(magical->val, physical->val);
	EXPECT_EQ(morale->val, -10);
	EXPECT_EQ(luck->val, -10);
	for(const auto * bonus : {physical.get(), magical.get(), morale.get(), luck.get()})
		EXPECT_EQ(bonus->turnsRemain, 2);
	const auto applied = shieldBonuses(friendly);
	const auto otherFriendlyBonuses = shieldBonuses(otherFriendly);
	const auto enemyBonuses = shieldBonuses(enemy);
	ASSERT_NE(applied, nullptr);
	ASSERT_NE(otherFriendlyBonuses, nullptr);
	ASSERT_NE(enemyBonuses, nullptr);
	EXPECT_EQ(applied->size(), 4u);
	EXPECT_TRUE(otherFriendlyBonuses->empty());
	EXPECT_TRUE(enemyBonuses->empty());
}

TEST_F(NewHorizonsShieldOfChaosTest, IndifferentTargetingAllowsAnEnemyStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castShield(enemy));

	const auto physical = shieldBonus(enemy, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	const auto magical = shieldBonus(enemy, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	const auto morale = shieldBonus(enemy, BonusType::MORALE);
	const auto luck = shieldBonus(enemy, BonusType::LUCK);
	ASSERT_NE(physical, nullptr);
	ASSERT_NE(magical, nullptr);
	ASSERT_NE(morale, nullptr);
	ASSERT_NE(luck, nullptr);
	EXPECT_EQ(physical->val, 5750);
	EXPECT_EQ(magical->val, 5750);
	EXPECT_EQ(morale->val, -10);
	EXPECT_EQ(luck->val, -10);
	const auto friendlyBonuses = shieldBonuses(friendly);
	ASSERT_NE(friendlyBonuses, nullptr);
	EXPECT_TRUE(friendlyBonuses->empty());
}

TEST_F(NewHorizonsShieldOfChaosTest, ChaosRankScalesOnlyTheSpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare(50, MasteryLevel::BASIC));
	ASSERT_TRUE(castShield(friendly));
	const int coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		attackerSideHero->getMagicRules(), attackerSideHero, shieldOfChaosSpell());
	const int expectedTerm = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		750, 1, coefficient, 0, 0);
	const int expected = 5000 + expectedTerm;
	const auto physical = shieldBonus(friendly, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	const auto magical = shieldBonus(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	ASSERT_NE(physical, nullptr);
	ASSERT_NE(magical, nullptr);
	EXPECT_EQ(physical->val, expected)
		<< "Chaos rank scales only the Spell Power-derived reduction term; the fixed 50% remains unchanged";
	EXPECT_EQ(magical->val, expected);
}

TEST_F(NewHorizonsShieldOfChaosTest, ExpertBaseFormulaCapsAtEightyPercent)
{
	ASSERT_NO_FATAL_FAILURE(prepare(200, MasteryLevel::EXPERT));
	ASSERT_TRUE(castShield(friendly));
	const auto physical = shieldBonus(friendly, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	const auto magical = shieldBonus(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	ASSERT_NE(physical, nullptr);
	ASSERT_NE(magical, nullptr);
	EXPECT_EQ(physical->val, 8000);
	EXPECT_EQ(magical->val, 8000);
}

TEST_F(NewHorizonsShieldOfChaosTest, PhysicalAndMagicalAttacksUseTheTimedReduction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto physicalBefore = physicalDamage(friendly, enemy).max;
	const auto magicalBefore = holyWrathDamage(enemy);
	ASSERT_GT(physicalBefore, 0);
	ASSERT_GT(magicalBefore, 0);
	ASSERT_TRUE(castShield(enemy));

	const auto physicalAfter = physicalDamage(friendly, enemy).max;
	const auto magicalAfter = holyWrathDamage(enemy);
	EXPECT_NEAR(static_cast<double>(physicalAfter), static_cast<double>(physicalBefore) * 4250 / 10000, 1.0);
	EXPECT_NEAR(static_cast<double>(magicalAfter), static_cast<double>(magicalBefore) * 4250 / 10000, 1.0);

	advanceRound();
	const auto activeReduction = shieldBonus(enemy, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	ASSERT_NE(activeReduction, nullptr);
	EXPECT_EQ(activeReduction->turnsRemain, 1);
	const auto healthBefore = enemy->getAvailableHealth();
	ASSERT_TRUE(castHolyWrath(enemy));
	EXPECT_EQ(healthBefore - enemy->getAvailableHealth(), magicalAfter)
		<< "The authoritative damaging spell cast uses the forecasted timed magical reduction";
}

TEST_F(NewHorizonsShieldOfChaosTest, ParadoxShieldAddsAfterTheSpellCapWhilePhysicalDamageKeepsItsGlobalCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare(200, MasteryLevel::EXPERT));
	selectParadoxShield();
	const auto physicalBefore = physicalDamage(friendly, enemy).max;
	const auto magicalBefore = holyWrathDamage(enemy);
	ASSERT_GT(physicalBefore, 0);
	ASSERT_GT(magicalBefore, 0);
	ASSERT_TRUE(castShield(enemy));

	const auto physical = shieldBonus(enemy, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	const auto magical = shieldBonus(enemy, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	const auto morale = shieldBonus(enemy, BonusType::MORALE);
	const auto luck = shieldBonus(enemy, BonusType::LUCK);
	ASSERT_NE(physical, nullptr);
	ASSERT_NE(magical, nullptr);
	ASSERT_NE(morale, nullptr);
	ASSERT_NE(luck, nullptr);
	EXPECT_EQ(physical->val, 9000)
		<< "Paradox Shield's +10 percentage points are applied after the base 80% spell cap";
	EXPECT_EQ(magical->val, 9000);
	EXPECT_EQ(morale->val, -10);
	EXPECT_EQ(luck->val, -10);
	EXPECT_NEAR(static_cast<double>(physicalDamage(friendly, enemy).max),
		static_cast<double>(physicalBefore) * 2000 / 10000, 1.0)
		<< "The saved global physical mitigation cap remains 80%";
	const auto magicalAfter = holyWrathDamage(enemy);
	EXPECT_NEAR(static_cast<double>(magicalAfter), static_cast<double>(magicalBefore) / 10, 1.0);

	advanceRound();
	const auto activeReduction = shieldBonus(enemy, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	ASSERT_NE(activeReduction, nullptr);
	EXPECT_EQ(activeReduction->turnsRemain, 1);
	const auto healthBefore = enemy->getAvailableHealth();
	ASSERT_TRUE(castHolyWrath(enemy));
	EXPECT_EQ(healthBefore - enemy->getAvailableHealth(), magicalAfter)
		<< "Paradox Shield's 90% magical reduction is present in the authoritative damage cast";
}

TEST_F(NewHorizonsShieldOfChaosTest, RecastRefreshesOneEffectAndTheBuffExpiresAfterTwoRounds)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castShield(friendly));
	const auto initialReduction = shieldBonus(friendly, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	ASSERT_NE(initialReduction, nullptr);
	EXPECT_EQ(initialReduction->turnsRemain, 2);
	advanceRound();
	const auto beforeRecast = shieldBonus(friendly, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	ASSERT_NE(beforeRecast, nullptr);
	EXPECT_EQ(beforeRecast->turnsRemain, 1);

	ASSERT_TRUE(castShield(friendly));
	const auto recastBonuses = shieldBonuses(friendly);
	ASSERT_NE(recastBonuses, nullptr);
	EXPECT_EQ(recastBonuses->size(), 4u)
		<< "Recasting refreshes the four bonuses from the same spell source instead of stacking another set";
	const auto refreshed = shieldBonus(friendly, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	ASSERT_NE(refreshed, nullptr);
	EXPECT_EQ(refreshed->turnsRemain, 2);
	advanceRound();
	const auto oneRoundLeft = shieldBonus(friendly, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	ASSERT_NE(oneRoundLeft, nullptr);
	EXPECT_EQ(oneRoundLeft->turnsRemain, 1);
	advanceRound();
	const auto expiredBonuses = shieldBonuses(friendly);
	ASSERT_NE(expiredBonuses, nullptr);
	EXPECT_TRUE(expiredBonuses->empty());
}

TEST_F(NewHorizonsShieldOfChaosTest, NonDamagingMagicRemainsLegalAndDispelRemovesTheBuff)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castShield(friendly));
	advanceRound();
	const auto activeReduction = shieldBonus(friendly, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	ASSERT_NE(activeReduction, nullptr);
	EXPECT_EQ(activeReduction->turnsRemain, 1);

	const auto * haste = SpellID(SpellID::HASTE).toSpell();
	spells::BattleCast hasteCast(battle(), attackerSideHero, spells::Mode::HERO, haste);
	EXPECT_TRUE(haste->battleMechanics(&hasteCast)->canBeCastAt(
		spells::Target{spells::Destination(friendly)}))
		<< "Shield of Chaos reduces damage without granting spell immunity";

	ASSERT_TRUE(castDispel(friendly));
	const auto dispelledBonuses = shieldBonuses(friendly);
	ASSERT_NE(dispelledBonuses, nullptr);
	EXPECT_TRUE(dispelledBonuses->empty())
		<< "Ordinary Dispel removes Shield of Chaos as a timed spell effect";
}

TEST(NewHorizonsShieldOfChaosSerializationTest, PhysicalReductionRoundTripsAndRejectsDownsave)
{
	const Bonus source(BonusDuration::N_TURNS, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSource::SPELL_EFFECT, 5750, BonusSourceID(), BonusSubtypeID());
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.oser & source;

	CMemorySerializer restored(current.extractBuffer());
	restored.iser.version = ESerializationVersion::CURRENT;
	Bonus loaded;
	restored.iser & loaded;
	EXPECT_EQ(loaded.type, BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS);
	EXPECT_EQ(loaded.val, 5750);
	EXPECT_EQ(loaded.subtype, BonusSubtypeID());

	CMemorySerializer oldSave;
	oldSave.oser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_PROBABILITY_MODIFIERS;
	EXPECT_THROW(oldSave.oser & source, std::runtime_error);
}
