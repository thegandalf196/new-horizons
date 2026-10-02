/*
 * NewHorizonsFrailtyTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/scripting/ScriptService.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/spells/effects/Effect.h"

#include <sstream>

namespace
{
constexpr auto frailtyKey = "new-horizons:frailty";
constexpr auto shadowMagicSkill = "new-horizons:shadowMagic";
constexpr auto witheringTouchPerk = "new-horizons:shadowMagic.witheringTouch";
constexpr auto frailtyEffectKey = "core:frailtyEffect";

SpellID frailtySpell()
{
	return SpellID(SpellID::decode(frailtyKey));
}

JsonNode savedV2RulesBeforeFrailty()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	rules["spells"].Struct().erase(frailtyKey);
	rules["spells"]["core:weakness"].Struct().erase("active");
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
	newHorizonsMagic::validateRules(rules);
	return rules;
}

class NewHorizonsFrailtyTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * target = nullptr;
	std::string lastCastDiagnostic;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";

		const auto effect = LIBRARY->identifiers()->getIdentifier(
			ModScope::scopeGame(), "script", std::string(frailtyEffectKey));
		ASSERT_TRUE(effect.has_value());
		EXPECT_EQ(LIBRARY->scriptTypes()->getById(ScriptID(*effect)).kind, ScriptKind::SPELL_EFFECT);
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int spellPower = 100, MasteryLevel::Type shadowRank = MasteryLevel::NONE)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(frailtySpell());
		attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(shadowMagicSkill)),
			shadowRank, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 100);
		beginCombat();
		activate(friendly);
	}

	BattleAction action(SpellID spell, const CStack * unit) const
	{
		BattleAction result;
		result.actionType = EActionType::HERO_SPELL;
		result.side = BattleSide::ATTACKER;
		result.spell = spell;
		result.aimToUnit(unit);
		return result;
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
	}

	bool cast(SpellID spell, const CStack * unit)
	{
		activate(friendly);
		const bool accepted = gameHandler->battles->makePlayerBattleAction(
			BattleID(0), PlayerColor(0), action(spell, unit));
		lastCastDiagnostic.clear();
		if(!accepted)
		{
			spells::BattleCast spellCast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
			spells::Target destination;
			destination.emplace_back(unit);
			spells::detail::ProblemImpl problem;
			auto mechanics = spell.toSpell()->battleMechanics(&spellCast);
			const bool canCast = mechanics->canBeCast(problem);
			const bool canCastAt = canCast && mechanics->canBeCastAt(destination, problem);
			std::vector<std::string> problems;
			problem.getAll(problems);
			lastCastDiagnostic = "player action rejected; canBeCast=" + std::to_string(canCast)
				+ ", canBeCastAt=" + std::to_string(canCastAt);
			for(const auto & message : problems)
				lastCastDiagnostic += "; " + message;
		}
		return accepted;
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	TConstBonusListPtr frailtyBonuses(const CStack * unit) const
	{
		return unit->getAllBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(frailtySpell())));
	}

	std::string defenseBonusDiagnostic(const CStack * unit) const
	{
		std::ostringstream result;
		bool found = false;
		for(const auto & bonus : unit->getExportedBonusList())
		{
			if(bonus->type != BonusType::PRIMARY_SKILL
				|| bonus->subtype != BonusSubtypeID(PrimarySkill::DEFENSE))
				continue;

			found = true;
			result << " [source=" << static_cast<int>(bonus->source)
				<< ", sid=" << bonus->sid.getNum()
				<< ", val=" << bonus->val;
			if(bonus->parameters)
				result << ", addInfo=" << bonus->parameters->toNumber();
			result << ']';
		}
		return found ? result.str() : " no exported Defense bonuses";
	}

	void selectWitheringTouch()
	{
		const auto skill = SecondarySkill(SecondarySkill::decode(shadowMagicSkill));
		if(attackerSideHero->getSecSkillLevel(skill) < MasteryLevel::BASIC)
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({shadowMagicSkill, witheringTouchPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(shadowMagicSkill, witheringTouchPerk));
	}
};
}

TEST_F(NewHorizonsFrailtyTest, RegistersLevelTwoSpellAndReplacesWeaknessOnlyForNewWorlds)
{
	const auto frailty = frailtySpell();
	ASSERT_NE(frailty, SpellID::NONE);
	ASSERT_NE(frailty.toSpell(), nullptr);
	ASSERT_TRUE(frailty.toSpell()->hasBattleEffects());
	ASSERT_TRUE(frailty.toSpell()->isCommonHeroSpell());
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(rules, frailty));
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, frailty), 2);
	EXPECT_EQ(newHorizonsMagic::spellCost(rules, frailty, MasteryLevel::NONE), 8);

	const SpellID weakness(SpellID::WEAKNESS);
	ASSERT_NE(weakness, SpellID::NONE);
	EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(rules, weakness));
	const JsonNode oldRules = savedV2RulesBeforeFrailty();
	EXPECT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(oldRules, weakness));
	EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(oldRules, frailty));

	startGame();
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), frailty));
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), weakness))
		<< "A new v3 Mage Guild or teacher must not offer the superseded Level-2 Weakness entry";
	startBattle();
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), frailty));
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), weakness));
	spells::BattleCast spellCast(battle(), attackerSideHero, spells::Mode::HERO, frailty.toSpell());
	const auto mechanics = frailty.toSpell()->battleMechanics(&spellCast);
	int effectCount = 0;
	mechanics->forEachEffect([&](const spells::effects::Effect & effect)
	{
		++effectCount;
		EXPECT_EQ(effect.name, "frailty");
		return false;
	});
	EXPECT_EQ(effectCount, 1) << "Frailty's configured Lua spell effect must load into battle mechanics";
}

TEST_F(NewHorizonsFrailtyTest, CastUsesSavedShadowCoefficientAndIntrinsicBaseDefense)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, MasteryLevel::ADVANCED));
	const int baseDefense = target->unitType()->getBaseDefense();
	ASSERT_GT(baseDefense, 0);
	const auto creatureDefense = Selector::typeSubtype(BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::DEFENSE)).And(Selector::sourceTypeSel(BonusSource::CREATURE_ABILITY));
	EXPECT_EQ(target->valOfBonuses(creatureDefense), baseDefense)
		<< "The Lua base-defense source filter must see intrinsic Creature Defense";
	spells::BattleCast spellCast(battle(), attackerSideHero, spells::Mode::HERO, frailtySpell().toSpell());
	const auto mechanics = frailtySpell().toSpell()->battleMechanics(&spellCast);
	EXPECT_TRUE(mechanics->isReceptive(target)) << "Effect filtering must not discard a receptive target";
	EXPECT_TRUE(mechanics->canBeCastAt(spells::Target{spells::Destination(target)}));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::PRIMARY_SKILL, BonusSource::OTHER, 7, BonusSourceID(),
		BonusSubtypeID(PrimarySkill::DEFENSE)));
	const int defenseBefore = target->getDefense(false);
	EXPECT_EQ(defenseBefore, baseDefense + 7);

	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, frailtySpell());
	EXPECT_EQ(coefficient, 13000);
	EXPECT_TRUE(target->isValidTarget(false));
	EXPECT_TRUE(mechanics->isSmart());
	EXPECT_TRUE(mechanics->ownerMatches(target));
	const spells::Target aim{spells::Destination(target)};
	const auto spellTarget = mechanics->canonicalizeTarget(aim);
	ASSERT_EQ(spellTarget.size(), 1u);
	const spells::effects::Effect * frailtyEffect = nullptr;
	mechanics->forEachEffect([&](const spells::effects::Effect & effect)
	{
		frailtyEffect = &effect;
		return true;
	});
	ASSERT_NE(frailtyEffect, nullptr);
	const auto transformedTarget = frailtyEffect->transformTarget(mechanics.get(), aim, spellTarget);
	ASSERT_EQ(transformedTarget.size(), 1u) << "Frailty transformTarget removed the selected enemy";
	const auto filteredTarget = frailtyEffect->filterTarget(mechanics.get(), transformedTarget);
	ASSERT_EQ(filteredTarget.size(), 1u)
		<< "Frailty filterTarget removed a receptive enemy";
	ASSERT_TRUE(cast(frailtySpell(), target)) << lastCastDiagnostic;

	const auto applied = frailtyBonuses(target);
	ASSERT_EQ(applied->size(), 1u) << defenseBonusDiagnostic(target);
	const auto & bonus = *applied->front();
	const int accumulatedBasisPoints = 1000 + 100 * 5 * coefficient / 10000;
	const int expectedLoss = baseDefense * accumulatedBasisPoints / 10000;
	EXPECT_EQ(bonus.type, BonusType::PRIMARY_SKILL);
	EXPECT_EQ(bonus.subtype, BonusSubtypeID(PrimarySkill::DEFENSE));
	EXPECT_EQ(bonus.source, BonusSource::SPELL_EFFECT);
	EXPECT_EQ(bonus.sid, BonusSourceID(frailtySpell()));
	EXPECT_EQ(bonus.val, -expectedLoss);
	ASSERT_NE(bonus.parameters, nullptr);
	EXPECT_EQ(bonus.parameters->toNumber(), accumulatedBasisPoints);
	EXPECT_EQ(bonus.duration, BonusDuration::ONE_BATTLE);
	EXPECT_EQ(bonus.turnsRemain, 0);
	EXPECT_EQ(target->getDefense(false), defenseBefore - expectedLoss)
		<< "Only base Creature Defense is eroded; the unrelated +7 bonus is preserved";
}

TEST_F(NewHorizonsFrailtyTest, RecastsAccumulateBasisPointsOnceAndClampAtSixtyPercent)
{
	ASSERT_NO_FATAL_FAILURE(prepare(300));
	const int baseDefense = target->unitType()->getBaseDefense();
	ASSERT_GT(baseDefense, 0);

	for(int castNumber = 1; castNumber <= 4; ++castNumber)
	{
		if(castNumber > 1)
			advanceRound();
		ASSERT_TRUE(cast(frailtySpell(), target)) << lastCastDiagnostic;

		const auto applied = frailtyBonuses(target);
		ASSERT_EQ(applied->size(), 1u) << "Recasting must replace the previous spell-effect bonus"
			<< defenseBonusDiagnostic(target);
		const int expectedBasisPoints = std::min(6000, castNumber * 2000);
		EXPECT_EQ(applied->front()->parameters->toNumber(), expectedBasisPoints);
		EXPECT_EQ(applied->front()->val, -(baseDefense * expectedBasisPoints / 10000));
		EXPECT_EQ(applied->front()->duration, BonusDuration::ONE_BATTLE);
		EXPECT_EQ(applied->front()->turnsRemain, 0);
	}
}

TEST_F(NewHorizonsFrailtyTest, WitheringTouchAddsFivePointsPerCastWithoutIncreasingTheBattleCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare(10000, MasteryLevel::BASIC));
	selectWitheringTouch();
	const int baseDefense = target->unitType()->getBaseDefense();
	ASSERT_GT(baseDefense, 0);

	for(int castNumber = 1; castNumber <= 4; ++castNumber)
	{
		if(castNumber > 1)
			advanceRound();
		ASSERT_TRUE(cast(frailtySpell(), target)) << lastCastDiagnostic;
		const auto applied = frailtyBonuses(target);
		ASSERT_EQ(applied->size(), 1u) << defenseBonusDiagnostic(target);
		const int expectedBasisPoints = std::min(6000, castNumber * 2500);
		EXPECT_EQ(applied->front()->parameters->toNumber(), expectedBasisPoints);
		EXPECT_EQ(applied->front()->val, -(baseDefense * expectedBasisPoints / 10000));
	}
}

TEST_F(NewHorizonsFrailtyTest, OrdinaryDispelRemovesTheAccumulatedCurse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int defenseBefore = target->getDefense(false);
	ASSERT_TRUE(cast(frailtySpell(), target)) << lastCastDiagnostic;
	ASSERT_EQ(frailtyBonuses(target)->size(), 1u);
	EXPECT_LT(target->getDefense(false), defenseBefore);

	advanceRound();
	ASSERT_TRUE(cast(SpellID::DISPEL, target)) << lastCastDiagnostic;
	EXPECT_TRUE(frailtyBonuses(target)->empty());
	EXPECT_EQ(target->getDefense(false), defenseBefore);
}

TEST_F(NewHorizonsFrailtyTest, SmartNegativeTargetingRejectsFriendlyStacksWithoutSpendingMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto beforeMana = attackerSideHero->getManaAvailable();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, frailtySpell().toSpell());
	EXPECT_FALSE(frailtySpell().toSpell()->battleMechanics(&cast)->canBeCastAt(
		spells::Target{spells::Destination(friendly)}));
	EXPECT_FALSE(this->cast(frailtySpell(), friendly));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), beforeMana);
	EXPECT_TRUE(frailtyBonuses(friendly)->empty());
}
