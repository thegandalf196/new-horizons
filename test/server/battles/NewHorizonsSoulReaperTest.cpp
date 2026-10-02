/*
 * NewHorizonsSoulReaperTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/callback/CGameInfoCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/bonuses/Bonus.h"
#include <vcmi/Environment.h>

namespace
{
SpellID soulReaperSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsMagic::SHADOW_SOUL_REAPER_SPELL)));
}

class SoulReaperPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
	const GameCb * world;
	const BattleCb * selectedBattle;
public:
	SoulReaperPredictionEnvironment(std::shared_ptr<CGameState> state, const GameCb * world,
		const BattleCb * selectedBattle = nullptr)
		: state(std::move(state)), world(world), selectedBattle(selectedBattle)
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override
	{
		return selectedBattle && id == BattleID(0) ? selectedBattle : state->getBattle(id);
	}
	const GameCb * game() const override { return world ? world : state.get(); }
};
}

class NewHorizonsSoulReaperTest : public HeroCommandFixture
{
protected:
	bool savedV2SnapshotWithSoulReaper = false;
	CStack * target = nullptr;
	const CSpell * spell = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		ASSERT_NE(soulReaperSpell(), SpellID::NONE);
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(savedV2SnapshotWithSoulReaper)
		{
			rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
			rules.Struct().erase("schoolRankPowerCoefficientPercent");
			rules.Struct().erase("spellcraftEfficiencyPercent");
			for(auto & [spellId, spellRow] : rules["spells"].Struct())
			{
				(void)spellId;
				spellRow.Struct().erase("selectedPlacement");
				if(spellRow.Struct().contains("variant"))
				{
					spellRow.Struct().erase("variant");
					spellRow["active"].Bool() = false;
				}
			}
		}
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	}

	void prepare(int32_t spellPower = 100)
	{
		startGame();
		spell = soulReaperSpell().toSpell();
		ASSERT_NE(spell, nullptr);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell->getId());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 100);
		ASSERT_NE(target, nullptr);
		beginCombat();
	}

	void damageStack(const CStack * stack, int64_t damage)
	{
		auto state = stack->acquireState();
		state->damage(damage);
		UnitChanges update(stack->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		update.healthDelta = -damage;
		BattleUnitsChanged changed;
		changed.battleID = BattleID(0);
		changed.changedStacks.push_back(std::move(update));
		gameHandler->sendAndApply(changed);
	}

	BattleAction actionFor(const CStack * selectedTarget) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = soulReaperSpell();
		action.aimToUnit(selectedTarget);
		return action;
	}

	int64_t forecastDamage(const CStack * realTarget)
	{
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		SoulReaperPredictionEnvironment environment(gameState(), nullptr);
		HypotheticBattle predicted(&environment, callback);
		const auto * projectedTarget = predicted.battleGetUnitByID(realTarget->unitId());
		EXPECT_NE(projectedTarget, nullptr);
		if(!projectedTarget)
			return 0;

		spells::Target aim;
		aim.emplace_back(projectedTarget);
		spells::BattleCast prediction(&predicted, attackerSideHero, spells::Mode::HERO, spell);
		auto mechanics = spell->battleMechanics(&prediction);
		const auto healthBefore = projectedTarget->getAvailableHealth();
		mechanics->castEval(predicted.getServerCallback(), aim);
		const auto * after = predicted.battleGetUnitByID(realTarget->unitId());
		EXPECT_NE(after, nullptr);
		return after ? healthBefore - after->getAvailableHealth() : 0;
	}
};

TEST_F(NewHorizonsSoulReaperTest, SavedV3FormulaAddsFortyPercentMissingHealthAndScalesOnlySpellPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(newHorizonsMagic::soulReaperEnabled(battle()->getMagicRules(), soulReaperSpell()));
	EXPECT_EQ(newHorizonsMagic::spellLevel(battle()->getMagicRules(), soulReaperSpell()), 5);
	for(int mastery = MasteryLevel::NONE; mastery <= MasteryLevel::EXPERT; ++mastery)
		EXPECT_EQ(newHorizonsMagic::spellCost(battle()->getMagicRules(), soulReaperSpell(), mastery), 21);

	damageStack(target, 600);
	ASSERT_EQ(target->getShadowGiftMaximumHealth(), 1000);
	ASSERT_EQ(target->getAvailableHealth(), 400);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	auto mechanics = spell->battleMechanics(&cast);
	EXPECT_EQ(mechanics->getEffectValue(), 200);
	EXPECT_EQ(mechanics->adjustEffectValue(target), 440)
		<< "60 + 1.4 x 100 Spell Power + 40% of 600 missing HP is 440";

	const SecondarySkill shadow(SecondarySkill::decode(std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL)));
	const SecondarySkill spellcraft(SecondarySkill::decode(std::string(newHorizonsMagic::SPELLCRAFT_SKILL)));
	ASSERT_TRUE(shadow.hasValue());
	ASSERT_TRUE(spellcraft.hasValue());
	attackerSideHero->setSecSkillLevel(shadow, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, soulReaperSpell()), 12650);
	spells::BattleCast rankedCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	auto rankedMechanics = spell->battleMechanics(&rankedCast);
	EXPECT_EQ(rankedMechanics->getEffectValue(), 237)
		<< "The 60 fixed damage is not scaled; the 140 Spell-Power component is floored after School x Spellcraft";
	EXPECT_EQ(rankedMechanics->adjustEffectValue(target), 477)
		<< "The 240 missing-HP component is also outside the School x Spellcraft coefficient";
}

TEST_F(NewHorizonsSoulReaperTest, TemporaryHealthCountsAsCurrentHealthAndForecastMatchesCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	damageStack(target, 600);
	target->health.addTemporaryHitPoints(200);
	ASSERT_EQ(target->getShadowGiftMaximumHealth(), 1000);
	ASSERT_EQ(target->getAvailableHealth(), 600);

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	auto mechanics = spell->battleMechanics(&cast);
	EXPECT_EQ(mechanics->adjustEffectValueBeforeExecution(target), 360)
		<< "Missing HP is measured as 1000 max minus 600 current, including temporary HP";
	EXPECT_EQ(mechanics->adjustEffectValue(target), 360);

	const auto beforeHealth = target->getAvailableHealth();
	const auto beforeCount = target->getCount();
	const auto forecast = forecastDamage(target);
	EXPECT_EQ(forecast, 360);
	EXPECT_EQ(target->getAvailableHealth(), beforeHealth)
		<< "The evaluator forecasts against a detached battle and leaves the real target unchanged";

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), actionFor(target)));
	EXPECT_EQ(beforeHealth - target->getAvailableHealth(), forecast);
	EXPECT_EQ(beforeCount - target->getCount(), 16);
	const auto casts = server.castsOf(soulReaperSpell());
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_EQ(casts.front().damage, forecast);
	EXPECT_EQ(casts.front().killed, 16u);
}

TEST_F(NewHorizonsSoulReaperTest, DoesNotExecuteJustAboveTenPercentBoundary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	damageStack(target, 499);
	spells::BattleCast aboveBoundaryCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	auto aboveBoundary = spell->battleMechanics(&aboveBoundaryCast);
	EXPECT_EQ(aboveBoundary->adjustEffectValueBeforeExecution(target), 399);
	EXPECT_EQ(aboveBoundary->adjustEffectValue(target), 399);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), actionFor(target)));
	EXPECT_EQ(target->getAvailableHealth(), 102)
		<< "At 501 max-HP, 399 damage leaves 102 HP, just above the 100-HP threshold";
}

TEST_F(NewHorizonsSoulReaperTest, ExecutesWhenPostMitigationDamageCrossesThreshold)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	damageStack(target, 700);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	ASSERT_EQ(target->getAvailableHealth(), 300);
	spells::BattleCast mitigatedCast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	auto mitigated = spell->battleMechanics(&mitigatedCast);
	EXPECT_EQ(mitigated->adjustEffectValueBeforeExecution(target), 240)
		<< "480 raw damage is reduced to 240 before checking execution";
	EXPECT_EQ(mitigated->adjustEffectValue(target), 300)
		<< "The 240 post-mitigation hit would leave 60 HP, so execution becomes lethal";

	const auto beforeCount = target->getCount();
	const auto predictedDamage = forecastDamage(target);
	EXPECT_EQ(predictedDamage, 300);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), actionFor(target)));
	EXPECT_EQ(target->getCount(), 0);
	EXPECT_EQ(target->getUnusableRemains(), 0)
		<< "Soul Reaper uses ordinary lethal damage rather than Disintegrate";
	const auto casts = server.castsOf(soulReaperSpell());
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_EQ(casts.front().damage, predictedDamage);
	EXPECT_EQ(casts.front().killed, static_cast<uint32_t>(beforeCount));
	EXPECT_TRUE(std::ranges::any_of(casts.front().logLines, [](const std::string & line)
	{
		return line.find("Soul Reaper executes") != std::string::npos
			&& line.find("6 additional creatures") != std::string::npos;
	})) << "The combat log separates mitigated damage from the six threshold-executed creatures";
}

TEST_F(NewHorizonsSoulReaperTest, LethalThresholdDamageStillProcessesOrdinaryRebirth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	damageStack(target, 500);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));

	const auto predictedNetHealthChange = forecastDamage(target);
	EXPECT_EQ(predictedNetHealthChange, -500)
		<< "The hypothetical cast deals 500 damage, then ordinary Rebirth restores the target's original health";
	const auto healthBeforeCast = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), actionFor(target)));
	EXPECT_TRUE(target->alive());
	EXPECT_EQ(target->getCount(), 100)
		<< "The ordinary CStack damage path permits the target's normal Rebirth";
	EXPECT_EQ(healthBeforeCast - target->getAvailableHealth(), predictedNetHealthChange);
	EXPECT_EQ(target->getUnusableRemains(), 0);
	EXPECT_TRUE(std::ranges::any_of(server.injuries, [](const auto & injury)
	{
		return std::ranges::any_of(injury.stacks, [](const auto & hit)
		{
			return hit.willRebirth();
		});
	}));
	const auto casts = server.castsOf(soulReaperSpell());
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_EQ(casts.front().damage, 500)
		<< "Combat cast damage is recorded before Rebirth's restoration";
	EXPECT_EQ(casts.front().killed, 50u);
}

TEST_F(NewHorizonsSoulReaperTest, LegacyAndV2SavedProfilesDoNotEnableNewSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = soulReaperSpell();
	const auto currentRules = battle()->getMagicRules();
	ASSERT_TRUE(newHorizonsMagic::soulReaperEnabled(currentRules, spell));

	JsonNode savedV2 = currentRules;
	savedV2["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	EXPECT_FALSE(newHorizonsMagic::soulReaperEnabled(savedV2, spell));
	EXPECT_FALSE(newHorizonsMagic::soulReaperMissingHealthDamage(savedV2, spell, 1000, 400));
	EXPECT_FALSE(newHorizonsMagic::soulReaperEnabled(JsonNode(), spell));
	EXPECT_FALSE(newHorizonsMagic::soulReaperMissingHealthDamage(JsonNode(), spell, 1000, 400));

	JsonNode missingRow = currentRules;
	missingRow["spells"].Struct().erase(std::string(newHorizonsMagic::SHADOW_SOUL_REAPER_SPELL));
	EXPECT_FALSE(newHorizonsMagic::soulReaperEnabled(missingRow, spell));
	EXPECT_FALSE(newHorizonsMagic::soulReaperMissingHealthDamage(missingRow, spell, 1000, 400));
}

TEST_F(NewHorizonsSoulReaperTest, V2SnapshotContainingNewRowIsRejectedByAuthoritativeCastGate)
{
	savedV2SnapshotWithSoulReaper = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto rules = battle()->getMagicRules();
	ASSERT_TRUE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), soulReaperSpell()));
	EXPECT_FALSE(newHorizonsMagic::soulReaperEnabled(rules, soulReaperSpell()));

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	auto mechanics = spell->battleMechanics(&cast);
	spells::detail::ProblemImpl problem;
	EXPECT_FALSE(mechanics->canBeCast(problem));

	const auto healthBefore = target->getAvailableHealth();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), actionFor(target)));
	EXPECT_EQ(target->getAvailableHealth(), healthBefore);
	EXPECT_TRUE(server.castsOf(soulReaperSpell()).empty());
}
