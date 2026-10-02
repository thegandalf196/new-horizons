/*
 * NewHorizonsSorrowTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"
#include "../../SpellPointTestUtils.h"

namespace
{
constexpr auto shadowMagic = "new-horizons:shadowMagic";

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
		if(spell.Struct().contains("variant"))
		{
			spell.Struct().erase("variant");
			spell["active"].Bool() = false;
		}
	}
	if(version < newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		// v1/v2 saved Sorrow was a Chaos spell with stock level/cost. Do not
		// derive legacy compatibility from the new v3 canonical row.
		auto & sorrow = rules["spells"]["core:sorrow"];
		sorrow.Struct().erase("level");
		sorrow.Struct().erase("costs");
		sorrow["schools"].Vector().clear();
		sorrow["schools"].Vector().emplace_back(std::string("new-horizons:chaos"));
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

class NewHorizonsSorrowTest : public HeroCommandFixture
{
protected:
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	CStack * target = nullptr;
	CStack * secondTarget = nullptr;
	CStack * friendlyTarget = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		const JsonNode rules = magicVersion == newHorizonsMagic::CURRENT_RULESET_VERSION
			? JsonNode(JsonPath::builtin("config/newHorizonsMagic")) : legacyMagicRules(magicVersion);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(int spellPower, int shadowRank)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
		for(const SpellID known : knownSpells)
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(SpellID::SORROW);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		const auto castingSchoolSkill = SecondarySkill::decode(magicVersion == newHorizonsMagic::CURRENT_RULESET_VERSION
			? shadowMagic : "new-horizons:chaosMagic");
		attackerSideHero->setSecSkillLevel(SecondarySkill(castingSchoolSkill), shadowRank,
			ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);

		startBattle();
		friendlyTarget = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 100);
		secondTarget = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(14, 5), 100);
		beginCombat();
		ASSERT_NE(friendlyTarget, nullptr);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(secondTarget, nullptr);
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	void selectMalediction()
	{
		const auto skill = SecondarySkill(SecondarySkill::decode(shadowMagic));
		if(attackerSideHero->getSecSkillLevel(skill) < MasteryLevel::BASIC)
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({shadowMagic, "new-horizons:shadowMagic.malediction"});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(shadowMagic, "new-horizons:shadowMagic.malediction"));
	}

	bool castSorrow(CStack * unit)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::SORROW;
		action.aimToUnit(unit);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
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

TEST_F(NewHorizonsSorrowTest, NewGamesRegisterLevelOneShadowSorrowForFourMana)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	ASSERT_TRUE(newHorizonsMagic::sorrowRulesEnabled(rules, SpellID::SORROW));
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, SpellID::SORROW), 1);
	const auto schools = newHorizonsMagic::spellSchools(rules, SpellID::SORROW);
	ASSERT_EQ(schools.size(), 1u);
	EXPECT_EQ(SpellSchool::encode(schools.front().getNum()), "shadow");
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, SpellID::SORROW, rank), 4);
}

TEST_F(NewHorizonsSorrowTest, SavedV3PenaltyUsesTheRankCoefficientBeforeOneFinalFloor)
{
	prepare(100, MasteryLevel::NONE);
	const JsonNode & rules = battle()->getBattle()->getMagicRules();
	const SecondarySkill shadow(SecondarySkill::decode(shadowMagic));
	constexpr std::array<int, 4> coefficients{100, 115, 130, 145};
	constexpr std::array<int, 4> expectedAtSixtyOne{1, 2, 2, 2};
	constexpr std::array<int, 4> expectedAtOneHundred{2, 2, 2, 3};
	for(int rank = 0; rank < 4; ++rank)
	{
		attackerSideHero->setSecSkillLevel(shadow, rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientPercent(rules, attackerSideHero, SpellID::SORROW),
			coefficients[rank]);
		EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 61),
			expectedAtSixtyOne[rank]);
		EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 100),
			expectedAtOneHundred[rank]);
	}
	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 60), 1);
	EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 0), 1);
	EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 69), 1);
	EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 70), 2);
	EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 139), 2);
	EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 140), 3);
	const SecondarySkill spellcraft(SecondarySkill::decode("new-horizons:spellcraft"));
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 65), 2)
		<< "Spellcraft scales raw Spell Power across the 70-point threshold";
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 60, 20), 2)
		<< "Warcasting scales the Spell Power term before the final floor";
	EXPECT_EQ(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 60, 0, 20), 2)
		<< "Empower scales the Spell Power term before the final floor";
	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	const auto description = newHorizonsMagic::spellDescriptionForHero(
		attackerSideHero, SpellID(SpellID::SORROW).toSpell(), MasteryLevel::EXPERT);
	EXPECT_NE(description.find("one enemy stack for 3 rounds"), std::string::npos);
	EXPECT_NE(description.find("floor(scaled raw Hero Spell Power / 70)"), std::string::npos);
	EXPECT_NE(description.find("single-target at every rank"), std::string::npos);
	EXPECT_NE(description.find("100% / 115% / 130% / 145%"), std::string::npos);
	const auto spell = SpellID(SpellID::SORROW).toSpell();
	const auto tooltipPenalty = newHorizonsMagic::sorrowMoralePenalty(attackerSideHero->getMagicRules(),
		attackerSideHero, SpellID::SORROW, attackerSideHero->getEffectPower(spell));
	ASSERT_TRUE(tooltipPenalty.has_value());
	EXPECT_NE(description.find("ordinary penalty is -" + std::to_string(*tooltipPenalty) + " Morale"),
		std::string::npos);
}

TEST_F(NewHorizonsSorrowTest, MalformedSavedV3RosterRowDoesNotOptIntoSorrow)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["spells"]["core:sorrow"]["level"] = JsonNode("one");
	EXPECT_FALSE(newHorizonsMagic::sorrowRulesEnabled(rules, SpellID::SORROW));
	EXPECT_FALSE(newHorizonsMagic::expertRangeIsSingleTarget(rules, SpellID::SORROW));
	EXPECT_FALSE(newHorizonsMagic::sorrowMoralePenalty(rules, nullptr, SpellID::SORROW, 70).has_value());

	JsonNode scalarRowRules(JsonPath::builtin("config/newHorizonsMagic"));
	scalarRowRules["spells"]["core:sorrow"] = JsonNode("malformed row");
	EXPECT_NO_THROW(newHorizonsMagic::sorrowRulesEnabled(scalarRowRules, SpellID::SORROW));
	EXPECT_FALSE(newHorizonsMagic::sorrowRulesEnabled(scalarRowRules, SpellID::SORROW));

	JsonNode malformedActiveRules(JsonPath::builtin("config/newHorizonsMagic"));
	malformedActiveRules["spells"]["core:sorrow"]["active"] = JsonNode("yes");
	EXPECT_NO_THROW(newHorizonsMagic::sorrowRulesEnabled(malformedActiveRules, SpellID::SORROW));
	EXPECT_FALSE(newHorizonsMagic::sorrowRulesEnabled(malformedActiveRules, SpellID::SORROW));
}

TEST_F(NewHorizonsSorrowTest, V3ExpertCastAppliesTheRankedPenaltyForThreeRoundsToOneStack)
{
	prepare(100, MasteryLevel::EXPERT);
	const auto * spell = SpellID(SpellID::SORROW).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&parameters);
	ASSERT_EQ(mechanics->getRangeLevel(), MasteryLevel::ADVANCED);
	EXPECT_TRUE(mechanics->usesNewHorizonsMagicV3());
	EXPECT_TRUE(newHorizonsMagic::sorrowRulesEnabled(battle()->getBattle()->getMagicRules(), SpellID::SORROW));
	EXPECT_EQ(mechanics->getEffectPower(), 100);
	EXPECT_EQ(mechanics->getEffectPowerDivisor(), 10)
		<< "Sorrow uses raw Hero Spell Power even when primary growth exposes a divisor";
	EXPECT_FALSE(mechanics->isMassive());
	const spells::Target friendlyAim{spells::Destination(friendlyTarget)};
	const spells::Target enemyAim{spells::Destination(target)};
	spells::detail::ProblemImpl problem;
	EXPECT_FALSE(mechanics->canBeCastAt(friendlyAim, problem));
	EXPECT_TRUE(mechanics->getAffectedStacks(friendlyAim).empty());
	const auto affected = mechanics->getAffectedStacks(enemyAim);
	ASSERT_EQ(affected.size(), 1u);
	EXPECT_EQ(affected.front(), target);
	const auto expectedPenalty = newHorizonsMagic::sorrowMoralePenalty(
		battle()->getBattle()->getMagicRules(), attackerSideHero, SpellID::SORROW,
		mechanics->getEffectPower(), mechanics->getWarcastingBonusPercent(),
		mechanics->getEmpowerSpellBonusPercent());
	ASSERT_TRUE(expectedPenalty.has_value());
	EXPECT_EQ(*expectedPenalty, 3);

	EXPECT_FALSE(castSorrow(friendlyTarget));
	EXPECT_EQ(sorrowMorale(friendlyTarget), nullptr);
	ASSERT_TRUE(castSorrow(target));
	const auto * applied = sorrowMorale(target);
	ASSERT_NE(applied, nullptr);
	EXPECT_EQ(applied->val, -*expectedPenalty);
	EXPECT_EQ(applied->turnsRemain, newHorizonsMagic::SORROW_BASE_DURATION_ROUNDS);
	EXPECT_EQ(sorrowMorale(secondTarget), nullptr)
		<< "Expert range remains single-target without the separate Mass perk";
	EXPECT_EQ(sorrowMorale(friendlyTarget), nullptr);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Sorrow reduces") != std::string::npos
			&& line.find("for 3 rounds") != std::string::npos;
	}));
}

TEST_F(NewHorizonsSorrowTest, StrongerRecastReplacesPenaltyWithoutStackingAndRefreshesThreeRoundDuration)
{
	prepare(10, MasteryLevel::EXPERT);
	ASSERT_TRUE(castSorrow(target));
	ASSERT_NE(sorrowMorale(target), nullptr);
	EXPECT_EQ(sorrowMorale(target)->val, -1);

	// The setup-to-round-one transition skips duration aging. Two later round
	// transitions leave two rounds, which a recast refreshes back to three.
	while(battle()->getRound() == 0)
		advanceRound();
	ASSERT_NE(sorrowMorale(target), nullptr);
	EXPECT_EQ(sorrowMorale(target)->turnsRemain, 3);
	advanceRound();
	ASSERT_NE(sorrowMorale(target), nullptr);
	EXPECT_EQ(sorrowMorale(target)->turnsRemain, 2);

	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1000, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(castSorrow(target));
	const auto currentSorrow = target->getBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SORROW)))
		.And(Selector::type()(BonusType::MORALE)));
	ASSERT_NE(currentSorrow, nullptr);
	ASSERT_EQ(currentSorrow->size(), 1u);
	ASSERT_NE(sorrowMorale(target), nullptr);
	EXPECT_EQ(sorrowMorale(target)->val, -3);
	EXPECT_EQ(sorrowMorale(target)->turnsRemain, 3);

	for(int remaining = 2; remaining >= 0; --remaining)
	{
		advanceRound();
		if(remaining == 0)
			EXPECT_EQ(sorrowMorale(target), nullptr);
		else
		{
			ASSERT_NE(sorrowMorale(target), nullptr);
			EXPECT_EQ(sorrowMorale(target)->turnsRemain, remaining);
		}
	}
}

TEST_F(NewHorizonsSorrowTest, MaledictionExtendsSorrowAndRefreshesOneNonstackingMoraleEffect)
{
	prepare(10, MasteryLevel::BASIC);
	selectMalediction();
	const auto & rules = battle()->getBattle()->getMagicRules();
	const auto duration = newHorizonsMagic::sorrowDurationRounds(rules, attackerSideHero, SpellID::SORROW);
	ASSERT_TRUE(duration.has_value());
	EXPECT_EQ(*duration, 4);

	const auto description = newHorizonsMagic::spellDescriptionForHero(
		attackerSideHero, SpellID(SpellID::SORROW).toSpell(), MasteryLevel::BASIC);
	EXPECT_NE(description.find("for 4 rounds"), std::string::npos);
	EXPECT_NE(description.find("Malediction extends the duration by one round"), std::string::npos);

	ASSERT_TRUE(castSorrow(target));
	ASSERT_NE(sorrowMorale(target), nullptr);
	EXPECT_EQ(sorrowMorale(target)->turnsRemain, 4);

	// The setup-to-round-one transition does not age effects. The next round
	// leaves three rounds, which the recast must replace and refresh to four.
	while(battle()->getRound() == 0)
		advanceRound();
	advanceRound();
	ASSERT_NE(sorrowMorale(target), nullptr);
	EXPECT_EQ(sorrowMorale(target)->turnsRemain, 3);
	ASSERT_TRUE(castSorrow(target));
	const auto currentSorrow = target->getBonuses(Selector::source(
		BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SORROW)))
		.And(Selector::type()(BonusType::MORALE)));
	ASSERT_NE(currentSorrow, nullptr);
	ASSERT_EQ(currentSorrow->size(), 1u);
	ASSERT_NE(sorrowMorale(target), nullptr);
	EXPECT_EQ(sorrowMorale(target)->turnsRemain, 4);

	for(int remaining = 3; remaining >= 0; --remaining)
	{
		advanceRound();
		if(remaining == 0)
			EXPECT_EQ(sorrowMorale(target), nullptr);
		else
		{
			ASSERT_NE(sorrowMorale(target), nullptr);
			EXPECT_EQ(sorrowMorale(target)->turnsRemain, remaining);
		}
	}
}

TEST_F(NewHorizonsSorrowTest, LegacyV2ExpertCastKeepsTheConfiguredMassAndTierPenalty)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	prepare(20, MasteryLevel::EXPERT);
	const auto & rules = battle()->getBattle()->getMagicRules();
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_FALSE(newHorizonsMagic::sorrowRulesEnabled(rules, SpellID::SORROW));
	EXPECT_FALSE(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 20).has_value());
	selectMalediction();
	EXPECT_FALSE(newHorizonsMagic::sorrowDurationRounds(rules, attackerSideHero, SpellID::SORROW).has_value());
	const auto * spell = SpellID(SpellID::SORROW).toSpell();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const int legacyDuration = spell->battleMechanics(&cast)->getEffectDuration();

	ASSERT_TRUE(castSorrow(target));
	const auto * applied = sorrowMorale(target);
	ASSERT_NE(applied, nullptr);
	EXPECT_EQ(applied->val, -2);
	EXPECT_NE(sorrowMorale(secondTarget), nullptr)
		<< "the saved v2 Expert spell retains its inherited Mass range";
	EXPECT_EQ(applied->turnsRemain, legacyDuration)
		<< "Malediction does not alter the v2 caster's inherited enchant duration";
}

TEST_F(NewHorizonsSorrowTest, HistoricalV1CastKeepsTheConfiguredMassAndTierPenalty)
{
	magicVersion = newHorizonsMagic::RULESET_VERSION;
	prepare(20, MasteryLevel::EXPERT);
	const auto & rules = battle()->getBattle()->getMagicRules();
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_FALSE(newHorizonsMagic::sorrowRulesEnabled(rules, SpellID::SORROW));
	EXPECT_FALSE(newHorizonsMagic::sorrowMoralePenalty(rules, attackerSideHero, SpellID::SORROW, 140).has_value());
	EXPECT_FALSE(newHorizonsMagic::expertRangeIsSingleTarget(rules, SpellID::SORROW));
	selectMalediction();
	EXPECT_FALSE(newHorizonsMagic::sorrowDurationRounds(rules, attackerSideHero, SpellID::SORROW).has_value());
	const auto * spell = SpellID(SpellID::SORROW).toSpell();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const int legacyDuration = spell->battleMechanics(&cast)->getEffectDuration();

	ASSERT_TRUE(castSorrow(target));
	const auto * applied = sorrowMorale(target);
	ASSERT_NE(applied, nullptr);
	EXPECT_EQ(applied->val, -2);
	EXPECT_NE(sorrowMorale(secondTarget), nullptr);
	EXPECT_EQ(applied->turnsRemain, legacyDuration)
		<< "Malediction does not alter the v1 caster's inherited enchant duration";
}
