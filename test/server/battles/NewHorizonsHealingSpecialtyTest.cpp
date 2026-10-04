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
#include "../../../lib/spells/effects/Effect.h"
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
constexpr std::string_view RESURRECTION_SPECIALTY_SPELL = "core:resurrection";
constexpr std::string_view CURE_SKILL_ID = "new-horizons:lightMagic";
constexpr std::string_view SPELLCRAFT_SKILL_ID = "new-horizons:spellcraft";
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

int64_t expectedResurrectionPool(int64_t spellPower, int coefficientBasisPoints, int specialtyPercent)
{
	const int64_t numerator = static_cast<int64_t>(newHorizonsMagic::RESURRECTION_SPELL_POWER_HP_PER_POINT)
		* spellPower * coefficientBasisPoints * (100 + specialtyPercent);
	return newHorizonsMagic::RESURRECTION_BASE_POOL_HP + numerator / 1'000'000;
}

const Bonus * findSpellScalingPrototype(const CGHeroInstance * hero, SpellID spell)
{
	const auto * type = hero->getHeroType();
	const auto found = std::find_if(type->specialty.begin(), type->specialty.end(), [spell](const auto & bonus)
	{
		return bonus && bonus->type == BonusType::SPECIAL_SPELL_SCALING
			&& bonus->subtype == BonusSubtypeID(SpellID(spell));
	});
	return found == type->specialty.end() ? nullptr : found->get();
}

class NewHorizonsHealingSpecialtyTest : public HeroCommandFixture
{
protected:
	bool enableNonDamageSpecialtyRules = true;
	bool includeResurrectionSpecialty = false;
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
			if(includeResurrectionSpecialty)
				spells.emplace_back(std::string(RESURRECTION_SPECIALTY_SPELL));
			heroRules.setOverrideFlag(true);
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

	void startSpecialistMap(std::string_view specialistHero = "core:uland")
	{
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		ASSERT_TRUE(pikeman.hasValue());
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.name("NewHorizonsHealingSpecialty")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, heroType(specialistHero), PlayerColor(0)).heroExperience(0)
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
		prepareHeroesForBattle(SpellID::CURE, spellPower);

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

