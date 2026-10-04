/*
 * NewHorizonsHealingSpecialtyTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../SpellPointTestUtils.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view NON_DAMAGE_SPECIALTY_SPELL = "core:cure";
constexpr std::string_view CURE_SKILL_ID = "new-horizons:lightMagic";
constexpr std::string_view HEALER_PERK_ID = "new-horizons:lightMagic.healer";
constexpr int TEST_SPELL_POWER = 43;

HeroTypeID heroType(std::string_view identifier)
{
	return HeroTypeID(HeroTypeID::decode(std::string(identifier)));
}

int64_t expectedCureHealing(int spellPower, int schoolCoefficientPercent, int specialtyPercent)
{
	// Cure is 25 + 1.5 x Spell Power. School and specialty factors share one
	// rational rounding boundary; the fixed 25 is outside that component.
	return 25 + static_cast<int64_t>(3) * spellPower * schoolCoefficientPercent
		* (100 + specialtyPercent) / (2 * 100 * 100);
}

class NewHorizonsHealingSpecialtyTest : public HeroCommandFixture
{
protected:
	bool enableNonDamageSpecialtyRules = true;
	CGHeroInstance * ordinaryHero = nullptr;
	CStack * target = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the activated New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));

		JsonNode heroRules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(enableNonDamageSpecialtyRules)
		{
			auto & specialtyRules = heroRules["nonDamageSpellSpecialties"];
			specialtyRules["version"].Integer() = 1;
			specialtyRules["componentPercent"].Integer() = 20;
			auto & spells = specialtyRules["spells"].Vector();
			spells.clear();
			spells.emplace_back(std::string(NON_DAMAGE_SPECIALTY_SPELL));
		}
		else
		{
			heroRules.Struct().erase("nonDamageSpellSpecialties");
			// Preserve the resolved old snapshot instead of merging in the newly
			// installed optional conversion rule.
			heroRules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(heroRules));
	}

	void startSpecialistMap()
	{
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		ASSERT_TRUE(pikeman.hasValue());
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.name("NewHorizonsHealingSpecialty")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, heroType("core:uland"), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{pikeman, 100}})
			.hero({7, 7, 0}, heroType("core:solmyr"), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{pikeman, 100}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroAt({5, 5, 0});
		ordinaryHero = findHeroAt({7, 7, 0});
		defenderSideHero = ordinaryHero;
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(ordinaryHero, nullptr);
	}

	void prepareBattle(int spellPower = TEST_SPELL_POWER)
	{
		for(auto * hero : {attackerSideHero, ordinaryHero})
		{
			if(!hero->getArt(ArtifactPosition::SPELLBOOK))
				giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			hero->addSpellToSpellbook(SpellID::CURE);
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
			setTestSpellPointTotal(hero, 100);
		}

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		target = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:archangel")),
			BattleHex(3, 5), 100);
		const auto enemy = addStack(BattleSide::DEFENDER,
			CreatureID(CreatureID::decode("core:peasant")), BattleHex(12, 5), 1);
		beginCombat();
		ASSERT_NE(target, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}

	void injure(int64_t damage)
	{
		auto state = target->acquireState();
		state->damage(damage);
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state->save();
		change.changedStacks.back().healthDelta = -damage;
		gameHandler->sendAndApply(change);
	}
};
}

TEST_F(NewHorizonsHealingSpecialtyTest, UlandScalesOnlyCuresPowerComponentAcrossSchoolRanks)
{
	startSpecialistMap();
	ASSERT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:uland");
	ASSERT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::CURE), 20);
	EXPECT_EQ(ordinaryHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::CURE), 0);
	const auto savedSpecialtyRules = newHorizonsHeroes::nonDamageSpellSpecialtyRules(
		attackerSideHero->getPrimaryGrowthRules());
	ASSERT_TRUE(savedSpecialtyRules.has_value());
	EXPECT_EQ(savedSpecialtyRules->version, 1);
	EXPECT_EQ(savedSpecialtyRules->componentPercent, 20);
	ASSERT_EQ(savedSpecialtyRules->spells.size(), 1u);
	EXPECT_EQ(savedSpecialtyRules->spells.front(), SpellID::CURE);

	JsonNode malformedRules(JsonPath::builtin("config/newHorizonsHeroes"));
	malformedRules["nonDamageSpellSpecialties"]["componentPercent"].Integer() = 19;
	EXPECT_THROW(newHorizonsHeroes::validateHeroRules(malformedRules, true), std::runtime_error);
	malformedRules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
	malformedRules["nonDamageSpellSpecialties"]["spells"].Vector()[0].String() = "core:magicArrow";
	EXPECT_THROW(newHorizonsHeroes::validateHeroRules(malformedRules, true), std::runtime_error);

	const auto originalProducer = std::find_if(attackerSideHero->getHeroType()->specialty.begin(),
		attackerSideHero->getHeroType()->specialty.end(), [](const auto & bonus)
		{
			return bonus && bonus->type == BonusType::SPECIAL_SPELL_SCALING
				&& bonus->subtype == BonusSubtypeID(SpellID(SpellID::CURE));
		});
	ASSERT_NE(originalProducer, attackerSideHero->getHeroType()->specialty.end());
	const int prototypeValue = (*originalProducer)->val;

	auto unrelatedProducer = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPECIAL_SPELL_SCALING, BonusSource::OTHER, 7, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::FIREBALL)));
	attackerSideHero->addNewBonus(unrelatedProducer);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::SPECIAL_SPELL_SCALING,
		BonusSubtypeID(SpellID(SpellID::FIREBALL))), 7);
	EXPECT_EQ((*originalProducer)->val, prototypeValue)
		<< "Conversion is local to Uland and never rewrites the shared hero prototype";

	const auto heroId = attackerSideHero->id;
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.iser.version = ESerializationVersion::CURRENT;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	auto * loadedUland = restored.getHero(heroId);
	ASSERT_NE(loadedUland, nullptr);
	EXPECT_EQ(loadedUland->getNonDamageSpellSpecialtyBonusPercent(SpellID::CURE), 20);
	EXPECT_EQ(loadedUland->valOfBonuses(BonusType::SPECIAL_SPELL_SCALING,
		BonusSubtypeID(SpellID(SpellID::FIREBALL))), 7);

	prepareBattle();
	const auto * cure = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(cure, nullptr);
	const auto lightMagic = SecondarySkill(SecondarySkill::decode(std::string(CURE_SKILL_ID)));
	static constexpr std::array<int, 4> schoolCoefficients = {100, 115, 130, 145};
	for(size_t rank = 0; rank < schoolCoefficients.size(); ++rank)
	{
		attackerSideHero->setSecSkillLevel(lightMagic, static_cast<int>(rank), ChangeValueMode::ABSOLUTE);
		ordinaryHero->setSecSkillLevel(lightMagic, static_cast<int>(rank), ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientPercent(
			battle()->getMagicRules(), attackerSideHero, SpellID::CURE), schoolCoefficients[rank]);

		spells::BattleCast specialistCast(battle(), attackerSideHero, spells::Mode::HERO, cure);
		spells::BattleCast ordinaryCast(battle(), ordinaryHero, spells::Mode::HERO, cure);
		EXPECT_EQ(cure->battleMechanics(&specialistCast)->getEffectValue(),
			expectedCureHealing(TEST_SPELL_POWER, schoolCoefficients[rank], 20)) << "School rank " << rank;
		EXPECT_EQ(cure->battleMechanics(&ordinaryCast)->getEffectValue(),
			expectedCureHealing(TEST_SPELL_POWER, schoolCoefficients[rank], 0)) << "Ordinary school rank " << rank;
	}

	attackerSideHero->setSecSkillLevel(lightMagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({std::string(CURE_SKILL_ID), std::string(HEALER_PERK_ID)});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(CURE_SKILL_ID), std::string(HEALER_PERK_ID)));
	const int64_t basicComponent = expectedCureHealing(TEST_SPELL_POWER, 115, 20) - 25;
	spells::BattleCast healerCast(battle(), attackerSideHero, spells::Mode::HERO, cure);
	EXPECT_EQ(cure->battleMechanics(&healerCast)->getEffectValue(), 25 + basicComponent * 120 / 100)
		<< "Healer continues to affect only the already-scaled Spell Power component";
}

TEST_F(NewHorizonsHealingSpecialtyTest, AcceptedUlandCureMatchesForecastAndDoesNotResurrect)
{
	startSpecialistMap();
	prepareBattle();
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(CURE_SKILL_ID))),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const int64_t expectedHealing = expectedCureHealing(TEST_SPELL_POWER, 115, 20);
	ASSERT_NO_FATAL_FAILURE(injure(2 * static_cast<int64_t>(target->getMaxHealth()) + expectedHealing + 1));
	const auto countBefore = target->getCount();
	ASSERT_LT(countBefore, 100) << "The test target has a casualty before Cure";
	const auto missingHealth = target->getSurvivingMissingHealth();
	ASSERT_EQ(missingHealth, expectedHealing + 1);

	const auto * cure = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(cure, nullptr);
	spells::BattleCast forecast(battle(), attackerSideHero, spells::Mode::HERO, cure);
	const int64_t forecastHealing = cure->battleMechanics(&forecast)->getEffectValue();
	ASSERT_EQ(forecastHealing, expectedHealing);
	const auto healthBefore = target->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::CURE;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(target->getAvailableHealth() - healthBefore, std::min(forecastHealing, missingHealth));
	EXPECT_EQ(target->getCount(), countBefore) << "Cure heals survivors without resurrecting the casualty";
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 4);
	EXPECT_TRUE(battle()->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsHealingSpecialtyTest, MissingSavedRulesPreserveUlandsLegacyProducerThroughSaveLoad)
{
	enableNonDamageSpecialtyRules = false;
	startSpecialistMap();
	ASSERT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::CURE), 0);
	EXPECT_FALSE(attackerSideHero->getPrimaryGrowthRules().Struct().contains("nonDamageSpellSpecialties"));
	const auto originalProducer = std::find_if(attackerSideHero->getHeroType()->specialty.begin(),
		attackerSideHero->getHeroType()->specialty.end(), [](const auto & bonus)
		{
			return bonus && bonus->type == BonusType::SPECIAL_SPELL_SCALING
				&& bonus->subtype == BonusSubtypeID(SpellID(SpellID::CURE));
		});
	ASSERT_NE(originalProducer, attackerSideHero->getHeroType()->specialty.end());
	const auto prototypeValue = (*originalProducer)->val;
	const auto local = attackerSideHero->getExportedBonusList();
	EXPECT_TRUE(std::ranges::any_of(local, [&prototypeValue](const auto & bonus)
	{
		return bonus && bonus->type == BonusType::SPECIAL_SPELL_SCALING
			&& bonus->subtype == BonusSubtypeID(SpellID(SpellID::CURE)) && bonus->val == prototypeValue;
	})) << "An old snapshot keeps Uland's exact original Cure producer active";

	const auto heroId = attackerSideHero->id;
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.iser.version = ESerializationVersion::CURRENT;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	auto * loadedUland = restored.getHero(heroId);
	ASSERT_NE(loadedUland, nullptr);
	EXPECT_FALSE(loadedUland->getPrimaryGrowthRules().Struct().contains("nonDamageSpellSpecialties"));
	EXPECT_EQ(loadedUland->getNonDamageSpellSpecialtyBonusPercent(SpellID::CURE), 0);
	const auto loadedBonuses = loadedUland->getExportedBonusList();
	EXPECT_TRUE(std::ranges::any_of(loadedBonuses, [&prototypeValue](const auto & bonus)
	{
		return bonus && bonus->type == BonusType::SPECIAL_SPELL_SCALING
			&& bonus->subtype == BonusSubtypeID(SpellID(SpellID::CURE)) && bonus->val == prototypeValue;
	}));
}
