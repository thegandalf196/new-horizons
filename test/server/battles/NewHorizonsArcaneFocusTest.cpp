/*
 * NewHorizonsArcaneFocusTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
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

namespace
{
class NewHorizonsArcaneFocusTest : public HeroCommandFixture
{
protected:
	CStack * friendlyTarget = nullptr;
	CStack * enemyTarget = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(const int spellPower = 54)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID::SORROW);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);

		const auto spellcraft = SecondarySkill::decode(std::string(newHorizonsMagic::SPELLCRAFT_SKILL));
		ASSERT_GE(spellcraft, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(spellcraft), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsMagic::SPELLCRAFT_ARCANE_FOCUS)});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsMagic::SPELLCRAFT_ARCANE_FOCUS)));
		setTestSpellPointTotal(attackerSideHero, 100);

		startBattle();
		friendlyTarget = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
		enemyTarget = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 100);
		ASSERT_NE(friendlyTarget, nullptr);
		ASSERT_NE(enemyTarget, nullptr);
		beginCombat();
	}

	bool castSorrow(CStack * target)
	{
		return castOn(attackerSideHero, SpellID::SORROW, target);
	}

	const Bonus * sorrowMorale(const CStack * unit) const
	{
		const auto bonuses = unit->getAllBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SORROW))));
		for(const auto & bonus : *bonuses)
			if(bonus->type == BonusType::MORALE)
				return bonus.get();
		return nullptr;
	}
};
}

TEST_F(NewHorizonsArcaneFocusTest, FirstSorrowScalesOnlyItsPowerTermAndHistorySurvivesTheRound)
{
	prepare();
	const auto * spell = SpellID(SpellID::SORROW).toSpell();
	ASSERT_NE(spell, nullptr);

	spells::BattleCast firstCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto firstMechanics = spell->battleMechanics(&firstCast);
	ASSERT_NE(firstMechanics, nullptr);
	EXPECT_EQ(firstMechanics->getArcaneFocusBonusPercent(),
		newHorizonsMagic::SPELLCRAFT_ARCANE_FOCUS_BONUS_PERCENT);
	EXPECT_EQ(firstMechanics->getSpellPowerCoefficientBasisPoints(), 13'200)
		<< "Basic Spellcraft's 110% factor is multiplied by Arcane Focus's 120% factor";

	const auto & rules = battle()->getBattle()->getMagicRules();
	EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 54), 1);
	EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 54,
		0, 0, newHorizonsMagic::SPELLCRAFT_ARCANE_FOCUS_BONUS_PERCENT), 2)
		<< "The fixed base Morale penalty remains one; only the Spell Power term crosses its threshold";

	const auto magicArrow = newHorizonsMagic::spellDirectDamage(rules, "core:magicArrow");
	ASSERT_TRUE(magicArrow.has_value());
	EXPECT_EQ(magicArrow->evaluateBasisPoints(0, 1, firstMechanics->getSpellPowerCoefficientBasisPoints()),
		magicArrow->base)
		<< "At zero Spell Power the fixed damage base is unchanged";

	ASSERT_TRUE(castSorrow(enemyTarget));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	ASSERT_NE(sorrowMorale(enemyTarget), nullptr);
	EXPECT_EQ(sorrowMorale(enemyTarget)->val, -2)
		<< "The effect uses the coefficient captured before BattleSpellCast records completion";
	EXPECT_EQ(sorrowMorale(enemyTarget)->turnsRemain, newHorizonsMagic::SORROW_BASE_DURATION_ROUNDS);
	EXPECT_EQ(firstMechanics->getArcaneFocusBonusPercent(), 20)
		<< "The Mechanics instance retains its cast snapshot after the live battle marker changes";

	endRound();
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	spells::BattleCast laterCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto laterMechanics = spell->battleMechanics(&laterCast);
	ASSERT_NE(laterMechanics, nullptr);
	EXPECT_EQ(laterMechanics->getArcaneFocusBonusPercent(), 0);
	EXPECT_EQ(laterMechanics->getSpellPowerCoefficientBasisPoints(), 11'000)
		<< "The first-spell bonus is combat-long, not round-limited";
}

TEST_F(NewHorizonsArcaneFocusTest, RejectedHeroAndCreatureCastsDoNotConsumeTheFirstSpellBonus)
{
	prepare();
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	EXPECT_FALSE(castSorrow(friendlyTarget));
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));

	auto * creatureCaster = addStack(BattleSide::ATTACKER,
		creatureByName("core:imp"), BattleHex(5, 5), 1);
	ASSERT_NE(creatureCaster, nullptr);
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELLCASTER, BonusSource::OTHER, 3, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	creatureCaster->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CREATURE_SPELL_POWER, BonusSource::OTHER, 20, BonusSourceID()));

	const auto * creatureSpell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast creatureCast(battle(), creatureCaster, spells::Mode::CREATURE_ACTIVE, creatureSpell);
	const auto creatureMechanics = creatureSpell->battleMechanics(&creatureCast);
	ASSERT_NE(creatureMechanics, nullptr);
	EXPECT_EQ(creatureMechanics->getArcaneFocusBonusPercent(), 0);
	EXPECT_EQ(creatureMechanics->getSpellPowerCoefficientBasisPoints(), 10'000);
	spells::Target target{spells::Destination(enemyTarget)};
	ASSERT_TRUE(creatureMechanics->canBeCastAt(target));
	creatureCast.cast(gameHandler->spellEnv.get(), target);
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));

	spells::BattleCast firstHeroCast(battle(), attackerSideHero, spells::Mode::HERO,
		SpellID(SpellID::SORROW).toSpell());
	const auto heroMechanics = SpellID(SpellID::SORROW).toSpell()->battleMechanics(&firstHeroCast);
	ASSERT_NE(heroMechanics, nullptr);
	EXPECT_EQ(heroMechanics->getArcaneFocusBonusPercent(), 20);
	EXPECT_EQ(heroMechanics->getSpellPowerCoefficientBasisPoints(), 13'200);
	ASSERT_TRUE(castSorrow(enemyTarget));
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
	ASSERT_NE(sorrowMorale(enemyTarget), nullptr);
	EXPECT_EQ(sorrowMorale(enemyTarget)->val, -2);
}

TEST_F(NewHorizonsArcaneFocusTest, LandMineCountScalesOnlyTheSpellPowerTerm)
{
	prepare(84);
	const auto * landMine = SpellID(SpellID::LAND_MINE).toSpell();
	ASSERT_NE(landMine, nullptr);

	spells::BattleCast firstCast(battle(), attackerSideHero, spells::Mode::HERO, landMine);
	const auto mechanics = landMine->battleMechanics(&firstCast);
	ASSERT_NE(mechanics, nullptr);
	EXPECT_EQ(mechanics->getEffectPower(), 84);
	EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 13'200);
	EXPECT_EQ(newHorizonsMagic::landMineHexCount(84, 11'000), 2)
		<< "Basic Spellcraft without the first-cast bonus leaves effective Spell Power below 100";
	EXPECT_EQ(newHorizonsMagic::landMineHexCount(84), 2)
		<< "Pre-v3 New Horizons snapshots preserve raw Spell Power thresholds";
	EXPECT_EQ(newHorizonsMagic::landMineHexCount(100), 3)
		<< "The legacy 100 Spell Power threshold still adds exactly one selected hex";
	EXPECT_EQ(mechanics->getNewHorizonsLandMinePatchCount(), 3)
		<< "Arcane Focus scales the Spell Power threshold while preserving the fixed base of two and cap of four";
}