	void prepareResurrectionBattle(int spellPower = TEST_SPELL_POWER)
	{
		prepareHeroesForBattle(SpellID::RESURRECTION, spellPower);
		const CreatureID archangel(CreatureID::decode("core:archangel"));
		const CreatureID peasant(CreatureID::decode("core:peasant"));
		attackerSideHero->clearSlots();
		ordinaryHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), archangel, 100));
		ASSERT_TRUE(ordinaryHero->setCreature(SlotID(0), peasant, 1));

		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
				target = const_cast<CStack *>(dynamic_cast<const CStack *>(unit));
		}
		ASSERT_NE(target, nullptr);
		ASSERT_FALSE(target->isSummoned()) << "Resurrection excludes temporary summoned slots";
		beginCombat();
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}

	void prepareHeroesForBattle(SpellID spell, int spellPower)
	{
		for(auto * hero : {attackerSideHero, ordinaryHero})
		{
			if(!hero->getArt(ArtifactPosition::SPELLBOOK))
				giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			hero->addSpellToSpellbook(spell);
			hero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
			setTestSpellPointTotal(hero, 100);
		}
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

	spells::effects::SpellEffectValue healthForecast(const CStack * unit,
		const spells::Mechanics * mechanics) const
	{
		spells::Target aim;
		aim.emplace_back(unit);
		const auto spellTarget = mechanics->canonicalizeTarget(aim);
		spells::effects::SpellEffectValue total;
		mechanics->forEachEffect([&](const spells::effects::Effect & effect)
		{
			const auto affected = effect.transformTarget(mechanics, aim, spellTarget);
			total += effect.getHealthChange(mechanics, affected);
			return false;
		});
		return total;
	}

	void expectConvertedResurrectionProducer(const CGHeroInstance * hero, int prototypeValue) const
	{
		const auto bonuses = hero->getExportedBonusList();
		EXPECT_TRUE(std::ranges::any_of(bonuses, [](const auto & bonus)
		{
			return bonus && bonus->type == BonusType::SPECIAL_SPELL_SCALING
				&& bonus->subtype == BonusSubtypeID(SpellID(SpellID::RESURRECTION))
				&& bonus->val == 0;
		})) << "The exact legacy producer becomes an inert, instance-local marker";
		EXPECT_EQ(findSpellScalingPrototype(hero, SpellID::RESURRECTION)->val, prototypeValue)
			<< "The shared Alamar/Jeddite prototype must remain unchanged";
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

TEST_F(NewHorizonsHealingSpecialtyTest, AlamarScalesOnlyResurrectionsPowerComponentAndAcceptedCast)
{
	includeResurrectionSpecialty = true;
	startSpecialistMap("core:alamar");
	ASSERT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:alamar");
	ASSERT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::RESURRECTION), 20);
	EXPECT_EQ(ordinaryHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::RESURRECTION), 0);
	const auto savedRules = newHorizonsHeroes::nonDamageSpellSpecialtyRules(
		attackerSideHero->getPrimaryGrowthRules());
	ASSERT_TRUE(savedRules.has_value());
	ASSERT_EQ(savedRules->spells.size(), 2u);
	EXPECT_EQ(savedRules->componentPercent, 20);
	EXPECT_EQ(savedRules->spells[0], SpellID::CURE);
	EXPECT_EQ(savedRules->spells[1], SpellID::RESURRECTION);
	JsonNode resOnlyRules(JsonPath::builtin("config/newHorizonsHeroes"));
	auto & supportedSpells = resOnlyRules["nonDamageSpellSpecialties"]["spells"].Vector();
	supportedSpells.clear();
	supportedSpells.emplace_back(std::string(RESURRECTION_SPECIALTY_SPELL));
	EXPECT_NO_THROW(newHorizonsHeroes::validateHeroRules(resOnlyRules, true));
	supportedSpells.emplace_back(std::string(RESURRECTION_SPECIALTY_SPELL));
	EXPECT_THROW(newHorizonsHeroes::validateHeroRules(resOnlyRules, true), std::runtime_error)
		<< "The supported-spell list must reject duplicate Resurrection entries";

	const auto * prototype = findSpellScalingPrototype(attackerSideHero, SpellID::RESURRECTION);
	ASSERT_NE(prototype, nullptr);
	const int prototypeValue = prototype->val;
	expectConvertedResurrectionProducer(attackerSideHero, prototypeValue);
	auto unrelatedProducer = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPECIAL_SPELL_SCALING, BonusSource::OTHER, 7, BonusSourceID(),
		BonusSubtypeID(SpellID(SpellID::FIREBALL)));
	attackerSideHero->addNewBonus(unrelatedProducer);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::SPECIAL_SPELL_SCALING,
		BonusSubtypeID(SpellID(SpellID::FIREBALL))), 7);
	EXPECT_EQ(prototype->val, prototypeValue) << "Conversion must not rewrite Alamar's shared prototype";

	const auto heroId = attackerSideHero->id;
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.iser.version = ESerializationVersion::CURRENT;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	auto * loadedAlamar = restored.getHero(heroId);
	ASSERT_NE(loadedAlamar, nullptr);
	EXPECT_EQ(loadedAlamar->getNonDamageSpellSpecialtyBonusPercent(SpellID::RESURRECTION), 20);
	EXPECT_EQ(loadedAlamar->valOfBonuses(BonusType::SPECIAL_SPELL_SCALING,
		BonusSubtypeID(SpellID(SpellID::FIREBALL))), 7);
	expectConvertedResurrectionProducer(loadedAlamar, prototypeValue);

	prepareResurrectionBattle();
	const auto * resurrection = SpellID(SpellID::RESURRECTION).toSpell();
	ASSERT_NE(resurrection, nullptr);
	auto lightMagic = SecondarySkill(SecondarySkill::decode(std::string(CURE_SKILL_ID)));
	auto spellcraft = SecondarySkill(SecondarySkill::decode(std::string(SPELLCRAFT_SKILL_ID)));
	ASSERT_TRUE(lightMagic.hasValue());
	ASSERT_TRUE(spellcraft.hasValue());
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	ordinaryHero->setSecSkillLevel(spellcraft, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	static constexpr std::array<int, 4> schoolCoefficientsBasisPoints = {10000, 11500, 13000, 14500};
	for(size_t rank = 0; rank < schoolCoefficientsBasisPoints.size(); ++rank)
	{
		attackerSideHero->setSecSkillLevel(lightMagic, static_cast<int>(rank), ChangeValueMode::ABSOLUTE);
		ordinaryHero->setSecSkillLevel(lightMagic, static_cast<int>(rank), ChangeValueMode::ABSOLUTE);
		const int coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
			battle()->getMagicRules(), attackerSideHero, SpellID::RESURRECTION);
		EXPECT_EQ(coefficient, schoolCoefficientsBasisPoints[rank]);
		spells::BattleCast specialistCast(battle(), attackerSideHero, spells::Mode::HERO, resurrection);
		spells::BattleCast ordinaryCast(battle(), ordinaryHero, spells::Mode::HERO, resurrection);
		const auto specialistMechanics = resurrection->battleMechanics(&specialistCast);
		const auto ordinaryMechanics = resurrection->battleMechanics(&ordinaryCast);
		EXPECT_EQ(specialistMechanics->getEffectValue(), expectedResurrectionPool(
			specialistMechanics->getEffectPower(), coefficient, 20)) << "School rank " << rank;
		EXPECT_EQ(ordinaryMechanics->getEffectValue(), expectedResurrectionPool(
			ordinaryMechanics->getEffectPower(), coefficient, 0)) << "ordinary control, rank " << rank;
	}

	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(lightMagic, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	spells::BattleCast fixedBaseCast(battle(), attackerSideHero, spells::Mode::HERO, resurrection);
	EXPECT_EQ(resurrection->battleMechanics(&fixedBaseCast)->getEffectValue(), 100)
		<< "The 20% specialty does not scale Resurrection's fixed 100 HP pool";

	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, TEST_SPELL_POWER, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(lightMagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	spells::BattleCast acceptedCast(battle(), attackerSideHero, spells::Mode::HERO, resurrection);
	const auto mechanics = resurrection->battleMechanics(&acceptedCast);
	const auto expectedPool = expectedResurrectionPool(mechanics->getEffectPower(),
		newHorizonsMagic::spellPowerCoefficientBasisPoints(
			battle()->getMagicRules(), attackerSideHero, SpellID::RESURRECTION), 20);
	ASSERT_NO_FATAL_FAILURE(injure(expectedPool + 1));
	spells::Target aim;
	aim.emplace_back(target);
	ASSERT_TRUE(mechanics->isNewHorizonsResurrection());
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	const auto forecast = healthForecast(target, mechanics.get());
	ASSERT_EQ(mechanics->getEffectValue(), expectedPool);
	ASSERT_GT(forecast.hpDelta, 0);
	const auto healthBefore = target->getAvailableHealth();
	const auto countBefore = target->getCount();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_EQ(battle()->battleGetSpellCost(resurrection, attackerSideHero), 22);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::RESURRECTION;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(target->getAvailableHealth() - healthBefore, forecast.hpDelta);
	EXPECT_EQ(target->getCount() - countBefore, forecast.unitsDelta);
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 22);
}

