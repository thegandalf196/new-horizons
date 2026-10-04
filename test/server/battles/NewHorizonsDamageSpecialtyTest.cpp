/*
 * NewHorizonsDamageSpecialtyTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameSettings.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsDirectDamage.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view DAMAGE_SPECIALTY_MARKER_PREFIX = "new-horizons:damage-spell-specialty:";

HeroTypeID heroType(const char * identifier)
{
	return HeroTypeID(HeroTypeID::decode(identifier));
}

CreatureID creature(const char * identifier)
{
	return CreatureID(CreatureID::decode(identifier));
}

SpellID spell(const char * identifier)
{
	return SpellID(SpellID::decode(identifier));
}

class NewHorizonsDamageSpecialtyTest : public HeroCommandFixture
{
protected:
	bool enableDamageSpecialtyRules = true;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		const auto & activeMods = LIBRARY->modh->getActiveMods();
		if(std::find(activeMods.begin(), activeMods.end(), GameConstants::NEW_HORIZONS_MOD_SCOPE) == activeMods.end())
			GTEST_SKIP() << "Requires the activated New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));

		JsonNode heroRules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(enableDamageSpecialtyRules)
		{
			heroRules["damageSpellSpecialties"]["version"].Integer() = 1;
			heroRules["damageSpellSpecialties"]["componentPercent"].Integer() = 15;
		}
		else
		{
			heroRules.Struct().erase("damageSpellSpecialties");
			// This is a deliberately old saved snapshot; do not inherit the active
			// preset's optional field while applying the map override.
			heroRules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(heroRules));
	}

	void startSpecialistMap(const char * specialistId, uint32_t experience = 0)
	{
		const auto pikeman = creature("core:pikeman");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, heroType(specialistId), PlayerColor(0)).heroExperience(experience)
			.heroGarrison({{pikeman, 100}})
			.hero({7, 7, 0}, heroType("core:solmyr"), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{pikeman, 100}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);

		attackerSideHero = findHeroAt({5, 5, 0});
		defenderSideHero = findHeroAt({7, 7, 0});
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
	}

	void prepareSpell(SpellID spell, int spellPower)
	{
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
	}

	void startBattleAndBeginCombat()
	{
		startBattle();
		beginCombat();
	}

	static size_t damageMarkerCount(const CGHeroInstance & hero)
	{
		const auto bonuses = hero.getExportedBonusList();
		return std::count_if(bonuses.begin(), bonuses.end(), [](const auto & bonus)
		{
			return bonus && bonus->stacking.starts_with(DAMAGE_SPECIALTY_MARKER_PREFIX);
		});
	}
};
}

TEST(NewHorizonsDamageSpecialtyFormulaTest, BoostsOnlyTheSpellPowerComponentAndRoundsOnce)
{
	const newHorizonsMagic::DirectDamageFormula formula{20, 7};
	EXPECT_EQ(formula.evaluateBasisPoints(0, 4, 10000, 0, 15), 20)
		<< "At zero Spell Power, the fixed base is unchanged";
	EXPECT_EQ(formula.evaluateBasisPoints(1, 4, 10000, 0, 0), 21);
	EXPECT_EQ(formula.evaluateBasisPoints(1, 4, 10000, 0, 15), 22)
		<< "7/4 Spell Power damage becomes 2.0125 before its one final floor";
	EXPECT_THROW(formula.evaluateBasisPoints(1, 4, 10000, 0, 14), std::runtime_error);
}

TEST_F(NewHorizonsDamageSpecialtyTest, CieleMagicArrowForecastAndAcceptedCastUseOnlyThePowerComponent)
{
	startSpecialistMap("core:ciele");
	ASSERT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:ciele");
	ASSERT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(SpellID::MAGIC_ARROW), 15);
	EXPECT_EQ(defenderSideHero->getDamageSpellSpecialtyBonusPercent(SpellID::MAGIC_ARROW), 0)
		<< "Solmyr is not converted by Ciele's spell-specific producer";

	const auto prototypeBonus = std::find_if(attackerSideHero->getHeroType()->specialty.begin(),
		attackerSideHero->getHeroType()->specialty.end(), [](const auto & bonus)
		{
			return bonus && bonus->type == BonusType::SPECIFIC_SPELL_DAMAGE
				&& bonus->subtype == BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW));
		});
	ASSERT_NE(prototypeBonus, attackerSideHero->getHeroType()->specialty.end());
	ASSERT_EQ((*prototypeBonus)->val, 50);

	prepareSpell(SpellID::MAGIC_ARROW, 10);
	// An unrelated authored bonus must continue to scale the complete damage
	// result after Ciele's old whole-result bonus is suppressed.
	auto unrelatedBonus = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPECIFIC_SPELL_DAMAGE, BonusSource::OTHER, 20, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW)));
	attackerSideHero->addNewBonus(unrelatedBonus);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::SPECIFIC_SPELL_DAMAGE,
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))), 20);
	EXPECT_EQ(damageMarkerCount(*attackerSideHero), 1u);

	// The saved profile and exact hero-local producer marker must both survive a
	// normal game-state save/load; an installed ruleset is not consulted on load.
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.iser.version = ESerializationVersion::CURRENT;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	auto * loadedCiele = restored.getHero(attackerSideHero->id);
	ASSERT_NE(loadedCiele, nullptr);
	const auto savedSpecialtyRules = newHorizonsHeroes::damageSpellSpecialtyRules(
		loadedCiele->getPrimaryGrowthRules());
	ASSERT_TRUE(savedSpecialtyRules.has_value());
	EXPECT_EQ(savedSpecialtyRules->version, 1);
	EXPECT_EQ(savedSpecialtyRules->componentPercent, 15);
	EXPECT_EQ(loadedCiele->getDamageSpellSpecialtyBonusPercent(SpellID::MAGIC_ARROW), 15);
	EXPECT_EQ(damageMarkerCount(*loadedCiele), 1u);
	EXPECT_EQ(loadedCiele->valOfBonuses(BonusType::SPECIFIC_SPELL_DAMAGE,
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))), 20);
	EXPECT_EQ((*prototypeBonus)->val, 50) << "Conversion must clone, not mutate the shared hero prototype";

	startBattleAndBeginCombat();
	auto * target = addStack(BattleSide::DEFENDER, creature("core:pikeman"), BattleHex(rightHex + 1), 1000);
	ASSERT_NE(target, nullptr);
	const auto spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	const auto formula = newHorizonsMagic::spellDirectDamage(battle()->getMagicRules(), spell->getJsonKey());
	ASSERT_TRUE(formula.has_value());
	const int coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell->getId());
	const int empower = newHorizonsMagic::empowerSpellBonusPercent(
		battle()->getMagicRules(), attackerSideHero, spell->getId());
	const auto powerComponentDamage = formula->evaluateBasisPoints(attackerSideHero->getEffectPower(spell),
		attackerSideHero->getEffectPowerDivisor(spell), coefficient, empower, 15);
	const auto expectedDamage = powerComponentDamage * 120 / 100;
	EXPECT_EQ(spell->calculateDamage(attackerSideHero), expectedDamage);

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&cast)->getEffectValue(), powerComponentDamage);
	const auto healthBefore = target->getAvailableHealth();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), expectedDamage)
		<< "Accepted damage must match the cast forecast with the unrelated bonus retained";
}

TEST_F(NewHorizonsDamageSpecialtyTest, DeemerMeteorShowerIsIndependentOfHeroLevelAndTargetTier)
{
	startSpecialistMap("core:deemer");
	ASSERT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:deemer");
	EXPECT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(SpellID::METEOR_SHOWER), 15);
	prepareSpell(SpellID::METEOR_SHOWER, 10);

	const auto spell = SpellID(SpellID::METEOR_SHOWER).toSpell();
	ASSERT_NE(spell, nullptr);
	attackerSideHero->level = 1;
	const auto lowLevelEstimate = spell->calculateDamage(attackerSideHero);
	attackerSideHero->level = 14;
	const auto highLevelEstimate = spell->calculateDamage(attackerSideHero);
	EXPECT_EQ(highLevelEstimate, lowLevelEstimate)
		<< "Deemer's historical per-hero-level specialty is suppressed for the converted producer";

	startBattle();
	const auto lowTier = addStack(BattleSide::DEFENDER, creature("core:pikeman"), BattleHex(12, 4), 1000);
	const auto highTier = addStack(BattleSide::DEFENDER, creature("core:archangel"), BattleHex(13, 5), 100);
	ASSERT_NE(lowTier, nullptr);
	ASSERT_NE(highTier, nullptr);
	// Keep the attacker as this focused cast's controller. The target's tier
	// must vary without its higher native Initiative changing who can act.
	highTier->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_INITIATIVE_BASE, BonusSource::OTHER, 1, BonusSourceID()));
	beginCombat();
	ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));

	const auto formula = newHorizonsMagic::spellDirectDamage(battle()->getMagicRules(), spell->getJsonKey());
	ASSERT_TRUE(formula.has_value());
	const int coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell->getId());
	const int empower = newHorizonsMagic::empowerSpellBonusPercent(
		battle()->getMagicRules(), attackerSideHero, spell->getId());
	const auto expectedDamage = formula->evaluateBasisPoints(attackerSideHero->getEffectPower(spell),
		attackerSideHero->getEffectPowerDivisor(spell), coefficient, empower, 15);
	EXPECT_EQ(spell->calculateDamage(attackerSideHero), expectedDamage);

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&cast)->getEffectValue(), expectedDamage);
	const auto lowTierBefore = lowTier->getAvailableHealth();
	const auto highTierBefore = highTier->getAvailableHealth();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToHex(BattleHex(12, 5));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(lowTierBefore - lowTier->getAvailableHealth(), expectedDamage);
	EXPECT_EQ(highTierBefore - highTier->getAvailableHealth(), expectedDamage)
		<< "The old target-tier/hero-level formula must not change the converted fixed SP-component bonus";
}

TEST_F(NewHorizonsDamageSpecialtyTest, LunaFireWallBoostIsLatchedOnceAndNotRepeatedByItsTrigger)
{
	startSpecialistMap("core:luna");
	ASSERT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:luna");
	const auto triggerSpell = spell("core:fireWallTrigger");
	EXPECT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(SpellID::FIRE_WALL), 15);
	EXPECT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(triggerSpell), 0)
		<< "The trigger is not an independent spell-power bonus producer";

	prepareSpell(SpellID::FIRE_WALL, 43);
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	startBattleAndBeginCombat();

	const auto spell = SpellID(SpellID::FIRE_WALL).toSpell();
	ASSERT_NE(spell, nullptr);
	const auto expectedSnapshot = spell->calculateDamage(attackerSideHero);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = spell->getId();
	action.aimToHex(BattleHex(70));
	action.spellFireWallDirection = BattleHex::RIGHT;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	ASSERT_EQ(battle()->obstacles.size(), 1u);
	const auto * wall = dynamic_cast<const SpellCreatedObstacle *>(battle()->obstacles.front().get());
	ASSERT_NE(wall, nullptr);
	ASSERT_TRUE(wall->damageSnapshot);
	EXPECT_EQ(wall->minimalDamage, expectedSnapshot);

	auto * foe = addStack(BattleSide::DEFENDER, creature("core:pikeman"), BattleHex(70), 100);
	ASSERT_NE(foe, nullptr);
	const auto healthBefore = foe->getAvailableHealth();
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *foe));
	EXPECT_EQ(healthBefore - foe->getAvailableHealth(), expectedSnapshot)
		<< "The stored cast-time snapshot is applied once; the later trigger must not scale it again";
}

TEST_F(NewHorizonsDamageSpecialtyTest, MissingSavedRulesKeepLegacyCieleAndLunaProducers)
{
	enableDamageSpecialtyRules = false;
	startSpecialistMap("core:ciele");
	EXPECT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(SpellID::MAGIC_ARROW), 0);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::SPECIFIC_SPELL_DAMAGE,
		BonusSubtypeID(SpellID(SpellID::MAGIC_ARROW))), 50)
		<< "An old saved hero profile retains Ciele's original whole-result specialty";
}

TEST_F(NewHorizonsDamageSpecialtyTest, MissingSavedRulesKeepLunasLegacyFireWallTriggerAndTooltipEntries)
{
	enableDamageSpecialtyRules = false;
	startSpecialistMap("core:luna");
	const auto triggerSpell = spell("core:fireWallTrigger");
	EXPECT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(SpellID::FIRE_WALL), 0);
	EXPECT_EQ(attackerSideHero->getDamageSpellSpecialtyBonusPercent(triggerSpell), 0);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::SPECIFIC_SPELL_DAMAGE,
		BonusSubtypeID(triggerSpell)), 100);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::SPECIFIC_SPELL_DAMAGE,
		BonusSubtypeID(SpellID(SpellID::FIRE_WALL))), 100)
		<< "A missing saved option leaves both original legacy Fire Wall bonus entries intact";
}
