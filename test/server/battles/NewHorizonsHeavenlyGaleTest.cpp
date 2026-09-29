/*
 * NewHorizonsHeavenlyGaleTest.cpp, part of VCMI engine
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
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr auto heavenlyGaleKey = "new-horizons:heavenlyGale";
constexpr auto holyArmorKey = "new-horizons:holyArmor";
constexpr auto lightMagicSkill = "new-horizons:lightMagic";
constexpr auto aegisPerk = "new-horizons:lightMagic.aegis";
constexpr auto healerPerk = "new-horizons:lightMagic.healer";
constexpr auto spellcraftSkill = "new-horizons:spellcraft";
constexpr auto spellPenetrationPerk = "new-horizons:spellcraft.spellPenetration";
constexpr auto empowerSpellPerk = "new-horizons:spellcraft.empowerSpell";
constexpr auto warcastingSkill = "new-horizons:warcasting";

SpellID spellId(const std::string & key)
{
	return SpellID(SpellID::decode(key));
}

SpellID heavenlyGaleSpell()
{
	return spellId(heavenlyGaleKey);
}

SpellID holyArmorSpell()
{
	return spellId(holyArmorKey);
}
}

class NewHorizonsHeavenlyGaleTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * secondFriendly = nullptr;
	CStack * enemyShooter = nullptr;
	CStack * enemyMelee = nullptr;
	CStack * enemySpellLike = nullptr;
	CStack * ballista = nullptr;
	CStack * tower = nullptr;

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

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void prepare(int spellPower = 50)
	{
		startGame();
		ASSERT_NE(heavenlyGaleSpell(), SpellID::NONE);
		ASSERT_NE(holyArmorSpell(), SpellID::NONE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(heavenlyGaleSpell());
		attackerSideHero->addSpellToSpellbook(holyArmorSpell());
		attackerSideHero->addSpellToSpellbook(SpellID::FIREBALL);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		defenderSideHero->addSpellToSpellbook(SpellID::FIREBALL);
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(defenderSideHero, 1000);

		startBattle();
		removeDeployedUnits();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:phoenix"), BattleHex(3, 5), 1000);
		secondFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(5, 5), 1000);
		enemyShooter = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(12, 5), 1000);
		enemyMelee = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(13, 3), 1000);
		enemySpellLike = addStack(BattleSide::DEFENDER, creatureByName("core:magog"), BattleHex(12, 7), 1000);
		ballista = addStack(BattleSide::DEFENDER, CreatureID::BALLISTA, BattleHex(11, 3), 1);
		tower = addStack(BattleSide::DEFENDER, CreatureID::ARROW_TOWERS, BattleHex(14, 6), 1);
		beginCombat();
	}

	bool cast(SpellID spell, CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		if(spell == heavenlyGaleSpell())
			action.aimToHex(BattleHex::INVALID);
		else
			action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	TConstBonusListPtr galeBonuses(const CStack * unit) const
	{
		return unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(heavenlyGaleSpell())).And(Selector::type()(BonusType::HEAVENLY_GALE)));
	}

	DamageRange physicalDamage(const CStack * attacker, const CStack * defender, bool shooting) const
	{
		return battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, shooting)).damage;
	}

	void selectAegis()
	{
		const auto skill = SecondarySkill(SecondarySkill::decode(lightMagicSkill));
		ASSERT_TRUE(skill.hasValue());
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, healerPerk});
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, aegisPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(lightMagicSkill, aegisPerk));
	}
};

TEST_F(NewHorizonsHeavenlyGaleTest, AuthoritativeCastCoversTheFriendlyArmyAtFiftySevenPointFivePercentForTwoRounds)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = heavenlyGaleSpell();
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spell));
	EXPECT_EQ(newHorizonsMagic::spellCost(attackerSideHero->getMagicRules(), spell, MasteryLevel::NONE), 13);
	const auto manaBefore = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(cast(spell, friendly));
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 13);
	for(const auto * unit : {friendly, secondFriendly})
	{
		const auto applied = galeBonuses(unit);
		ASSERT_NE(applied, nullptr);
		ASSERT_EQ(applied->size(), 1u);
		EXPECT_EQ(applied->front()->val, 5750)
			<< "50% fixed protection plus 0.15% x 50 Spell Power must preserve half-percentage-point precision";
		EXPECT_EQ(applied->front()->turnsRemain, 2);
	}
	EXPECT_TRUE(galeBonuses(enemyShooter)->empty());

	while(battle()->getRound() == 0)
		advanceRound();
	ASSERT_EQ(galeBonuses(friendly)->front()->turnsRemain, 2);
	advanceRound();
	ASSERT_EQ(galeBonuses(friendly)->front()->turnsRemain, 1);
	advanceRound();
	EXPECT_TRUE(galeBonuses(friendly)->empty());
	EXPECT_TRUE(galeBonuses(secondFriendly)->empty());
}

TEST_F(NewHorizonsHeavenlyGaleTest, LightRankScalesOnlyTheSpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto skill = SecondarySkill(SecondarySkill::decode(lightMagicSkill));
	ASSERT_TRUE(skill.hasValue());
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(cast(heavenlyGaleSpell(), friendly));
	ASSERT_EQ(galeBonuses(friendly)->size(), 1u);
	EXPECT_EQ(galeBonuses(friendly)->front()->val, 5862)
		<< "Basic Light scales the 750-basis-point Spell Power term by 115%, while the 5000-basis-point base stays fixed";
}

TEST_F(NewHorizonsHeavenlyGaleTest, SchoolAndSpellcraftScaleOnlyTheSpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto light = SecondarySkill(SecondarySkill::decode(lightMagicSkill));
	const auto spellcraft = SecondarySkill(SecondarySkill::decode(spellcraftSkill));
	ASSERT_TRUE(light.hasValue());
	ASSERT_TRUE(spellcraft.hasValue());
	attackerSideHero->setSecSkillLevel(light, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		attackerSideHero->getMagicRules(), attackerSideHero, heavenlyGaleSpell());
	EXPECT_EQ(coefficient, 12650);
	ASSERT_TRUE(cast(heavenlyGaleSpell(), friendly));
	ASSERT_EQ(galeBonuses(friendly)->size(), 1u);
	EXPECT_EQ(galeBonuses(friendly)->front()->val, 5948)
		<< "School and Spellcraft scale the 750-basis-point Spell Power term together; the fixed base remains 5000";
}

TEST_F(NewHorizonsHeavenlyGaleTest, AegisScalesOnlyTheHeavenlyGaleSpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	selectAegis();
	ASSERT_TRUE(cast(heavenlyGaleSpell(), friendly));
	ASSERT_EQ(galeBonuses(friendly)->size(), 1u);
	EXPECT_EQ(galeBonuses(friendly)->front()->val, 6170)
		<< "Advanced Light and Aegis scale only the 750-basis-point Spell Power term, not the fixed 5000-basis-point base";
}

TEST_F(NewHorizonsHeavenlyGaleTest, AegisCombinesWithTheSchoolCoefficientBeforeFinalFractionalRounding)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	selectAegis();
	ASSERT_TRUE(cast(heavenlyGaleSpell(), friendly));
	ASSERT_EQ(galeBonuses(friendly)->size(), 1u);
	EXPECT_EQ(galeBonuses(friendly)->front()->val, 5070)
		<< "45 basis points x 130% Advanced Light x 120% Aegis floors to 70; separate intermediate floors would produce 69";
}

TEST_F(NewHorizonsHeavenlyGaleTest, WarcastingScalesOnlyTheSpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto skill = SecondarySkill(SecondarySkill::decode(warcastingSkill));
	ASSERT_TRUE(skill.hasValue());
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();

	spells::BattleCast castEvent(battle(), attackerSideHero, spells::Mode::HERO, heavenlyGaleSpell().toSpell());
	const auto mechanics = heavenlyGaleSpell().toSpell()->battleMechanics(&castEvent);
	ASSERT_EQ(mechanics->getWarcastingBonusPercent(), 10);
	ASSERT_TRUE(cast(heavenlyGaleSpell(), friendly));
	ASSERT_EQ(galeBonuses(friendly)->size(), 1u);
	EXPECT_EQ(galeBonuses(friendly)->front()->val, 5825)
		<< "Warcasting scales the 750-basis-point Spell Power term while the 5000-basis-point base stays fixed";
}

TEST_F(NewHorizonsHeavenlyGaleTest, EmpowerScalesOnlyTheSpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto skill = SecondarySkill(SecondarySkill::decode(spellcraftSkill));
	ASSERT_TRUE(skill.hasValue());
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({spellcraftSkill, spellPenetrationPerk});
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({spellcraftSkill, empowerSpellPerk});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(spellcraftSkill, empowerSpellPerk));
	const auto spell = heavenlyGaleSpell();
	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		attackerSideHero->getMagicRules(), attackerSideHero, spell);
	const auto empower = newHorizonsMagic::empowerSpellBonusPercent(
		attackerSideHero->getMagicRules(), attackerSideHero, spell);
	ASSERT_EQ(empower, 25);
	const auto expectedTerm = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		750, 1, coefficient, 0, empower);
	spells::BattleCast castEvent(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	ASSERT_EQ(spell.toSpell()->battleMechanics(&castEvent)->getEmpowerSpellBonusPercent(), empower);

	ASSERT_TRUE(cast(spell, friendly));
	ASSERT_EQ(galeBonuses(friendly)->size(), 1u);
	EXPECT_EQ(galeBonuses(friendly)->front()->val, 5000 + expectedTerm)
		<< "Empower scales only the Spell Power term; the fixed base remains 5000";
}

TEST_F(NewHorizonsHeavenlyGaleTest, PhysicalShotsAndSiegeProjectilesAreReducedButMeleeSpellLikeAndAreaDamageAreNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto rangedBefore = physicalDamage(enemyShooter, friendly, true).max;
	const auto ballistaBefore = physicalDamage(ballista, friendly, true).max;
	const auto towerBefore = physicalDamage(tower, friendly, true).max;
	const auto meleeBefore = physicalDamage(enemyMelee, friendly, false).max;
	const BattleAttackInfo spellLikeAttack(enemySpellLike, friendly, 0, true);
	ASSERT_FALSE(spellLikeAttack.physicalDamage);
	const auto spellLikeBefore = battle()->calculateDmgRange(spellLikeAttack).damage.max;
	const SpellID fireballSpell(SpellID::FIREBALL);
	spells::BattleCast fireballCast(battle(), defenderSideHero, spells::Mode::HERO, fireballSpell.toSpell());
	const auto fireball = fireballSpell.toSpell()->battleMechanics(&fireballCast);
	const auto fireballBefore = fireball->adjustEffectValue(friendly);

	ASSERT_TRUE(cast(heavenlyGaleSpell(), friendly));

	const auto galeRanged = physicalDamage(enemyShooter, friendly, true).max;
	const auto galeBallista = physicalDamage(ballista, friendly, true).max;
	const auto galeTower = physicalDamage(tower, friendly, true).max;
	const auto galeMelee = physicalDamage(enemyMelee, friendly, false).max;
	const auto galeSpellLike = battle()->calculateDmgRange(spellLikeAttack).damage.max;
	const auto galeFireball = fireball->adjustEffectValue(friendly);
	for(const auto [before, after] : {
		std::pair{rangedBefore, galeRanged},
		std::pair{ballistaBefore, galeBallista},
		std::pair{towerBefore, galeTower}})
	{
		ASSERT_GT(before, 0);
		EXPECT_NEAR(static_cast<double>(after), static_cast<double>(before) * 4250 / 10000, 1.0);
	}
	EXPECT_EQ(galeMelee, meleeBefore);
	EXPECT_EQ(galeSpellLike, spellLikeBefore);
	EXPECT_EQ(galeFireball, fireballBefore)
		<< "The timed projectile marker is not a magical-damage reduction and does not protect against an area spell";
}

TEST_F(NewHorizonsHeavenlyGaleTest, PhysicalDamageReductionsRemainBoundedByTheEightyPercentCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare(200));
	const auto unprotected = physicalDamage(enemyShooter, friendly, true).max;
	ASSERT_GT(unprotected, 0);
	ASSERT_TRUE(cast(heavenlyGaleSpell(), friendly));
	ASSERT_EQ(galeBonuses(friendly)->size(), 1u);
	EXPECT_EQ(galeBonuses(friendly)->front()->val, 8000);

	const auto galeOnly = physicalDamage(enemyShooter, friendly, true).max;
	auto otherReduction = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::GENERAL_DAMAGE_REDUCTION, BonusSource::OTHER, 80, BonusSourceID(),
		BonusSubtypeID(BonusCustomSubtype::damageTypeRanged));
	friendly->addNewBonus(otherReduction);
	const auto combinedReduction = physicalDamage(enemyShooter, friendly, true).max;
	EXPECT_NEAR(static_cast<double>(galeOnly), static_cast<double>(unprotected) * 2000 / 10000, 1.0);
	EXPECT_NEAR(static_cast<double>(combinedReduction), static_cast<double>(unprotected) * 2000 / 10000, 1.0)
		<< "The independent 80% cap still governs Gale combined with another ranged reduction";
}

TEST_F(NewHorizonsHeavenlyGaleTest, AegisScalesHolyArmorBeforeItsFractionalPowerTermIsRounded)
{
	ASSERT_NO_FATAL_FAILURE(prepare(13));
	selectAegis();
	ASSERT_TRUE(cast(holyArmorSpell(), friendly));
	const auto armor = friendly->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(holyArmorSpell())).And(Selector::type()(BonusType::SPELL_DAMAGE_REDUCTION)));
	ASSERT_NE(armor, nullptr);
	ASSERT_EQ(armor->size(), 1u);
	EXPECT_EQ(armor->front()->val, 34)
		<< "Advanced Light and Aegis scale Holy Armor's 13/5 Spell Power term together before flooring; its fixed 30% base is unchanged";
}