TEST_F(NewHorizonsHealingSpecialtyTest, JedditeRetainsTheMappedResurrectionSpecialtyAlias)
{
	includeResurrectionSpecialty = true;
	startSpecialistMap("core:jeddite");
	ASSERT_EQ(attackerSideHero->getHeroType()->getJsonKey(), "core:jeddite");
	ASSERT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::RESURRECTION), 20);
	const auto * prototype = findSpellScalingPrototype(attackerSideHero, SpellID::RESURRECTION);
	ASSERT_NE(prototype, nullptr);
	expectConvertedResurrectionProducer(attackerSideHero, prototype->val);
	prepareResurrectionBattle();

	const auto * resurrection = SpellID(SpellID::RESURRECTION).toSpell();
	ASSERT_NE(resurrection, nullptr);
	const auto lightMagic = SecondarySkill(SecondarySkill::decode(std::string(CURE_SKILL_ID)));
	const auto spellcraft = SecondarySkill(SecondarySkill::decode(std::string(SPELLCRAFT_SKILL_ID)));
	attackerSideHero->setSecSkillLevel(lightMagic, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, resurrection);
	const auto mechanics = resurrection->battleMechanics(&cast);
	const int coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, SpellID::RESURRECTION);
	EXPECT_EQ(mechanics->getEffectValue(), expectedResurrectionPool(mechanics->getEffectPower(), coefficient, 20));
}

