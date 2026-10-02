/*
 * NewHorizonsDoomTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/networkPacks/SetStackEffect.h"

namespace
{
constexpr std::string_view SHADOW_MAGIC = "new-horizons:shadowMagic";
constexpr std::string_view SPELLCRAFT = "new-horizons:spellcraft";

SpellID doomSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsMagic::SHADOW_DOOM_SPELL)));
}

JsonNode savedV2RulesContainingDoom()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [name, row] : rules["spells"].Struct())
	{
		(void)name;
		row.Struct().erase("selectedPlacement");
		if(row.Struct().contains("variant"))
		{
			row.Struct().erase("variant");
			row["active"].Bool() = false;
		}
	}
	newHorizonsMagic::validateRules(rules);
	return rules;
}

class NewHorizonsDoomTest : public HeroCommandFixture
{
protected:
	bool useSavedV2Rules = false;
	CStack * target = nullptr;
	CStack * secondTarget = nullptr;
	CStack * friendly = nullptr;
	const CSpell * spell = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(doomSpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(useSavedV2Rules)
			rules = savedV2RulesContainingDoom();
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int spellPower = 100, MasteryLevel::Type shadowRank = MasteryLevel::NONE,
		MasteryLevel::Type spellcraftRank = MasteryLevel::NONE)
	{
		startGame();
		spell = doomSpell().toSpell();
		ASSERT_NE(spell, nullptr);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(doomSpell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(SHADOW_MAGIC))),
			shadowRank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(SPELLCRAFT))),
			spellcraftRank, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 100);
		secondTarget = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(14, 5), 100);
		beginCombat();
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(secondTarget, nullptr);
	}

	void giveExplicitInitiative(CStack * unit, int initiative)
	{
		Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, initiative, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER);
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{bonus});
		gameHandler->sendAndApply(effect);
	}

	bool castDoom(const CStack * unit)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = doomSpell();
		action.aimToUnit(unit);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	TConstBonusListPtr doomBonuses(const CStack * unit) const
	{
		return unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(doomSpell())));
	}

	const Bonus * doomBonus(const CStack * unit, BonusType type) const
	{
		const auto bonuses = doomBonuses(unit);
		if(!bonuses)
			return nullptr;
		for(const auto & bonus : *bonuses)
			if(bonus && bonus->type == type)
				return bonus.get();
		return nullptr;
	}

	void expectTimedDoomBonuses(const CStack * unit, int turns, size_t expectedCount) const
	{
		const auto bonuses = doomBonuses(unit);
		ASSERT_NE(bonuses, nullptr);
		ASSERT_EQ(bonuses->size(), expectedCount);
		for(const auto & bonus : *bonuses)
		{
			ASSERT_NE(bonus, nullptr);
			EXPECT_EQ(bonus->duration, BonusDuration::N_TURNS);
			EXPECT_EQ(bonus->turnsRemain, turns);
			EXPECT_EQ(bonus->source, BonusSource::SPELL_EFFECT);
			EXPECT_EQ(bonus->sid, BonusSourceID(doomSpell()));
		}
	}
};
}

TEST_F(NewHorizonsDoomTest, SavedV3FormulaScalesOnlyTheSpellPowerTermAndCapsAtSixty)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, MasteryLevel::NONE));
	const auto & rules = battle()->getBattle()->getMagicRules();
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	ASSERT_TRUE(newHorizonsMagic::doomRulesEnabled(rules, doomSpell()));
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, doomSpell()), 5);
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, doomSpell(), rank), 25);

	const auto coefficient = [&](int shadowRank, int spellcraftRank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(SHADOW_MAGIC))),
			shadowRank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(SPELLCRAFT))),
			spellcraftRank, ChangeValueMode::ABSOLUTE);
		return newHorizonsMagic::doomCripplingPenaltyPercent(rules, attackerSideHero, doomSpell(), 100);
	};
	EXPECT_EQ(coefficient(MasteryLevel::NONE, MasteryLevel::NONE), 50);
	EXPECT_EQ(coefficient(MasteryLevel::BASIC, MasteryLevel::NONE), 52)
		<< "The fixed 35-point base is not scaled with Shadow mastery";
	EXPECT_EQ(coefficient(MasteryLevel::EXPERT, MasteryLevel::NONE), 56);
	EXPECT_EQ(coefficient(MasteryLevel::EXPERT, MasteryLevel::EXPERT), 60)
		<< "School and Spellcraft scale only the power term before the cap";
	EXPECT_EQ(newHorizonsMagic::doomCripplingPenaltyPercent(rules, attackerSideHero, doomSpell(), 0), 35);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(SHADOW_MAGIC))),
		MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(SPELLCRAFT))),
		MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);

	const auto healthBefore = target->getAvailableHealth();
	ASSERT_TRUE(castDoom(target));
	EXPECT_EQ(target->getAvailableHealth(), healthBefore) << "Doom has no direct-damage component";
	ASSERT_NE(doomBonus(target, BonusType::GENERAL_ATTACK_REDUCTION), nullptr);
	EXPECT_EQ(doomBonus(target, BonusType::GENERAL_ATTACK_REDUCTION)->val, 50);
	ASSERT_NE(doomBonus(target, BonusType::MORALE), nullptr);
	EXPECT_EQ(doomBonus(target, BonusType::MORALE)->val, -3);
	const auto secondTargetDoom = doomBonuses(secondTarget);
	EXPECT_TRUE(!secondTargetDoom || secondTargetDoom->empty()) << "Doom affects one stack only";
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Doom") != std::string::npos && line.find("50%") != std::string::npos
			&& line.find("3 rounds") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);
}

TEST_F(NewHorizonsDoomTest, ExplicitInitiativeAndBothAttackModesAreReducedOnceWithoutDefenseLoss)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, MasteryLevel::NONE));
	giveExplicitInitiative(target, 20);
	const auto movementBefore = target->getMovementRange();
	const auto initiativeBefore = target->getInitiative();
	const auto defenseBefore = target->getDefense(false);
	BattleAttackInfo ordinaryAttack(target, friendly, 0, false);
	BattleAttackInfo retaliationAttack(target, friendly, 0, false);
	retaliationAttack.retaliation = true;
	const auto ordinaryBefore = battle()->calculateDmgRange(ordinaryAttack).damage;
	const auto retaliationBefore = battle()->calculateDmgRange(retaliationAttack).damage;
	ASSERT_GT(ordinaryBefore.min, 0);
	ASSERT_GT(retaliationBefore.min, 0);

	ASSERT_TRUE(castDoom(target));
	EXPECT_EQ(target->getMovementRange(), movementBefore / 2);
	EXPECT_EQ(target->getInitiative(), initiativeBefore / 2)
		<< "Explicit initiative is reduced separately from movement Speed";
	EXPECT_EQ(target->getDefense(false), defenseBefore);
	ASSERT_NE(doomBonus(target, BonusType::STACKS_SPEED), nullptr);
	EXPECT_EQ(doomBonus(target, BonusType::STACKS_SPEED)->val, -50);
	ASSERT_NE(doomBonus(target, BonusType::STACKS_INITIATIVE), nullptr);
	EXPECT_EQ(doomBonus(target, BonusType::STACKS_INITIATIVE)->val, -50);
	ASSERT_NE(doomBonus(target, BonusType::MORALE), nullptr);
	EXPECT_EQ(doomBonus(target, BonusType::MORALE)->val, -3);

	const auto ordinaryAfter = battle()->calculateDmgRange(ordinaryAttack).damage;
	const auto retaliationAfter = battle()->calculateDmgRange(retaliationAttack).damage;
	EXPECT_EQ(ordinaryAfter.min, ordinaryBefore.min / 2)
		<< "GENERAL_ATTACK_REDUCTION applies a single penalty to a normal attack";
	EXPECT_EQ(retaliationAfter.min, retaliationBefore.min / 2)
		<< "A retaliation attack receives the same single penalty, not a compounded one";
}

TEST_F(NewHorizonsDoomTest, SpeedFallbackReducesMovementAndInitiativeOnceAndRefreshesThreeRounds)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, MasteryLevel::NONE));
	ASSERT_FALSE(target->hasBonus(Selector::type()(BonusType::STACKS_INITIATIVE_BASE)));
	const auto movementBefore = target->getMovementRange();
	const auto initiativeBefore = target->getInitiative();
	ASSERT_EQ(initiativeBefore, movementBefore) << "This creature's Initiative falls back to Speed";

	ASSERT_TRUE(castDoom(target));
	EXPECT_EQ(target->getMovementRange(), movementBefore / 2);
	EXPECT_EQ(target->getInitiative(), initiativeBefore / 2);
	EXPECT_EQ(doomBonus(target, BonusType::STACKS_INITIATIVE), nullptr)
		<< "The Speed-derived Initiative must not receive a second percentage bonus";
	ASSERT_NE(doomBonus(target, BonusType::MORALE), nullptr);
	EXPECT_EQ(doomBonus(target, BonusType::MORALE)->val, -3);
	expectTimedDoomBonuses(target, 3, 3);

	while(battle()->getRound() == 0)
		advanceRound();
	advanceRound();
	expectTimedDoomBonuses(target, 2, 3);
	ASSERT_TRUE(castDoom(target));
	expectTimedDoomBonuses(target, 3, 3);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Doom") != std::string::npos && line.find("refreshed") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);

	for(int remaining = 2; remaining >= 0; --remaining)
	{
		advanceRound();
		const auto bonuses = doomBonuses(target);
		if(remaining == 0)
			EXPECT_TRUE(!bonuses || bonuses->empty());
		else
			expectTimedDoomBonuses(target, remaining, 3);
	}
}

TEST_F(NewHorizonsDoomTest, RejectsFriendlyMultipleAndPreV3TargetsBeforeSpendingMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto * doomDefinition = doomSpell().toSpell();
	ASSERT_NE(doomDefinition, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, doomDefinition);
	const auto mechanics = doomDefinition->battleMechanics(&cast);
	spells::detail::ProblemImpl problem;
	spells::Target friendlyTarget{spells::Destination(friendly)};
	spells::Target multipleTargets{spells::Destination(target), spells::Destination(secondTarget)};
	EXPECT_FALSE(mechanics->canBeCastAt(friendlyTarget, problem));
	EXPECT_FALSE(mechanics->canBeCastAt(multipleTargets, problem));
	EXPECT_FALSE(castDoom(friendly));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(doomBonuses(friendly) == nullptr || doomBonuses(friendly)->empty());

	JsonNode v2Rules = savedV2RulesContainingDoom();
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(v2Rules, doomSpell()));
	EXPECT_FALSE(newHorizonsMagic::doomRulesEnabled(v2Rules, doomSpell()));
	EXPECT_FALSE(newHorizonsMagic::doomCripplingPenaltyPercent(v2Rules, attackerSideHero, doomSpell(), 100).has_value());
}

TEST_F(NewHorizonsDoomTest, SavedV2SnapshotWithSyntheticDoomRowIsRejectedByAuthoritativeGate)
{
	useSavedV2Rules = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & rules = battle()->getBattle()->getMagicRules();
	ASSERT_TRUE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), doomSpell()));
	EXPECT_FALSE(newHorizonsMagic::doomRulesEnabled(rules, doomSpell()));

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&cast);
	spells::detail::ProblemImpl problem;
	EXPECT_FALSE(mechanics->canBeCast(problem));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto healthBefore = target->getAvailableHealth();
	EXPECT_FALSE(castDoom(target));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(target->getAvailableHealth(), healthBefore);
	EXPECT_TRUE(doomBonuses(target) == nullptr || doomBonuses(target)->empty());
}
