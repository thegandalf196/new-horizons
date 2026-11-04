/* SpellPowerCompositionTest.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "../SpellPointTestUtils.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/SpellEffectPreview.h"
#include "../../lib/spells/NewHorizonsDirectDamage.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/spells/NewHorizonsPlacementPower.h"
#include "../../lib/spells/NewHorizonsElementalConvergencePower.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include <array>
#include <limits>

TEST(SpellPowerComposition, CoefficientEmpowerAndSpecialtyKeepOneFinalFloor)
{
	using spells::scaleSpellPowerComponentWithCoefficientBasisPoints;
	EXPECT_EQ(scaleSpellPowerComponentWithCoefficientBasisPoints(19, 10, 10000), 1);
	EXPECT_EQ(scaleSpellPowerComponentWithCoefficientBasisPoints(19, 10, 10000, 10), 2);
	EXPECT_EQ(scaleSpellPowerComponentWithCoefficientBasisPoints(20, 3, 14500, 25, 15), 13);
	EXPECT_EQ(scaleSpellPowerComponentWithCoefficientBasisPoints(38, 10, 10000), 3);
	const newHorizonsMagic::DirectDamageFormula formula{100, 20};
	EXPECT_EQ(formula.evaluateBasisPoints(0, 3, 14500, 25, 15), 100);
	EXPECT_EQ(formula.evaluateBasisPoints(1, 3, 14500, 25, 15), 113);
}

TEST(SpellPowerComposition, RetainedDomainsRejectInvalidInputs)
{
	using spells::scaleSpellPowerComponentWithCoefficientBasisPoints;
	EXPECT_THROW(scaleSpellPowerComponentWithCoefficientBasisPoints(-1, 1, 10000), std::invalid_argument);
	EXPECT_THROW(scaleSpellPowerComponentWithCoefficientBasisPoints(1, 0, 10000), std::invalid_argument);
	EXPECT_THROW(scaleSpellPowerComponentWithCoefficientBasisPoints(1, 1, -1), std::invalid_argument);
	EXPECT_THROW(scaleSpellPowerComponentWithCoefficientBasisPoints(1, 1, 100001), std::invalid_argument);
	EXPECT_THROW(scaleSpellPowerComponentWithCoefficientBasisPoints(1, 1, 10000, -1), std::invalid_argument);
	EXPECT_THROW(scaleSpellPowerComponentWithCoefficientBasisPoints(1, 1, 10000, 1001), std::invalid_argument);
	EXPECT_THROW(scaleSpellPowerComponentWithCoefficientBasisPoints(1, 1, 10000, 0, 14), std::invalid_argument);
}

TEST(SpellPowerComposition, LargeRationalIntermediatesDoNotNarrowBeforeTheResult)
{
	using spells::scaleSpellPowerComponentWithCoefficientBasisPoints;
	const auto maximum = std::numeric_limits<int64_t>::max();
	EXPECT_EQ(scaleSpellPowerComponentWithCoefficientBasisPoints(maximum, 1, 10000), maximum);
	EXPECT_THROW(scaleSpellPowerComponentWithCoefficientBasisPoints(maximum, 1, 10000, 1), std::overflow_error);
	const auto divisor = std::numeric_limits<int32_t>::max();
	const int64_t numerator = static_cast<int64_t>(divisor) * 2000000000LL - 1;
	EXPECT_EQ(scaleSpellPowerComponentWithCoefficientBasisPoints(numerator, divisor, 100000, 1000, 15), 252999999999LL);
}

class SpellPowerCompositionBattleTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepare(bool uniform = false)
	{
		startGame();
		if(uniform)
		{
			attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:neela")));
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
				MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			const auto wisdom = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
			attackerSideHero->setSecSkillLevel(wisdom, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.arcaneMomentum"});
			attackerSideHero->setSecSkillLevel(wisdom, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.arcaneInsight"});
			attackerSideHero->setSecSkillLevel(wisdom, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.arcaneInsight"));
		}
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 24, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->addSpellToSpellbook(SpellID::CURE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
		beginCombat();
		if(uniform)
		{
			SetMetamagicPointState points;
			points.battleID = BattleID(0);
			points.side = BattleSide::ATTACKER;
			points.round = battle()->getRound();
			points.state = battle()->getMetamagicPointState(points.side);
			points.state.points = 4;
			gameHandler->sendAndApply(points);
		}
	}
	int64_t value(SpellID id)
	{
		const auto * spell = id.toSpell();
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		return spell->battleMechanics(&cast)->getEffectValue();
	}
};

TEST_F(SpellPowerCompositionBattleTest, WarcastingRankAndPreviousOrderDoNotBoostSpellComponents)
{
	prepare();
	const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:warcasting"));
	ASSERT_GE(skill.getNum(), 0);
	const auto arrow = value(SpellID::MAGIC_ARROW);
	const auto cure = value(SpellID::CURE);
	for(const auto rank : {MasteryLevel::BASIC, MasteryLevel::ADVANCED, MasteryLevel::EXPERT})
	{
		attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(value(SpellID::MAGIC_ARROW), arrow);
		EXPECT_EQ(value(SpellID::CURE), cure);
	}
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();
	EXPECT_EQ(value(SpellID::MAGIC_ARROW), arrow);
	EXPECT_EQ(value(SpellID::CURE), cure);
	const auto mana = attackerSideHero->getManaAvailable();
	for(const auto id : {SpellID::MAGIC_ARROW, SpellID::CURE})
	{
		const auto * spell = SpellID(id).toSpell();
		const auto preview = newHorizonsMagic::spellEffectPreview(*attackerSideHero, *spell, battle());
		ASSERT_TRUE(preview.primary);
		EXPECT_EQ(preview.primary->value, spell->isDamage()
			? spell->adjustRawDamage(attackerSideHero, nullptr, value(id)) : value(id));
	}
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST(SpellPowerComposition, UniformDurationAddsCapturedBonusesOnceAndPreservesSentinels)
{
	newHorizonsMetamagic::CastProfile cast;
	cast.perks.rank = 3;
	cast.perks.pointPolicy = true;
	cast.perks.wisdomEffectivenessBonus = 25;
	cast.points = 4;
	EXPECT_EQ(newHorizonsMetamagic::targetFreeEffectivenessBonusPercent(&cast), 37);
	EXPECT_EQ(newHorizonsMetamagic::outgoingMagnitude(83, &cast, nullptr), 113);
	EXPECT_EQ(newHorizonsMetamagic::outgoingDuration(3, &cast, nullptr), 4);
	EXPECT_EQ(newHorizonsMetamagic::outgoingDuration(0, &cast, nullptr), 0);
	EXPECT_EQ(newHorizonsMetamagic::outgoingDuration(-1, &cast, nullptr), -1);
	EXPECT_EQ(newHorizonsMetamagic::outgoingDuration(3, nullptr, nullptr), 3);
}

TEST_F(SpellPowerCompositionBattleTest, SummonCardsMatchActualHpAndLivingArmorLifetimeWithoutSpendingAtPreview)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const std::array<const char *, 3> keys{"new-horizons:summonTrolls", "new-horizons:elementalConvergence",
		"new-horizons:summonLivingArmor"};
	const std::array<BattleHex, 3> positions{BattleHex{8, 3}, BattleHex{10, 4}, BattleHex{8, 6}};
	for(size_t index = 0; index < keys.size(); ++index)
	{
		SCOPED_TRACE(keys[index]);
		const auto id = SpellID(SpellID::decode(keys[index]));
		ASSERT_NE(id, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(id);
		const auto * spell = id.toSpell();
		const auto mana = attackerSideHero->getManaAvailable();
		const auto points = battle()->getMetamagicPointState(BattleSide::ATTACKER);
		CMemorySerializer rngBefore;
		rngBefore.oser & *gameHandler->randomizer;
		const auto preview = newHorizonsMagic::spellEffectPreview(*attackerSideHero, *spell, battle());
		ASSERT_TRUE(preview.primary);
		EXPECT_EQ(preview, newHorizonsMagic::spellEffectPreview(*attackerSideHero, *spell, battle()));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
		EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER), points);
		CMemorySerializer rngAfter;
		rngAfter.oser & *gameHandler->randomizer;
		EXPECT_EQ(rngBefore.extractBuffer(), rngAfter.extractBuffer());
		const auto unitsBefore = battle()->stacks.size();
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = id;
		action.aimToHex(positions[index]);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		ASSERT_EQ(battle()->stacks.size(), unitsBefore + 1);
		const auto & summoned = battle()->stacks.back();
		EXPECT_EQ(summoned->getAvailableHealth(), preview.primary->value);
		if(index == 2)
		{
			ASSERT_TRUE(preview.secondary);
			EXPECT_EQ(summoned->acquireState()->summonRoundsRemaining, preview.secondary->value);
		}
		advanceRound();
	}
}

TEST_F(SpellPowerCompositionBattleTest, PlacementCardsUseUniformOutputAndEarthquakeRetainsAuthoredSectionCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	for(const auto * key : {"new-horizons:verdantPrison", "new-horizons:transfigureMatter", "core:earthquake"})
	{
		const auto id = SpellID(SpellID::decode(key));
		const auto * spell = id.toSpell();
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		const auto preview = newHorizonsMagic::spellEffectPreview(*attackerSideHero, *spell, battle());
		ASSERT_TRUE(preview.primary);
		if(id == SpellID::EARTHQUAKE)
		{
			ASSERT_TRUE(preview.secondary);
			EXPECT_EQ(preview.secondary->value, mechanics->getNewHorizonsEarthquakeSectionCount());
			EXPECT_LE(preview.secondary->value, battle()->getMagicRules()["spells"][key]["earthquake"]["maxSections"].Integer());
		}
		else
		{
			const auto ordinary = std::string_view(key) == "new-horizons:verdantPrison"
				? newHorizonsMagic::verdantPrisonHitPoints(mechanics->getEffectPower(), mechanics->getSpellPowerCoefficientBasisPoints(), 0, false)
				: newHorizonsMagic::transfigureHitPoints(mechanics->getEffectPower(), mechanics->getSpellPowerCoefficientBasisPoints(), 0, 1, false);
			EXPECT_EQ(preview.primary->value, mechanics->adjustMetamagicMagnitude(ordinary, nullptr));
		}
	}
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 10000, ChangeValueMode::ABSOLUTE);
	const auto id = SpellID(SpellID::EARTHQUAKE);
	const auto preview = newHorizonsMagic::spellEffectPreview(*attackerSideHero, *id.toSpell(), battle());
	ASSERT_TRUE(preview.secondary);
	EXPECT_EQ(preview.secondary->value, battle()->getMagicRules()["spells"]["core:earthquake"]["earthquake"]["maxSections"].Integer());
}

TEST_F(SpellPowerCompositionBattleTest, OutOfBattleCardsIncludeStaticArchmageWithoutCombatInsightMomentumOrPoints)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	battle()->getSide(BattleSide::ATTACKER).wisdomCombatState.recordCompletedCast(SpellID::MAGIC_ARROW, battle()->getRound());
	const auto id = SpellID(SpellID::decode("new-horizons:elementalConvergence"));
	const auto * spell = id.toSpell();
	const auto & rules = attackerSideHero->getMagicRules();
	ASSERT_EQ(newHorizonsMagic::spellLevel(rules, id), 5);
	const auto combat = newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, battle(), true, id);
	ASSERT_EQ(newHorizonsMetamagic::targetFreeEffectivenessBonusPercent(&combat), 42);
	const auto raw = newHorizonsMagic::elementalConvergenceHitPoints(attackerSideHero->getEffectPower(spell),
		newHorizonsMagic::ordinarySpellPowerCoefficientBasisPoints(rules, attackerSideHero, id),
		newHorizonsMagic::empowerSpellBonusPercent(rules, attackerSideHero, id), false);
	const auto before = newHorizonsMagic::spellEffectPreview(*attackerSideHero, *spell);
	ASSERT_TRUE(before.primary);
	EXPECT_EQ(before.primary->value, raw);
	const auto armorID = SpellID(SpellID::decode("new-horizons:summonLivingArmor"));
	ASSERT_EQ(newHorizonsMagic::spellLevel(rules, armorID), 1);
	const auto armorBefore = newHorizonsMagic::spellEffectPreview(*attackerSideHero, *armorID.toSpell());
	attackerSideHero->applyPerkSelection({"new-horizons:wisdom", "new-horizons:wisdom.archmage"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.archmage"));
	const auto after = newHorizonsMagic::spellEffectPreview(*attackerSideHero, *spell);
	ASSERT_TRUE(after.primary);
	EXPECT_EQ(after.primary->value, raw * 120 / 100);
	EXPECT_EQ(newHorizonsMagic::spellEffectPreview(*attackerSideHero, *armorID.toSpell()), armorBefore);
	EXPECT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER).points, 4);
	EXPECT_EQ(battle()->getWisdomCombatState(BattleSide::ATTACKER).momentumBonusPercent(battle()->getRound()), 5);
}
