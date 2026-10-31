/*
 * SpellEffectPreviewTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../mock/mock_IGameInfoCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/SpellEffectPreview.h"
#include "../../lib/spells/NewHorizonsDirectDamage.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsCureHealing.h"
#include "../../lib/spells/NewHorizonsTimedUtility.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/spells/NewHorizonsSorceryRevision.h"
#include "../../lib/spells/NewHorizonsShadowMagnitudes.h"
#include "../../lib/spells/NewHorizonsTimedBuffMagnitude.h"
#include "../../lib/spells/NewHorizonsRestorationPools.h"
#include "../../lib/spells/NewHorizonsBlessDuration.h"
#include "../../lib/spells/NewHorizonsLightDamage.h"
#include "../../lib/spells/effects/BattleForm.h"
#include "../../lib/battle/NewHorizonsSoulChain.h"
#include "../../lib/entities/hero/NewHorizonsSchoolSpellcraft.h"
#include "../../lib/spells/NewHorizonsStormOfDaggersDamage.h"
#include "../../lib/spells/NewHorizonsTrollSummonPower.h"
#include "../../lib/spells/NewHorizonsElementalConvergencePower.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../client/windows/SpellbookEntryPresentation.h"
#include "../../lib/texts/ITranslator.h"
#include <map>
#include "../../lib/spells/ISpellMechanics.h"
namespace
{
class PreviewRules final : public ::testing::NiceMock<IGameInfoCallbackMock>
{
public:
	JsonNode rules{JsonPath::builtin("config/newHorizonsMagic")};
	const JsonNode & getMagicRules() const override { return rules; }
};
class PreviewTranslator final : public ITranslator
{
public:
	std::map<std::string, std::string> texts{
		{"new-horizons.spellbook.unit.number", "%s"},
		{"new-horizons.spellbook.effect.damage", "Damage %s"},
		{"new-horizons.spellbook.effect.damage.short", "%s dmg"},
		{"new-horizons.spellbook.currentEffect", "Current effect: %s"},
		{"new-horizons.spellbook.compactNotation", "Compact values may be rounded."},
		{"new-horizons.spellbook.beforeDefenses", "Before target defenses"}};
	const std::string & translateString(const TextIdentifier & id) const override { return texts.at(id.get()); }
};

}
class SpellEffectPreviewTest : public ::testing::Test
{
protected:
	PreviewRules callback;
	CGHeroInstance hero{&callback};
	void SetUp() override
	{
		hero.setHeroType(HeroTypeID(0));
		hero.pushPrimSkill(PrimarySkill::SPELL_POWER, 1);
		// Detached heroes do not inherit the canonical world baseline from gameConfig.
		hero.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE,
			BonusSource::OTHER, 100, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	}
	const CSpell & spell(const char * key) { return *SpellID(SpellID::decode(key)).toSpell(); }
};

TEST_F(SpellEffectPreviewTest, CurrentHeroPowerChangesTheResolvedDamageWithoutSpendingResources)
{
	const auto & bolt = spell("core:iceBolt");
	const auto mana = hero.getManaAvailable();
	const auto known = hero.getSpellsInSpellbook();
	const auto perks = hero.getPerkState().selected;
	const auto first = newHorizonsMagic::spellEffectPreview(hero, bolt);
	ASSERT_TRUE(first.primary);
	EXPECT_EQ(first.primary->value, bolt.calculateDamage(&hero));
	EXPECT_EQ(first, newHorizonsMagic::spellEffectPreview(hero, bolt));
	EXPECT_EQ(hero.getManaAvailable(), mana);
	EXPECT_EQ(hero.getSpellsInSpellbook(), known);
	EXPECT_EQ(hero.getPerkState().selected, perks);
	hero.setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const auto second = newHorizonsMagic::spellEffectPreview(hero, bolt);
	ASSERT_TRUE(second.primary);
	EXPECT_GT(second.primary->value, first.primary->value);
	EXPECT_EQ(second.primary->value, bolt.calculateDamage(&hero));
	EXPECT_EQ(hero.getManaAvailable(), mana);
}

TEST_F(SpellEffectPreviewTest, CureUsesTheSharedHealingBudgetAndPoisonNamesItsFirstActivationTick)
{
	const auto & cure = spell("core:cure");
	const auto healing = newHorizonsMagic::spellEffectPreview(hero, cure);
	ASSERT_TRUE(healing.primary);
	EXPECT_EQ(healing.primary->value, hero.getSpellBonus(&cure, newHorizonsMagic::cureHealingBudget(&hero, hero.getEffectPower(&cure),
		newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules, &hero, cure.getId())), nullptr));
	const auto poison = newHorizonsMagic::spellEffectPreview(hero, spell("new-horizons:poison"));
	ASSERT_TRUE(poison.primary);
	EXPECT_EQ(poison.labelKey, "new-horizons.spellbook.effect.poison");
	EXPECT_EQ(poison.qualificationKey, "new-horizons.spellbook.poisonTicks");
}

TEST(SpellEffectPreviewFormulaTest, WarcastingSharesTheAuthorityPowerFloorInsteadOfScalingTheFixedBase)
{
	const newHorizonsMagic::DirectDamageFormula formula{20, 20};
	EXPECT_EQ(formula.evaluateBasisPoints(24, 10, 10000, 0, 0, 10), 72);
	EXPECT_EQ(formula.evaluateBasisPoints(24, 10), 68);
	for(const int power : {0, 1, 17, 24, 101})
		EXPECT_EQ(formula.evaluateBasisPoints(power, 10, 12650, 25, 15, 10),
			20 + spells::scaleSpellPowerComponentWithCoefficientBasisPoints(20LL * power, 10, 12650, 10, 25, 15));
}

TEST_F(SpellEffectPreviewTest, HolyArmorAndForgetfulnessUseTheSharedUtilityResultAtCurrentPower)
{
	const auto & armor = spell("new-horizons:holyArmor");
	const auto & forget = spell("core:forgetfulness");
	for(const int power : {1, 80, 200})
	{
		hero.setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		const auto base = newHorizonsMagic::configuredHolyArmorBase(armor, hero.getEffectLevel(&armor));
		ASSERT_TRUE(base);
		const auto reduction = newHorizonsMagic::spellEffectPreview(hero, armor);
		ASSERT_TRUE(reduction.primary);
		EXPECT_EQ(reduction.primary->value, newHorizonsMagic::holyArmorReduction(*base,
			hero.getEffectPower(&armor), newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules,
				&hero, armor.getId()), 0, 0, false));
		const auto duration = newHorizonsMagic::spellEffectPreview(hero, forget);
		ASSERT_TRUE(duration.primary);
		EXPECT_EQ(duration.primary->value, newHorizonsMagic::forgetfulnessBaseDuration(hero.getEffectPower(&forget),
			newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules, &hero, forget.getId()), 0, 0));
	}
}

TEST_F(SpellEffectPreviewTest, HexAndPlagueUseCapturedDamageHelpersWithoutAddingUnownedModifiers)
{
	for(const int power : {1, 100})
	{
		hero.setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		const auto & hex = spell("new-horizons:hexOfPain");
		const auto capturedHex = newHorizonsMagic::spellEffectPreview(hero, hex);
		ASSERT_TRUE(capturedHex.primary);
		ASSERT_TRUE(capturedHex.secondary);
		EXPECT_EQ(capturedHex.primary->value, newHorizonsMagic::hexOfPainFlatDamage(hero.getEffectPower(&hex),
			newHorizonsMagic::damageSpellPowerCoefficientBasisPoints(callback.rules, &hero, hex.getId()), 0, 0,
			hero.getDamageSpellSpecialtyBonusPercent(hex.getId()), false));
		EXPECT_EQ(capturedHex.secondary->value, newHorizonsMagic::hexOfPainDamageSharePercent());
		const auto & plague = spell("new-horizons:plague");
		const auto capturedPlague = newHorizonsMagic::spellEffectPreview(hero, plague);
		ASSERT_TRUE(capturedPlague.primary);
		EXPECT_EQ(capturedPlague.primary->value, newHorizonsMagic::plagueTickDamage(hero.getEffectPower(&plague),
			newHorizonsMagic::damageSpellPowerCoefficientBasisPoints(callback.rules, &hero, plague.getId()),
			hero.getDamageSpellSpecialtyBonusPercent(plague.getId())));
	}
}

TEST_F(SpellEffectPreviewTest, KnownUtilityFamiliesExposeTheirActualSharedCasterQuantity)
{
	hero.setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const auto & sand = spell("core:quicksand");
	const auto sandPreview = newHorizonsMagic::spellEffectPreview(hero, sand);
	ASSERT_TRUE(sandPreview.primary);
	const auto sandCount = newHorizonsMagic::quicksandPatchCount(callback.rules, &hero, sand.getId(),
		hero.getEffectPower(&sand), hero.getEffectPowerDivisor(&sand));
	ASSERT_TRUE(sandCount);
	EXPECT_EQ(sandPreview.primary->value, *sandCount);
	const auto & mine = spell("core:landMine");
	const auto minePreview = newHorizonsMagic::spellEffectPreview(hero, mine);
	ASSERT_TRUE(minePreview.primary);
	ASSERT_TRUE(minePreview.secondary);
	EXPECT_EQ(minePreview.primary->value, mine.calculateDamage(&hero));
	EXPECT_EQ(minePreview.secondary->value, newHorizonsMagic::landMineCountForHero(hero.getEffectPower(&mine),
		newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules, &hero, mine.getId()), &hero));
	const auto & sorrow = spell("core:sorrow");
	const auto sorrowPreview = newHorizonsMagic::spellEffectPreview(hero, sorrow);
	ASSERT_TRUE(sorrowPreview.primary);
	const auto penalty = newHorizonsMagic::sorrowMoralePenalty(callback.rules, &hero, sorrow.getId(), hero.getEffectPower(&sorrow));
	ASSERT_TRUE(penalty);
	EXPECT_EQ(sorrowPreview.primary->value, *penalty);
	const auto & curse = spell("core:curse");
	const auto cursePreview = newHorizonsMagic::spellEffectPreview(hero, curse);
	ASSERT_TRUE(cursePreview.primary);
	EXPECT_EQ(cursePreview.primary->value, *newHorizonsMagic::curseDurationRounds(callback.rules, &hero, curse.getId()));
	const auto & reanimate = spell("new-horizons:reanimate");
	const auto pool = newHorizonsMagic::spellEffectPreview(hero, reanimate);
	ASSERT_TRUE(pool.primary);
	EXPECT_EQ(pool.primary->value, *newHorizonsMagic::reanimateBaseHealingPool(callback.rules, &hero, reanimate.getId(),
		hero.getPrimSkillLevel(PrimarySkill::SPELL_POWER)));
	const auto & phantom = spell("new-horizons:phantomArmy");
	const auto integrity = newHorizonsMagic::spellEffectPreview(hero, phantom);
	ASSERT_TRUE(integrity.primary);
	EXPECT_EQ(integrity.primary->scale, 100);
	EXPECT_EQ(integrity.primary->value, newHorizonsSorcery::phantomArmyIntegrityWithModifiers(10000,
		hero.getEffectPower(&phantom), false,
		newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules, &hero, phantom.getId()), 0, 0,
		hero.getNonDamageSpellSpecialtyBonusPercent(phantom.getId())));
}

TEST_F(SpellEffectPreviewTest, TimeStopBookRadiusUsesTheCapturedCoefficientOverload)
{
	const auto & stop = spell("new-horizons:timeStop");
	for(const int power : {1, 100, 250})
	{
		hero.setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		const auto preview = newHorizonsMagic::spellEffectPreview(hero, stop);
		ASSERT_TRUE(preview.primary);
		EXPECT_EQ(preview.primary->value, newHorizonsSorcery::timeStopRadius(hero.getEffectPower(&stop),
			newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules, &hero, stop.getId()), 0, 0, false));
	}
}

TEST_F(SpellEffectPreviewTest, FocusAndLivingArmorUseSharedAuthorityAndDetailFormulaOwners)
{
	const auto & focus = spell("new-horizons:focusMagic");
	const auto & armor = spell("new-horizons:summonLivingArmor");
	const auto profile = newHorizonsSorceryRevision::focusRules(callback.rules);
	ASSERT_TRUE(profile);
	const auto & savedArmor = callback.rules["spells"][armor.getJsonKey()]["livingArmor"];
	for(const int power : {1, 100, 250})
	{
		hero.setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		const auto penetration = newHorizonsMagic::spellEffectPreview(hero, focus);
		ASSERT_TRUE(penetration.primary);
		EXPECT_EQ(penetration.primary->value, newHorizonsSorceryRevision::focusMarkBasisPoints(*profile,
			hero.getEffectPower(&focus), newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules,
				&hero, focus.getId()), 0, 0, hero.getNonDamageSpellSpecialtyBonusPercent(focus.getId())));
		const auto pool = newHorizonsMagic::spellEffectPreview(hero, armor);
		ASSERT_TRUE(pool.primary);
		EXPECT_EQ(pool.primary->value, newHorizonsSorceryRevision::livingArmorHitPoints(
			savedArmor["baseHitPoints"].Integer(), savedArmor["spellPowerCoefficient"].Integer(), hero.getEffectPower(&armor),
			hero.getEffectPowerDivisor(&armor), newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules,
				&hero, armor.getId()), 0, 0));
	}
}

TEST_F(SpellEffectPreviewTest, ShadowMagnitudesUseCapturedCoefficientHelpers)
{
	const auto & vampirism = spell("new-horizons:vampirism");
	const auto & doom = spell("new-horizons:doom");
	for(const int power : {1, 100, 500})
	{
		hero.setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		const auto healing = newHorizonsMagic::spellEffectPreview(hero, vampirism);
		ASSERT_TRUE(healing.primary);
		EXPECT_EQ(healing.primary->scale, 100);
		EXPECT_EQ(healing.primary->value, newHorizonsMagic::vampirismHealBasisPoints(hero.getEffectPower(&vampirism),
			newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules, &hero, vampirism.getId()), 0, 0, false));
		const auto crippling = newHorizonsMagic::spellEffectPreview(hero, doom);
		ASSERT_TRUE(crippling.primary);
		EXPECT_EQ(crippling.primary->value, newHorizonsMagic::doomCripplingPenaltyPercent(hero.getEffectPower(&doom),
			newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules, &hero, doom.getId()), 0, 0));
	}
}

TEST_F(SpellEffectPreviewTest, ChangingHeroPowerKeepsBookAndDetailOnTheSameResolvedReadout)
{
	const auto & bolt = spell("core:iceBolt");
	const auto mana = hero.getManaAvailable();
	const auto spells = hero.getSpellsInSpellbook();
	PreviewTranslator translator;
	std::string previousRow;
	for(const int power : {1, 100})
	{
		hero.setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		const auto preview = newHorizonsMagic::spellEffectPreview(hero, bolt);
		ASSERT_TRUE(preview.primary);
		const auto value = std::to_string(bolt.calculateDamage(&hero));
		const auto row = spellbookPresentation::effectLine(preview, translator,
			[](const std::string & text) { return static_cast<int>(text.size()); });
		EXPECT_EQ(row, "Damage " + value);
		EXPECT_EQ(spellbookPresentation::effectDetail(preview, translator),
			"Current effect: Damage " + value + "\nBefore target defenses\nCompact values may be rounded.");
		if(!previousRow.empty())
			EXPECT_NE(row, previousRow);
		previousRow = row;
		EXPECT_EQ(hero.getManaAvailable(), mana);
		EXPECT_EQ(hero.getSpellsInSpellbook(), spells);
	}
}

TEST_F(SpellEffectPreviewTest, CasterAdditiveThenFixedEnchantOverridesReachTheBookWithoutInventingRecipientTier)
{
	const auto & haste = spell("core:haste");
	EXPECT_DOUBLE_EQ(newHorizonsMagic::casterEnchantValue(nullptr, haste.getId(), 3.75), 3.75);
	auto add = std::make_shared<Bonus>();
	add->type = BonusType::SPECIAL_ADD_VALUE_ENCHANT;
	add->subtype = BonusSubtypeID(haste.getId());
	add->parameters = std::make_shared<BonusParameters>(int32_t{2});
	hero.addNewBonus(add);
	EXPECT_DOUBLE_EQ(newHorizonsMagic::casterEnchantValue(&hero, haste.getId(), 3.75), 5.75);
	const auto base = newHorizonsMagic::configuredAdditiveTimedValue(haste, hero.getEffectLevel(&haste), BonusType::STACKS_SPEED);
	ASSERT_TRUE(base);
	const auto beforeFixed = newHorizonsMagic::spellEffectPreview(hero, haste);
	ASSERT_TRUE(beforeFixed.primary);
	EXPECT_EQ(beforeFixed.primary->value, static_cast<int64_t>(*base + 2));
	auto fixed = std::make_shared<Bonus>();
	fixed->type = BonusType::SPECIAL_FIXED_VALUE_ENCHANT;
	fixed->subtype = BonusSubtypeID(haste.getId());
	fixed->parameters = std::make_shared<BonusParameters>(int32_t{7});
	hero.addNewBonus(fixed);
	EXPECT_DOUBLE_EQ(newHorizonsMagic::casterEnchantValue(&hero, haste.getId(), 3.75), 7);
	const auto afterFixed = newHorizonsMagic::spellEffectPreview(hero, haste);
	ASSERT_TRUE(afterFixed.primary);
	EXPECT_EQ(afterFixed.primary->value, 7);
}

TEST_F(SpellEffectPreviewTest, BasicBuffRowsUseSharedSlowMagnitudeAndLoadedHasteFortune)
{
	hero.setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	const auto & slow = spell("core:slow");
	const auto slowed = newHorizonsMagic::spellEffectPreview(hero, slow);
	ASSERT_TRUE(slowed.primary);
	EXPECT_EQ(slowed.primary->value, -newHorizonsSorceryRevision::slowBaseReduction(hero.getEffectPower(&slow),
		newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules, &hero, slow.getId()), 0, 0));
	for(const auto id : {SpellID::HASTE, SpellID::FORTUNE})
	{
		const auto * buff = SpellID(id).toSpell();
		const auto preview = newHorizonsMagic::spellEffectPreview(hero, *buff);
		ASSERT_TRUE(preview.primary);
		const auto type = id == SpellID::HASTE ? BonusType::STACKS_SPEED : BonusType::LUCK;
		const auto configured = newHorizonsMagic::configuredAdditiveTimedValue(*buff, hero.getEffectLevel(buff), type);
		ASSERT_TRUE(configured);
		EXPECT_EQ(preview.primary->value, static_cast<int64_t>(*configured));
	}
}

TEST_F(SpellEffectPreviewTest, StormRangeUsesIndependentPerTargetKernelEndpointsAndCasterFinishing)
{
	const auto & storm = spell("new-horizons:stormOfDaggers");
	hero.setPrimarySkill(PrimarySkill::SPELL_POWER, 3, ChangeValueMode::ABSOLUTE);
	const auto formula = newHorizonsMagic::spellDirectDamage(callback.rules, storm.getJsonKey());
	ASSERT_TRUE(formula);
	const auto preview = newHorizonsMagic::spellEffectPreview(hero, storm);
	ASSERT_TRUE(preview.primary);
	ASSERT_TRUE(preview.secondary);
	const auto coefficient = newHorizonsMagic::directDamageSpellPowerCoefficientBasisPoints(callback.rules, &hero, storm.getId());
	const auto endpoint = [&](int count)
	{
		return storm.adjustRawDamage(&hero, nullptr, spells::stormOfDaggersRoundedDamage(formula->base,
			formula->powerCoefficient, hero.getEffectPower(&storm), hero.getEffectPowerDivisor(&storm),
			coefficient, 0, 0, count, false));
	};
	EXPECT_EQ(preview.primary->value, endpoint(newHorizonsMagic::STORM_OF_DAGGERS_MAX_TARGETS));
	EXPECT_EQ(preview.secondary->value, endpoint(1));
	EXPECT_EQ(preview.qualificationKey, "new-horizons.spellbook.stormCounts");
}

TEST_F(SpellEffectPreviewTest, ActiveNatureSummonRowsExposeSharedHitPointPoolsWithoutGuessingCounts)
{
	const auto & trolls = spell("new-horizons:summonTrolls");
	const auto & convergence = spell("new-horizons:elementalConvergence");
	for(const int power : {1, 100, 500})
	{
		hero.setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		const auto trollPool = newHorizonsMagic::spellEffectPreview(hero, trolls);
		ASSERT_TRUE(trollPool.primary);
		EXPECT_EQ(trollPool.primary->value, newHorizonsMagic::trollSummonHitPoints(hero.getEffectPower(&trolls),
			newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules, &hero, trolls.getId()), 0, 0, false));
		const auto elementalPool = newHorizonsMagic::spellEffectPreview(hero, convergence);
		ASSERT_TRUE(elementalPool.primary);
		EXPECT_EQ(elementalPool.primary->value, newHorizonsMagic::elementalConvergenceHitPoints(hero.getEffectPower(&convergence),
			newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules, &hero, convergence.getId()), 0, 0, false));
		EXPECT_EQ(elementalPool.qualificationKey, "new-horizons.spellbook.summonPlacement");
	}
}

TEST_F(SpellEffectPreviewTest, SoulChainUsesCapturedNumericalCoefficientWithoutAnExtraCastMultiplier)
{
	const auto & chain = spell("new-horizons:soulChain");
	for(const int power : {0, 1, 100})
	{
		hero.setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		const auto preview = newHorizonsMagic::spellEffectPreview(hero, chain);
		ASSERT_TRUE(preview.primary);
		const auto captured = newHorizonsHeroes::captureSpellcraftContext(callback.rules, &hero, chain.getId(), true);
		const auto coefficient = captured.factors
			? newHorizonsHeroes::composeSpellcraftCoefficient(*captured.factors, newHorizonsHeroes::SpellcraftComponent::NUMERICAL, 0)
			: newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(callback.rules, &hero, chain.getId());
		EXPECT_EQ(preview.primary->value, newHorizonsSoulChain::echoPercentBasisPoints(
			std::max<int32_t>(0, hero.getEffectPower(&chain)), coefficient,
			hero.hasActivePerk("new-horizons:shadowMagic", "new-horizons:shadowMagic.soulBinder")));
		EXPECT_EQ(preview.primary->scale, 100);
		EXPECT_EQ(preview.qualificationKey, "new-horizons.spellbook.soulChainRecipient");
	}
}

TEST_F(SpellEffectPreviewTest, RestorationRowsTrackHeroPowerWithoutChoosingARecipient)
{
	for(const auto * id : {"core:resurrection", "new-horizons:guardianSpirit"})
	{
		const auto & restored = spell(id);
		for(const int power : {0, 1, 100})
		{
			hero.setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
			const auto preview = newHorizonsMagic::spellEffectPreview(hero, restored);
			ASSERT_TRUE(preview.primary);
			const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(callback.rules, &hero, restored.getId());
			const int specialty = hero.getNonDamageSpellSpecialtyBonusPercent(restored.getId());
			// This detached fixture deliberately retains legacy primary bounds (1..99).
			const auto actualPower = hero.getEffectPower(&restored);
			ASSERT_FALSE(hero.usesPrimaryGrowth());
			EXPECT_EQ(actualPower, std::clamp(power, 1, 99));
			const auto expected = restored.getId() == SpellID::RESURRECTION
				? hero.getSpellBonus(&restored, newHorizonsMagic::resurrectionRestorationPool(actualPower, coefficient, 0, 0, specialty), nullptr)
				: newHorizonsMagic::guardianSpiritHitPoints(actualPower, coefficient, 0, 0, specialty,
					hero.hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.healer"),
					hero.hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.guardian"));
			EXPECT_EQ(preview.primary->value, expected);
			EXPECT_EQ(preview.qualificationKey, "new-horizons.spellbook.restorationRecipient");
		}
	}
}

TEST_F(SpellEffectPreviewTest, BlessAndItsMassVariantDisplayCurrentCasterDuration)
{
	for(const auto * id : {"core:bless", "new-horizons:massBless"})
	{
		const auto & blessed = spell(id);
		for(const int power : {1, 80, 160})
		{
			hero.setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
			const auto row = newHorizonsMagic::spellEffectPreview(hero, blessed);
			ASSERT_TRUE(row.primary);
			const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(callback.rules, &hero, blessed.getId());
			const auto ordinary = newHorizonsMagic::blessOrdinaryDuration(hero.getEffectPower(&blessed),
				coefficient, 0, 0, hero.getNonDamageSpellSpecialtyBonusPercent(SpellID::BLESS));
			EXPECT_EQ(row.primary->value, newHorizonsMagic::blessCasterDuration(hero, ordinary, false));
			EXPECT_EQ(row.labelKey, "new-horizons.spellbook.effect.bless");
		}
	}
}

TEST_F(SpellEffectPreviewTest, HeavenlyGaleShowsCurrentCasterReductionBeforeUnknownRecipientBonus)
{
	const auto & gale = spell("new-horizons:heavenlyGale");
	for(const int power : {0, 1, 1000})
	{
		hero.setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		const auto row = newHorizonsMagic::spellEffectPreview(hero, gale);
		ASSERT_TRUE(row.primary);
		EXPECT_EQ(row.primary->value, newHorizonsMagic::heavenlyGaleReductionBasisPoints(hero.getEffectPower(&gale),
			newHorizonsMagic::spellPowerCoefficientBasisPoints(callback.rules, &hero, gale.getId()), 0, 0,
			hero.hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.aegis")));
		EXPECT_EQ(row.primary->scale, 100);
		EXPECT_EQ(row.qualificationKey, "new-horizons.spellbook.heavenlyGaleRecipient");
	}
}

TEST_F(SpellEffectPreviewTest, ConditionalFamiliesHaveExplicitQuantitiesWithoutSelectingTargets)
{
	const auto known = hero.getSpellsInSpellbook();
	const auto mana = hero.getManaAvailable();
	for(const auto * id : {"new-horizons:shadowGift", "core:implosion", "new-horizons:soulReaper",
		"new-horizons:handOfFate", "new-horizons:holyWrath", "new-horizons:naturesWrath", "new-horizons:pandemonium"})
	{
		const auto & effect = spell(id);
		const auto row = newHorizonsMagic::spellEffectPreview(hero, effect);
		ASSERT_TRUE(row.primary) << id;
		EXPECT_FALSE(row.qualificationKey.empty()) << id;
		EXPECT_EQ(row, newHorizonsMagic::spellEffectPreview(hero, effect));
	}
	EXPECT_EQ(hero.getManaAvailable(), mana);
	EXPECT_EQ(hero.getSpellsInSpellbook(), known);
}
TEST_F(SpellEffectPreviewTest, HolyWrathRangeUsesTheSameSavedRecipientMultiplierAsAuthority)
{
	const auto & wrath = spell("new-horizons:holyWrath");
	EXPECT_EQ(newHorizonsMagic::holyWrathRecipientMultiplier(callback.rules, wrath.getId(), false), 100);
	EXPECT_EQ(newHorizonsMagic::holyWrathRecipientMultiplier(callback.rules, wrath.getId(), true), 150);
	const auto row = newHorizonsMagic::spellEffectPreview(hero, wrath);
	ASSERT_TRUE(row.primary);
	ASSERT_TRUE(row.secondary);
	EXPECT_LE(row.primary->value, row.secondary->value);
	EXPECT_EQ(row.qualificationKey, "new-horizons.spellbook.holyWrathRecipient");
}

TEST_F(SpellEffectPreviewTest, UtilityActionsExposeLoadedDurationsOrAccurateCategoricalEffects)
{
	for(const auto * id : {"core:berserk", "new-horizons:realityWarp", "new-horizons:confusion",
		"new-horizons:puppetMaster", "core:teleport", "new-horizons:polymorph", "new-horizons:spellLock",
		"new-horizons:theChosen", "new-horizons:purify"})
	{
		const auto row = newHorizonsMagic::spellEffectPreview(hero, spell(id));
		EXPECT_FALSE(row.labelKey.empty()) << id;
	}
}
TEST(BattleFormDuration, LoadedDurationAndExtendShareAuthoritySaturationWithoutEcho)
{
	JsonNode effect;
	effect["duration"].Integer() = 5;
	EXPECT_EQ(spells::effects::BattleFormEffect::configuredDuration(effect), 5);
	EXPECT_EQ(spells::effects::BattleFormEffect::durationWithExtend(5, 1), 6);
	EXPECT_EQ(spells::effects::BattleFormEffect::durationWithExtend(std::numeric_limits<int32_t>::max(), 1),
		std::numeric_limits<int32_t>::max());
}

TEST_F(SpellEffectPreviewTest, RemainingTimedFamiliesShowNumericEffectsAtCurrentHeroPower)
{
	for(const auto * id : {"new-horizons:frailty","new-horizons:hydrasVitality","new-horizons:shieldOfChaos",
		"new-horizons:divineRetribution","new-horizons:entangle","core:misfortune"})
	{
		const auto row = newHorizonsMagic::spellEffectPreview(hero, spell(id));
		EXPECT_TRUE(row.primary) << id;
	}
}

TEST_F(SpellEffectPreviewTest, EveryCuratedCombatRowHasAnExplicitCurrentEffect)
{
	const std::set<std::string> categorical{"new-horizons:sanctuary","core:dispel","core:berserk",
		"new-horizons:realityWarp","new-horizons:confusion","new-horizons:puppetMaster","core:teleport"};
	int covered = 0;
	const auto known = hero.getSpellsInSpellbook();
	const auto mana = hero.getManaAvailable();
	for(const auto & [key, rules] : callback.rules["spells"].Struct())
	{
		if((rules["active"].isBool() && !rules["active"].Bool()) || (rules["heroAccess"].isBool() && !rules["heroAccess"].Bool()))
			continue;
		const auto & current = spell(key.c_str());
		if(!current.isCombat())
			continue;
		for(const int power : {0,1,100,500})
		{
			hero.setPrimarySkill(PrimarySkill::SPELL_POWER,power,ChangeValueMode::ABSOLUTE);
			const auto row = newHorizonsMagic::spellEffectPreview(hero,current);
			ASSERT_FALSE(row.labelKey.empty()) << key;
			EXPECT_EQ(row.primary.has_value(),!categorical.contains(key)) << key;
			EXPECT_EQ(row,newHorizonsMagic::spellEffectPreview(hero,current)) << key;
		}
		++covered;
	}
	EXPECT_EQ(covered,75);
	EXPECT_EQ(hero.getManaAvailable(),mana);
	EXPECT_EQ(hero.getSpellsInSpellbook(),known);
}
