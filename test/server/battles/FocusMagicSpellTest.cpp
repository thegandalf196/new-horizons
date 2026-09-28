/*
 * FocusMagicSpellTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "BattleStartSnapshotFixture.h"
#include "HeroCommandFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"

#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/constants/StringConstants.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/IdentifierStorage.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/scripting/ScriptService.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/Problem.h"

#include <array>
#include <utility>

namespace
{
constexpr auto sorcerySkillKey = "new-horizons:sorceryMagic";
constexpr auto warcastingSkillKey = "new-horizons:warcasting";
constexpr auto focusMagicEffectKey = "core:focusMagicEnchantment";

SpellID focusMagicSpell()
{
	return SpellID(SpellID::decode(newHorizonsSorcery::FOCUS_MAGIC_SPELL));
}

class FocusMagicSpellTest : public HeroCommandFixture
{
protected:
	SpellID spell = SpellID::NONE;
	ScriptID focusMagicTrigger;
	ScriptID focusMagicEffect;
	bool useV2MagicRules = false;
	CStack * friendlyShooter = nullptr;
	CStack * friendlyNonShooter = nullptr;
	CStack * hostileShooter = nullptr;
	CStack * defenderShooter = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";

		spell = focusMagicSpell();
		ASSERT_NE(spell, SpellID::NONE);
		const auto script = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
			std::string(newHorizonsSorcery::FOCUS_MAGIC_TRIGGER));
		ASSERT_TRUE(script.has_value());
		focusMagicTrigger = ScriptID(*script);

		const auto effect = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script",
			std::string(focusMagicEffectKey));
		ASSERT_TRUE(effect.has_value());
		focusMagicEffect = ScriptID(*effect);
		ASSERT_NE(focusMagicEffect, focusMagicTrigger);
		EXPECT_EQ(LIBRARY->scriptTypes()->getById(focusMagicEffect).kind, ScriptKind::SPELL_EFFECT);
		EXPECT_EQ(LIBRARY->scriptTypes()->getById(focusMagicTrigger).kind, ScriptKind::COMBAT_EVENT);
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));

		auto magicRules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		ASSERT_FALSE(magicRules["spells"][newHorizonsSorcery::FOCUS_MAGIC_SPELL].isNull());
		if(useV2MagicRules)
		{
			magicRules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
			magicRules.Struct().erase("schoolRankPowerCoefficientPercent");
		}
		newHorizonsMagic::validateRules(magicRules);
		// Exercise installed roster eligibility, not a test-only spell entry.
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
	}

	bool configureCaster(CGHeroInstance * hero, int32_t spellPower,
		MasteryLevel::Type sorceryMastery = MasteryLevel::EXPERT)
	{
		const auto sorcery = SecondarySkill::decode(sorcerySkillKey);
		if(sorcery < 0)
			return false;
		hero->setSecSkillLevel(SecondarySkill(sorcery), sorceryMastery, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		hero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(hero, 1000);
		return true;
	}

	bool prepare(int32_t spellPower = 20)
	{
		startGame();
		if(!configureCaster(attackerSideHero, spellPower)
			|| !configureCaster(defenderSideHero, spellPower))
			return false;

		startBattle();
		friendlyShooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex), 10);
		friendlyNonShooter = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex - 2), 10);
		hostileShooter = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(rightHex), 10);
		defenderShooter = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(rightHex + 4), 10);
		if(!friendlyShooter || !friendlyNonShooter || !hostileShooter || !defenderShooter)
			return false;
		battle()->nextRound();
		return true;
	}

	std::vector<const Bonus *> focusMagicBonuses(const CStack * unit) const
	{
		std::vector<const Bonus *> result;
		for(const auto & bonus : unit->getExportedBonusList())
		{
			if(bonus->type == BonusType::COMBAT_EVENT_TRIGGER
				&& bonus->subtype == BonusSubtypeID(focusMagicTrigger)
				&& bonus->source == BonusSource::SPELL_EFFECT
				&& bonus->sid == BonusSourceID(spell))
				result.push_back(bonus.get());
		}
		return result;
	}

	void expectCapturedFocusMagic(const CStack * unit, int32_t basisPoints, BattleSide beneficiary,
		int32_t remainingRounds = newHorizonsSorcery::FOCUS_MAGIC_DURATION_ROUNDS,
		bool arcaneAcquisition = false) const
	{
		const auto bonuses = focusMagicBonuses(unit);
		ASSERT_EQ(bonuses.size(), 1u);
		const auto * bonus = bonuses.front();
		EXPECT_EQ(bonus->val, basisPoints);
		EXPECT_EQ(bonus->duration, BonusDuration::N_TURNS);
		EXPECT_EQ(bonus->turnsRemain, remainingRounds);
		ASSERT_NE(bonus->parameters, nullptr);
		EXPECT_EQ(bonus->parameters->toCustom<JsonNode>()["beneficiarySide"].Integer(),
			static_cast<int32_t>(beneficiary));
		const auto parameters = bonus->parameters->toCustom<JsonNode>();
		ASSERT_TRUE(parameters["arcaneAcquisition"].isBool());
		EXPECT_EQ(parameters["arcaneAcquisition"].Bool(), arcaneAcquisition);
	}

	bool canCastOnWithDiagnostics(const CGHeroInstance * hero, const CStack * target) const
	{
		spells::BattleCast cast(battle(), hero, spells::Mode::HERO, spell.toSpell());
		spells::Target destination;
		destination.emplace_back(target);
		spells::detail::ProblemImpl problem;
		auto mechanics = spell.toSpell()->battleMechanics(&cast);
		const bool canBeCast = mechanics->canBeCast(problem);
		EXPECT_TRUE(canBeCast) << "Focus Magic canBeCast failed before target validation";
		const bool canBeCastAtTarget = canBeCast && mechanics->canBeCastAt(destination, problem);
		EXPECT_TRUE(canBeCastAtTarget) << "Focus Magic canBeCastAt failed for the selected target";
		std::vector<std::string> problems;
		problem.getAll(problems);
		EXPECT_TRUE(canBeCast && canBeCastAtTarget)
			<< "Focus Magic cast preflight problems: " << testing::PrintToString(problems);
		return canBeCast && canBeCastAtTarget;
	}

	bool castOnWithDiagnostics(const CGHeroInstance * hero, const CStack * target) const
	{
		if(!canCastOnWithDiagnostics(hero, target))
			return false;

		spells::BattleCast cast(battle(), hero, spells::Mode::HERO, spell.toSpell());
		spells::Target destination;
		destination.emplace_back(target);
		cast.cast(gameHandler->spellEnv.get(), destination);
		return true;
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = stack->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	bool castAtUnit(const CStack * target, bool followup = false)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		action.metamagicFollowup = followup;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
};
}

TEST_F(FocusMagicSpellTest, RealCastTargetsFriendlyRangedCapableStackEvenWithoutRemainingAmmunition)
{
	ASSERT_TRUE(prepare());
	ASSERT_TRUE(friendlyShooter->isShooter());
	ASSERT_GT(friendlyShooter->shots.available(), 0);
	friendlyShooter->shots.use(friendlyShooter->shots.available());
	EXPECT_TRUE(friendlyShooter->isShooter());
	EXPECT_FALSE(friendlyShooter->canShoot());

	ASSERT_TRUE(castOnWithDiagnostics(attackerSideHero, friendlyShooter));
	expectCapturedFocusMagic(friendlyShooter, 1145, BattleSide::ATTACKER);
}

TEST_F(FocusMagicSpellTest, RealCastScalesSpellPowerBySorceryRankAtLowPower)
{
	ASSERT_TRUE(prepare(3));
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(),
		newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION);
	const auto sorcery = SecondarySkill::decode(sorcerySkillKey);
	ASSERT_GE(sorcery, 0);

	const std::array<std::pair<MasteryLevel::Type, int32_t>, 4> cases = {{
		{MasteryLevel::NONE, 1015},
		{MasteryLevel::BASIC, 1017},
		{MasteryLevel::ADVANCED, 1019},
		{MasteryLevel::EXPERT, 1021}
	}};
	for(size_t index = 0; index < cases.size(); ++index)
	{
		if(index > 0)
			advanceRound();
		attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), cases[index].first,
			ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(castOnWithDiagnostics(attackerSideHero, friendlyShooter));
		expectCapturedFocusMagic(friendlyShooter, cases[index].second, BattleSide::ATTACKER);
	}
}

TEST_F(FocusMagicSpellTest, RealCastWithSavedV2ProfileUsesTheUnrankedPowerCoefficient)
{
	useV2MagicRules = true;
	ASSERT_TRUE(prepare(3));
	ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(),
		newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION);
	EXPECT_FALSE(battle()->getMagicRules().Struct().contains("schoolRankPowerCoefficientPercent"));
	ASSERT_TRUE(castOnWithDiagnostics(attackerSideHero, friendlyShooter));
	expectCapturedFocusMagic(friendlyShooter, 1015, BattleSide::ATTACKER);
}

TEST_F(FocusMagicSpellTest, ContextualHelpShowsSavedSorceryRankAndOrdinaryMarkValue)
{
	ASSERT_TRUE(prepare(3));
	const auto sorcery = SecondarySkill::decode(sorcerySkillKey);
	ASSERT_GE(sorcery, 0);
	ASSERT_EQ(attackerSideHero->getEffectPower(spell.toSpell()), 3);

	struct HelpExpectation
	{
		MasteryLevel::Type rank;
		const char * rankName;
		int coefficientPercent;
		const char * currentPenetration;
	};
	const std::array<HelpExpectation, 4> cases = {{
		{MasteryLevel::NONE, "No Sorcery Rank", 100, "10.15"},
		{MasteryLevel::BASIC, "Basic Sorcery", 115, "10.17"},
		{MasteryLevel::ADVANCED, "Advanced Sorcery", 130, "10.19"},
		{MasteryLevel::EXPERT, "Expert Sorcery", 145, "10.21"}
	}};
	for(const auto & expected : cases)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(sorcery), expected.rank,
			ChangeValueMode::ABSOLUTE);
		const auto description = newHorizonsMagic::spellDescriptionForHero(
			attackerSideHero, spell.toSpell(), 0);
		EXPECT_NE(description.find("Under saved v3 rules, the coefficient is 100% with no rank, 115% at Basic, "
			"130% at Advanced, and 145% at Expert; it scales only the Spell Power term, "
			"leaving the 10% base unchanged."), std::string::npos);
		EXPECT_NE(description.find("Saved v1/v2 profiles use 100% at every Sorcery rank."), std::string::npos);
		EXPECT_NE(description.find(std::string(expected.rankName) + ": "
			+ std::to_string(expected.coefficientPercent) + "% coefficient on the Spell Power term."),
			std::string::npos);
		EXPECT_NE(description.find(std::string("Current ordinary per-mark penetration at Spell Power 3: ")
			+ expected.currentPenetration + "% (before battle-only Warcasting)."), std::string::npos);
	}
}

TEST_F(FocusMagicSpellTest, ContextualHelpKeepsSavedV2AtUnrankedCoefficient)
{
	useV2MagicRules = true;
	ASSERT_TRUE(prepare(3));
	// configureCaster leaves this hero at Expert, but v2 saved rules retain 100%.
	const auto description = newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell.toSpell(), 0);
	EXPECT_NE(description.find("Legacy profile: 100% coefficient on the Spell Power term; "
		"Sorcery rank does not scale Focus Magic."), std::string::npos);
	EXPECT_NE(description.find("Current ordinary per-mark penetration at Spell Power 3: "
		"10.15% (before battle-only Warcasting)."), std::string::npos);
}

TEST_F(FocusMagicSpellTest, EchoedDurationExtendsOnlyTheActuallyAppliedAdditionalCast)
{
	startGame();
	ASSERT_TRUE(configureCaster(attackerSideHero, 20));
	constexpr auto metamagic = "new-horizons:metamagic";
	const auto skill = SecondarySkill::decode(metamagic);
	ASSERT_GE(skill, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(skill), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({metamagic, "new-horizons:metamagic.arcaneEconomy"});
	attackerSideHero->applyPerkSelection({metamagic, std::string(newHorizonsMagic::METAMAGIC_ECHOED_DURATION)});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(metamagic, std::string(newHorizonsMagic::METAMAGIC_ECHOED_DURATION)));
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(leftHex), 10);
	ASSERT_NE(shooter, nullptr);
	beginCombat();
	activate(shooter);
	ASSERT_TRUE(castAtUnit(shooter));
	expectCapturedFocusMagic(shooter, 1145, BattleSide::ATTACKER, 3);
	ASSERT_TRUE(castAtUnit(shooter, true));
	expectCapturedFocusMagic(shooter, 1145, BattleSide::ATTACKER, 4);
}

TEST_F(FocusMagicSpellTest, RealCastRejectsFriendlyNonShooterAndHostileShooter)
{
	ASSERT_TRUE(prepare());
	ASSERT_FALSE(friendlyNonShooter->isShooter());
	ASSERT_TRUE(hostileShooter->isShooter());
	EXPECT_FALSE(castOn(attackerSideHero, spell, friendlyNonShooter));
	EXPECT_FALSE(castOn(attackerSideHero, spell, hostileShooter));
	EXPECT_TRUE(focusMagicBonuses(friendlyNonShooter).empty());
	EXPECT_TRUE(focusMagicBonuses(hostileShooter).empty());
}

TEST_F(FocusMagicSpellTest, RealRecastReplacesSnapshotAndKeepsFixedThreeRoundDuration)
{
	ASSERT_TRUE(prepare(20));
	// A second hero spell is only legal after the next round; the enchantment's
	// remaining duration then proves that recasting refreshes it back to 3 rounds.
	ASSERT_TRUE(castOnWithDiagnostics(attackerSideHero, friendlyShooter));
	const auto firstPower = attackerSideHero->getEffectPower(spell.toSpell());
	ASSERT_EQ(firstPower, 20);
	expectCapturedFocusMagic(friendlyShooter, 1145, BattleSide::ATTACKER);

	advanceRound();
	expectCapturedFocusMagic(friendlyShooter, 1145, BattleSide::ATTACKER, 2);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(castOnWithDiagnostics(attackerSideHero, friendlyShooter));
	const auto secondPower = attackerSideHero->getEffectPower(spell.toSpell());
	ASSERT_EQ(secondPower, 100);
	expectCapturedFocusMagic(friendlyShooter, 1725, BattleSide::ATTACKER);
}

TEST_F(FocusMagicSpellTest, DefenderCastCapturesTheDefenderAsBeneficiary)
{
	ASSERT_TRUE(prepare());
	ASSERT_TRUE(castOnWithDiagnostics(defenderSideHero, defenderShooter));
	expectCapturedFocusMagic(defenderShooter, 1145, BattleSide::DEFENDER);
}

TEST_F(FocusMagicSpellTest, WarcastingCastBoostsTheCapturedSpellPowerComponent)
{
	startGame();
	if(!configureCaster(attackerSideHero, 1, MasteryLevel::BASIC))
		FAIL() << "New Horizons Sorcery skill is unavailable";
	const auto warcasting = SecondarySkill::decode(warcastingSkillKey);
	ASSERT_GE(warcasting, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(warcasting), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 100, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:archer"), 10));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), 1));
	startBattle();
	const CStack * shooter = nullptr;
	for(const auto * stack : battle()->battleGetAllStacks())
		if(stack->unitSlot() == SlotID(0) && stack->unitSide() == BattleSide::ATTACKER)
			shooter = stack;
	ASSERT_NE(shooter, nullptr);
	ASSERT_TRUE(shooter->isShooter());
	beginCombat();
	activate(shooter);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();

	const auto manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(canCastOnWithDiagnostics(attackerSideHero, shooter));
	ASSERT_TRUE(castAtUnit(shooter));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - newHorizonsSorcery::FOCUS_MAGIC_MANA);
	// Basic Sorcery (115%) and the saved Basic Warcasting readiness (110%) act
	// on 5*Spell Power before one final floor: floor(5 * 1.15 * 1.10) = 6.
	expectCapturedFocusMagic(shooter, 1006, BattleSide::ATTACKER);

	advanceRound();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
	activate(shooter);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	advanceRound();
	ASSERT_TRUE(canCastOnWithDiagnostics(attackerSideHero, shooter));
	ASSERT_TRUE(castAtUnit(shooter));
	expectCapturedFocusMagic(shooter, 2000, BattleSide::ATTACKER);
}

TEST_F(FocusMagicSpellTest, AuthoritativeDoubleShotAddsMarksBetweenHitsAndSelectiveDispelRemovesThem)
{
	startGame();
	ASSERT_TRUE(configureCaster(attackerSideHero, 200));
	ASSERT_TRUE(configureCaster(defenderSideHero, 200));
	defenderSideHero->addSpellToSpellbook(SpellID::DISPEL);
	defenderSideHero->applyPerkSelection({sorcerySkillKey, "new-horizons:sorceryMagic.selectiveDispel"});
	ASSERT_TRUE(defenderSideHero->hasActivePerk(sorcerySkillKey, "new-horizons:sorceryMagic.selectiveDispel"));
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:grandElf"), BattleHex(3, 5), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 1000);
	forceMaximumDamage(shooter);
	beginCombat();
	activate(shooter);
	ASSERT_TRUE(battle()->battleCanShoot(shooter, target->getPosition()));
	ASSERT_TRUE(castAtUnit(shooter));
	expectCapturedFocusMagic(shooter, 2000, BattleSide::ATTACKER);
	const auto baseline = battle()->calculateDmgRange(BattleAttackInfo(shooter, target, 0, true)).damage;
	ASSERT_EQ(baseline.min, baseline.max);
	const auto healthBefore = target->getAvailableHealth();
	server.attacks.clear();
	server.battleLogLines.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, target)));
	std::vector<int64_t> shotDamage;
	for(const auto & attack : server.attacks)
	{
		if(attack.stackAttacking != shooter->unitId() || !attack.shot() || attack.counter())
			continue;
		for(const auto & hit : attack.bsa)
			if(hit.stackAttacked == target->unitId())
				shotDamage.push_back(hit.damageAmount);
	}
	ASSERT_EQ(shotDamage.size(), 2u);
	EXPECT_EQ(shotDamage.front(), baseline.max);
	EXPECT_GT(shotDamage.back(), shotDamage.front());
	EXPECT_EQ(target->getAvailableHealth(), healthBefore - shotDamage.front() - shotDamage.back());
	std::vector<std::string> markLogs;
	for(const auto & line : server.battleLogLines)
		if(line.find("Arcane Breach") != std::string::npos)
			markLogs.push_back(line);
	ASSERT_EQ(markLogs.size(), 2u) << testing::PrintToString(server.battleLogLines);
	EXPECT_THAT(markLogs.front(), testing::HasSubstr("20.00%"));
	EXPECT_THAT(markLogs.back(), testing::HasSubstr("40.00%"));
	for(const auto & line : markLogs)
	{
		EXPECT_THAT(line, testing::HasSubstr("Angels"));
		EXPECT_THAT(line, testing::HasSubstr("attacking"));
		EXPECT_THAT(line, testing::HasSubstr("2 rounds"));
		EXPECT_THAT(line, testing::Not(testing::HasSubstr("%s")));
		EXPECT_THAT(line, testing::Not(testing::HasSubstr("%d")));
	}
	const auto markSource = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::decode(newHorizonsSorcery::ARCANE_BREACH_EFFECT))));
	const auto marks = target->getBonuses(markSource);
	ASSERT_EQ(marks->size(), 2u);
	for(const auto & mark : *marks)
	{
		EXPECT_EQ(mark->val, 2000);
		EXPECT_EQ(mark->turnsRemain, 2);
	}

	// Selective Dispel must recognize the marks' negative source independently
	// of the positive Focus Magic enchantment which caused them.
	auto positive = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::BLESS)));
	positive->turnsRemain = 3;
	target->addNewBonus(positive);
	activate(target);
	BattleAction dispel;
	dispel.actionType = EActionType::HERO_SPELL;
	dispel.side = BattleSide::DEFENDER;
	dispel.spell = SpellID::DISPEL;
	dispel.spellSelectiveDispel = true;
	dispel.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), dispel));
	EXPECT_FALSE(target->hasBonus(markSource));
	EXPECT_TRUE(target->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BLESS)))));
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(shooter, target, 0, true)).damage.max, baseline.max);
}

TEST_F(FocusMagicSpellTest, ArcaneAcquisitionSnapshotsMetamagicAndRechecksMarksBetweenHits)
{
	startGame();
	ASSERT_TRUE(configureCaster(attackerSideHero, 200));
	const auto metamagic = SecondarySkill::decode("new-horizons:metamagic");
	ASSERT_GE(metamagic, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(metamagic), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:metamagic",
		"new-horizons:metamagic.arcaneAcquisition"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic",
		"new-horizons:metamagic.arcaneAcquisition"));
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:grandElf"), 10));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:angel"), 1000));
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:angel"), 1000));
	startBattle();
	const CStack * shooter = nullptr;
	const CStack * target = nullptr;
	const CStack * secondTarget = nullptr;
	for(const auto * stack : battle()->battleGetAllStacks())
	{
		if(stack->unitSlot() == SlotID(0) && stack->unitSide() == BattleSide::ATTACKER)
			shooter = stack;
		if(stack->unitSlot() == SlotID(0) && stack->unitSide() == BattleSide::DEFENDER)
			target = stack;
		if(stack->unitSlot() == SlotID(1) && stack->unitSide() == BattleSide::DEFENDER)
			secondTarget = stack;
	}
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(secondTarget, nullptr);
	forceMaximumDamage(const_cast<CStack *>(shooter));
	beginCombat();
	activate(shooter);
	ASSERT_TRUE(castAtUnit(shooter));
	ASSERT_TRUE(castAtUnit(shooter, true));
	expectCapturedFocusMagic(shooter, 2000, BattleSide::ATTACKER,
		newHorizonsSorcery::FOCUS_MAGIC_DURATION_ROUNDS, true);
	auto restoredBattle = battleStartFixture::snapshot(*battle(), gameState().get());
	// BattleStart carries detached stack descriptors, not live unit health state.
	const auto * restoredShooter = restoredBattle->getStack(shooter->unitId(), false);
	ASSERT_NE(restoredShooter, nullptr);
	expectCapturedFocusMagic(restoredShooter, 2000, BattleSide::ATTACKER,
		newHorizonsSorcery::FOCUS_MAGIC_DURATION_ROUNDS, true);

	ASSERT_TRUE(battle()->battleCanShoot(shooter, target->getPosition()));
	server.attacks.clear();
	server.battleLogLines.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, target)));
	const auto markSource = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::decode(newHorizonsSorcery::ARCANE_BREACH_EFFECT))));
	const auto marks = target->getBonuses(markSource);
	ASSERT_EQ(marks->size(), 3u);
	std::vector<std::string> markLogs;
	for(const auto & line : server.battleLogLines)
		if(line.find("Arcane Breach") != std::string::npos)
			markLogs.push_back(line);
	ASSERT_EQ(markLogs.size(), 2u) << testing::PrintToString(server.battleLogLines);
	EXPECT_THAT(markLogs.front(), testing::HasSubstr("40.00%"));
	EXPECT_THAT(markLogs.back(), testing::HasSubstr("60.00%"));

	advanceRound();
	activate(shooter);
	ASSERT_TRUE(battle()->battleCanShoot(shooter, secondTarget->getPosition()));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, secondTarget)));
	const auto secondTargetMarks = secondTarget->getBonuses(markSource);
	// Grand Elf resolves two shots separately: this unmarked target receives two
	// marks on the first hit and one more after the second hit is rechecked.
	EXPECT_EQ(secondTargetMarks->size(), 3u);
}
