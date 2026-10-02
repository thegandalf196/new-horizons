/*
 * NewHorizonsBlessTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/battle/AlternatingHeroActionState.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto lightMagicSkill = "new-horizons:lightMagic";
constexpr auto benedictionPerk = "new-horizons:lightMagic.benediction";
constexpr auto shadowMagicSkill = "new-horizons:shadowMagic";
constexpr auto maledictionPerk = "new-horizons:shadowMagic.malediction";

JsonNode legacyMagicRules(int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = version;
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
	if(version == newHorizonsMagic::RULESET_VERSION)
	{
		rules.Struct().erase("spellPoints");
		rules.Struct().erase("mageGuildGeneration");
		rules.Struct().erase("physicalDamageReductionCapPercent");
		rules.Struct().erase("warcasting");
		auto & spells = rules["spells"].Struct();
		for(auto it = spells.begin(); it != spells.end();)
		{
			if(it->first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':'))
				it = spells.erase(it);
			else
				++it;
		}
		for(auto & [factionId, faction] : rules["factions"].Struct())
		{
			(void)factionId;
			faction["major"] = faction["preferredA"];
			faction["minor"] = faction["preferredB"];
			faction.Struct().erase("preferredA");
			faction.Struct().erase("preferredB");
		}
		for(auto & [name, spell] : rules["spells"].Struct())
		{
			(void)name;
			spell.Struct().erase("active");
			spell.Struct().erase("directDamage");
			spell.Struct().erase("cureAfflictions");
		}
	}
	return rules;
}

class NewHorizonsBlessTest : public HeroCommandFixture
{
protected:
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	const CSpell * bless = nullptr;
	const CSpell * curse = nullptr;
	CStack * target = nullptr;
	CStack * otherFriendly = nullptr;
	CStack * hostileTarget = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		JsonNode rules = magicVersion == newHorizonsMagic::CURRENT_RULESET_VERSION
			? JsonNode(JsonPath::builtin("config/newHorizonsMagic")) : legacyMagicRules(magicVersion);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(int spellPower, int lightRank, bool selectBenediction = false)
	{
		startGame();
		bless = SpellID(SpellID::BLESS).toSpell();
		curse = SpellID(SpellID::CURSE).toSpell();
		ASSERT_NE(bless, nullptr);
		ASSERT_NE(curse, nullptr);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const SpellID known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(bless->getId());
		attackerSideHero->addSpellToSpellbook(curse->getId());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(
			SecondarySkill(SecondarySkill::decode(lightMagicSkill)), lightRank, ChangeValueMode::ABSOLUTE);
		if(selectBenediction)
			attackerSideHero->applyPerkSelection({lightMagicSkill, benedictionPerk});
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 100);
		otherFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(6, 5), 100);
		hostileTarget = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
		beginCombat();
		ASSERT_NE(target, nullptr);
		ASSERT_NE(otherFriendly, nullptr);
		ASSERT_NE(hostileTarget, nullptr);
	}

	int ordinaryDuration()
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, bless);
		return bless->battleMechanics(&cast)->getEffectDuration();
	}

	int durationWithExplicitOverride(int duration)
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, bless);
		cast.setEffectDuration(duration);
		return bless->battleMechanics(&cast)->getEffectDuration();
	}

	int appliedDuration(const CStack * unit) const
	{
		const auto bonuses = unit->getAllBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS))));
		for(const auto & bonus : *bonuses)
			if(bonus->type == BonusType::ALWAYS_MAXIMUM_DAMAGE)
				return bonus->turnsRemain;
		return 0;
	}

	int appliedCurseDuration(const CStack * unit) const
	{
		const auto bonuses = unit->getAllBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::CURSE))));
		for(const auto & bonus : *bonuses)
			if(bonus->type == BonusType::ALWAYS_MINIMUM_DAMAGE)
				return bonus->turnsRemain;
		return 0;
	}

	std::optional<int32_t> appliedEndpointValue(const CStack * unit, SpellID spell, BonusType type) const
	{
		const auto bonuses = unit->getAllBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
		for(const auto & bonus : *bonuses)
			if(bonus->type == type)
				return bonus->val;
		return std::nullopt;
	}

	void castOn(SpellID spell, CStack * unit)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.aimToUnit(unit);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	}

	void setCurseExpert()
	{
		attackerSideHero->setSecSkillLevel(
			SecondarySkill(SecondarySkill::decode(shadowMagicSkill)),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	}

	void selectMalediction()
	{
		const auto skill = SecondarySkill(SecondarySkill::decode(shadowMagicSkill));
		if(attackerSideHero->getSecSkillLevel(skill) < MasteryLevel::BASIC)
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({shadowMagicSkill, maledictionPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(shadowMagicSkill, maledictionPerk));
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	void addDurationBonus(int value, BonusSubtypeID subtype)
	{
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::SPELL_DURATION, BonusSource::OTHER, value, BonusSourceID(), subtype));
	}
};
}

TEST_F(NewHorizonsBlessTest, V3LightRanksUseTheSavedCoefficientAtTheirSpellPowerThresholds)
{
	constexpr std::array<std::tuple<int, int>, 4> ranks{{
		{MasteryLevel::NONE, 100},
		{MasteryLevel::BASIC, 115},
		{MasteryLevel::ADVANCED, 130},
		{MasteryLevel::EXPERT, 145},
	}};
	prepare(1, MasteryLevel::NONE);
	const int64_t thresholdNumerator = static_cast<int64_t>(
		newHorizonsMagic::BLESS_SPELL_POWER_DURATION_DIVISOR) * 100;
	const auto light = SecondarySkill(SecondarySkill::decode(lightMagicSkill));
	for(const auto & [rank, coefficient] : ranks)
	{
		attackerSideHero->setSecSkillLevel(light, rank, ChangeValueMode::ABSOLUTE);
		const int threshold = static_cast<int>((thresholdNumerator + coefficient - 1) / coefficient);
		EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientPercent(
			battle()->getMagicRules(), attackerSideHero, bless->getId()), coefficient);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, threshold - 1, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(ordinaryDuration(), newHorizonsMagic::BLESS_BASE_DURATION)
			<< "rank " << static_cast<int>(rank) << " remains below its first duration threshold";
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, threshold, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(ordinaryDuration(), newHorizonsMagic::BLESS_BASE_DURATION + 1)
			<< "rank " << static_cast<int>(rank) << " crosses its first duration threshold";
	}

	attackerSideHero->setSecSkillLevel(light, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const int expertCoefficient = newHorizonsMagic::spellPowerCoefficientPercent(
		battle()->getMagicRules(), attackerSideHero, bless->getId());
	ASSERT_EQ(expertCoefficient, 145);
	const int capThreshold = static_cast<int>((2 * thresholdNumerator + expertCoefficient - 1) / expertCoefficient);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, capThreshold, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(ordinaryDuration(), newHorizonsMagic::BLESS_MAX_DURATION)
		<< "school-scaled duration is still capped at four before bonuses";
}

TEST_F(NewHorizonsBlessTest, DurationBonusesAndBenedictionApplyAfterTheOrdinaryCapAndOverridesStayExplicit)
{
	prepare(160, MasteryLevel::BASIC, true);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(lightMagicSkill, benedictionPerk));
	addDurationBonus(1, BonusSubtypeID());
	addDurationBonus(2, BonusSubtypeID(SpellID(SpellID::BLESS)));
	EXPECT_EQ(ordinaryDuration(), 8)
		<< "four capped rounds plus common/specific SPELL_DURATION and Benediction";
	EXPECT_EQ(durationWithExplicitOverride(11), 11)
		<< "an explicit BattleCast duration remains authoritative";

	const auto description = newHorizonsMagic::spellDescriptionForHero(attackerSideHero, bless, 0);
	EXPECT_NE(description.find("Current ordinary duration: 4 rounds"), std::string::npos);
	EXPECT_NE(description.find("combined Spell Power coefficient: 115%"), std::string::npos);
	EXPECT_NE(description.find("Current total before battle-only adjustments: 8 rounds"), std::string::npos);
	EXPECT_NE(description.find("Benediction adds 1 round"), std::string::npos);
}

TEST_F(NewHorizonsBlessTest, EligibleWarcastingScalesTheSpellPowerDurationTerm)
{
	prepare(79, MasteryLevel::NONE);
	const int32_t round = battle()->battleGetRound();
	battle()->getSide(BattleSide::ATTACKER).warcastingState = {
		AlternatingHeroActionState::Action::SPELL, 20, round};
	EXPECT_EQ(ordinaryDuration(), newHorizonsMagic::BLESS_BASE_DURATION + 1)
		<< "the eligible +20% Warcasting bonus crosses the 80 Spell Power threshold";

	battle()->getSide(BattleSide::ATTACKER).warcastingState = AlternatingHeroActionState{};
	EXPECT_EQ(ordinaryDuration(), newHorizonsMagic::BLESS_BASE_DURATION)
		<< "the same Spell Power is below the threshold without Warcasting";
}

TEST_F(NewHorizonsBlessTest, ExpertV3BlessRemainsSingleTargetInTheActualCast)
{
	prepare(80, MasteryLevel::EXPERT);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, bless);
	const auto mechanics = bless->battleMechanics(&cast);
	EXPECT_EQ(mechanics->getRangeLevel(), 2);
	EXPECT_EQ(mechanics->getEffectLevel(), 3)
		<< "saved v3 targeting does not reduce Expert effect mastery";
	EXPECT_FALSE(mechanics->isMassive());

	spells::BattleCast explicitMassCast(battle(), attackerSideHero, spells::Mode::HERO, bless);
	explicitMassCast.forceMassive = true;
	const auto explicitMassMechanics = bless->battleMechanics(&explicitMassCast);
	EXPECT_EQ(explicitMassMechanics->getRangeLevel(), 3);
	EXPECT_EQ(explicitMassMechanics->getEffectLevel(), 3);
	EXPECT_TRUE(explicitMassMechanics->isMassive())
		<< "an explicit perk/event Mass override remains authoritative in v3";

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::BLESS;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(appliedDuration(target), 3);
	EXPECT_EQ(appliedDuration(otherFriendly), 0)
		<< "Expert school rank does not silently turn this single-target cast into Mass Bless";
	EXPECT_EQ(appliedEndpointValue(target, SpellID(SpellID::BLESS), BonusType::ALWAYS_MAXIMUM_DAMAGE), 0);
}

TEST_F(NewHorizonsBlessTest, V3ExpertCurseUsesTheNaturalMinimumWithoutLoweringEffectMastery)
{
	prepare(80, MasteryLevel::NONE);
	setCurseExpert();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, curse);
	const auto mechanics = curse->battleMechanics(&cast);
	EXPECT_EQ(mechanics->getEffectLevel(), MasteryLevel::EXPERT);
	castOn(SpellID::CURSE, hostileTarget);
	EXPECT_EQ(appliedEndpointValue(hostileTarget, SpellID(SpellID::CURSE), BonusType::ALWAYS_MINIMUM_DAMAGE), 0);
	EXPECT_EQ(appliedCurseDuration(hostileTarget), newHorizonsMagic::CURSE_BASE_DURATION_ROUNDS);
}

TEST_F(NewHorizonsBlessTest, V3MaledictionExtendsCurseAndRefreshesOneTimedEffect)
{
	prepare(80, MasteryLevel::NONE);
	selectMalediction();
	const auto & rules = battle()->getBattle()->getMagicRules();
	const auto duration = newHorizonsMagic::curseDurationRounds(rules, attackerSideHero, SpellID::CURSE);
	ASSERT_TRUE(duration.has_value());
	EXPECT_EQ(*duration, 4);

	const auto description = newHorizonsMagic::spellDescriptionForHero(
		attackerSideHero, SpellID(SpellID::CURSE).toSpell(), MasteryLevel::BASIC);
	EXPECT_NE(description.find("for 4 rounds"), std::string::npos);
	EXPECT_NE(description.find("Malediction extends the duration by one round"), std::string::npos);

	castOn(SpellID::CURSE, hostileTarget);
	EXPECT_EQ(appliedCurseDuration(hostileTarget), 4);
	EXPECT_EQ(appliedEndpointValue(hostileTarget, SpellID(SpellID::CURSE), BonusType::ALWAYS_MINIMUM_DAMAGE), 0);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Curse forces") != std::string::npos && line.find("for 4 rounds") != std::string::npos;
	}));

	while(battle()->getRound() == 0)
		advanceRound();
	advanceRound();
	EXPECT_EQ(appliedCurseDuration(hostileTarget), 3);
	castOn(SpellID::CURSE, hostileTarget);

	const auto currentCurse = hostileTarget->getBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::CURSE)))
		.And(Selector::type()(BonusType::ALWAYS_MINIMUM_DAMAGE)));
	ASSERT_NE(currentCurse, nullptr);
	ASSERT_EQ(currentCurse->size(), 1u);
	EXPECT_EQ(appliedCurseDuration(hostileTarget), 4);
}

TEST_F(NewHorizonsBlessTest, V2RetainsVanillaExpertMassTargetAndExpertEffect)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare(80, MasteryLevel::EXPERT));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, bless);
	const auto mechanics = bless->battleMechanics(&cast);
	EXPECT_EQ(mechanics->getRangeLevel(), 3);
	EXPECT_EQ(mechanics->getEffectLevel(), 3);
	EXPECT_TRUE(mechanics->isMassive())
		<< "an older saved ruleset keeps the core Expert Mass target shape";
	castOn(SpellID::BLESS, target);
	EXPECT_EQ(appliedEndpointValue(target, SpellID(SpellID::BLESS), BonusType::ALWAYS_MAXIMUM_DAMAGE), 1);
}

TEST_F(NewHorizonsBlessTest, V2RetainsTheExpertCurseMinusOneBonus)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare(80, MasteryLevel::NONE));
	setCurseExpert();
	selectMalediction();
	const auto & rules = battle()->getBattle()->getMagicRules();
	EXPECT_FALSE(newHorizonsMagic::curseDurationRounds(rules, attackerSideHero, SpellID::CURSE).has_value());
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, curse);
	const int legacyDuration = curse->battleMechanics(&cast)->getEffectDuration();
	castOn(SpellID::CURSE, hostileTarget);
	EXPECT_EQ(appliedEndpointValue(hostileTarget, SpellID(SpellID::CURSE), BonusType::ALWAYS_MINIMUM_DAMAGE), 1);
	EXPECT_EQ(appliedCurseDuration(hostileTarget), legacyDuration);
}

TEST_F(NewHorizonsBlessTest, V1RetainsTheExpertBlessPlusOneBonus)
{
	magicVersion = newHorizonsMagic::RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare(80, MasteryLevel::EXPERT));
	castOn(SpellID::BLESS, target);
	EXPECT_EQ(appliedEndpointValue(target, SpellID(SpellID::BLESS), BonusType::ALWAYS_MAXIMUM_DAMAGE), 1);
}

TEST_F(NewHorizonsBlessTest, V1RetainsTheExpertCurseMinusOneBonus)
{
	magicVersion = newHorizonsMagic::RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare(80, MasteryLevel::NONE));
	setCurseExpert();
	selectMalediction();
	const auto & rules = battle()->getBattle()->getMagicRules();
	EXPECT_FALSE(newHorizonsMagic::curseDurationRounds(rules, attackerSideHero, SpellID::CURSE).has_value());
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, curse);
	const int legacyDuration = curse->battleMechanics(&cast)->getEffectDuration();
	castOn(SpellID::CURSE, hostileTarget);
	EXPECT_EQ(appliedEndpointValue(hostileTarget, SpellID(SpellID::CURSE), BonusType::ALWAYS_MINIMUM_DAMAGE), 1);
	EXPECT_EQ(appliedCurseDuration(hostileTarget), legacyDuration);
}

TEST_F(NewHorizonsBlessTest, V1KeepsLegacyEnchantDuration)
{
	magicVersion = newHorizonsMagic::RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare(1600, MasteryLevel::EXPERT));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, bless);
	const auto mechanics = bless->battleMechanics(&cast);
	EXPECT_EQ(mechanics->getEffectDuration(), attackerSideHero->getEnchantPower(bless));
}

TEST_F(NewHorizonsBlessTest, V2KeepsLegacyEnchantDuration)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare(1600, MasteryLevel::EXPERT));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, bless);
	const auto mechanics = bless->battleMechanics(&cast);
	EXPECT_EQ(mechanics->getEffectDuration(), attackerSideHero->getEnchantPower(bless));
}