TEST_F(NewHorizonsHealingSpecialtyTest, HistoricalCureOnlyRulesKeepAlamarsResurrectionProducerUnconverted)
{
	startSpecialistMap("core:alamar");
	EXPECT_EQ(attackerSideHero->getNonDamageSpellSpecialtyBonusPercent(SpellID::RESURRECTION), 0);
	const auto savedRules = newHorizonsHeroes::nonDamageSpellSpecialtyRules(
		attackerSideHero->getPrimaryGrowthRules());
	ASSERT_TRUE(savedRules.has_value());
	ASSERT_EQ(savedRules->spells.size(), 1u);
	EXPECT_EQ(savedRules->spells.front(), SpellID::CURE);
	const auto * prototype = findSpellScalingPrototype(attackerSideHero, SpellID::RESURRECTION);
	ASSERT_NE(prototype, nullptr);
	const int prototypeValue = prototype->val;
	const auto local = attackerSideHero->getExportedBonusList();
	EXPECT_TRUE(std::ranges::any_of(local, [prototypeValue](const auto & bonus)
	{
		return bonus && bonus->type == BonusType::SPECIAL_SPELL_SCALING
			&& bonus->subtype == BonusSubtypeID(SpellID(SpellID::RESURRECTION))
			&& bonus->val == prototypeValue;
	})) << "A historical Cure-only allowlist must preserve Alamar's unrelated producer";

	const auto heroId = attackerSideHero->id;
	CMemorySerializer memory;
	memory.oser.version = ESerializationVersion::CURRENT;
	memory.iser.version = ESerializationVersion::CURRENT;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	auto * loadedAlamar = restored.getHero(heroId);
	ASSERT_NE(loadedAlamar, nullptr);
	EXPECT_EQ(loadedAlamar->getNonDamageSpellSpecialtyBonusPercent(SpellID::RESURRECTION), 0);
	EXPECT_TRUE(std::ranges::any_of(loadedAlamar->getExportedBonusList(), [prototypeValue](const auto & bonus)
	{
		return bonus && bonus->type == BonusType::SPECIAL_SPELL_SCALING
			&& bonus->subtype == BonusSubtypeID(SpellID(SpellID::RESURRECTION))
			&& bonus->val == prototypeValue;
	}));

	prepareResurrectionBattle();
	const auto * resurrection = SpellID(SpellID::RESURRECTION).toSpell();
	ASSERT_NE(resurrection, nullptr);
	const auto lightMagic = SecondarySkill(SecondarySkill::decode(std::string(CURE_SKILL_ID)));
	const auto spellcraft = SecondarySkill(SecondarySkill::decode(std::string(SPELLCRAFT_SKILL_ID)));
	attackerSideHero->setSecSkillLevel(lightMagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, resurrection);
	const auto mechanics = resurrection->battleMechanics(&cast);
	const int coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, SpellID::RESURRECTION);
	const int64_t expectedPool = expectedResurrectionPool(mechanics->getEffectPower(), coefficient, 0);
	EXPECT_EQ(mechanics->getEffectValue(), expectedPool);
	ASSERT_NO_FATAL_FAILURE(injure(expectedPool + 1));
	spells::Target aim;
	aim.emplace_back(target);
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	const auto forecast = healthForecast(target, mechanics.get());
	const auto healthBefore = target->getAvailableHealth();
	const auto manaBefore = attackerSideHero->getManaAvailable();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::RESURRECTION;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(target->getAvailableHealth() - healthBefore, forecast.hpDelta);
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 22);
}
