/*
 * NewHorizonsSlowResolvedStatsTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsElementalTerrainFixture.h"
#include "../../spells/HistoricalCrusadeRules.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/CStack.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/battle/BattleForm.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include <vcmi/Environment.h>

namespace
{
constexpr auto sorcery = "new-horizons:sorceryMagic";
SpellID massSlow() { return SpellID(SpellID::decode("new-horizons:massSlow")); }

class SlowEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SlowEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsSlowResolvedStatsTest : public NewHorizonsElementalTerrainFixture
{
protected:
	bool historical = false;
	CStack * actor = nullptr;
	CStack * target = nullptr;

	void mapLoaded(CMap * map) override
	{
		NewHorizonsElementalTerrainFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(historical) rules = historicalCrusadeRules(std::move(rules));
		newHorizonsMagic::validateRules(rules);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(TerrainId terrain, const std::string & creature = "core:pikeman", bool withMass = false,
		bool withEnvironmentSpeed = false)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(SpellID::SLOW);
		attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		const SecondarySkill skill(SecondarySkill::decode(sorcery));
		if(withMass)
		{
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({sorcery, "new-horizons:sorceryMagic.temporalist"});
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({sorcery, "new-horizons:sorceryMagic.temporalField"});
		}
		else
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		if(withEnvironmentSpeed)
		{
			constexpr auto bloodrage = "new-horizons:bloodrage";
			const SecondarySkill defenderSkill(SecondarySkill::decode(bloodrage));
			defenderSideHero->setSecSkillLevel(defenderSkill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			defenderSideHero->applyPerkSelection({bloodrage, "new-horizons:bloodrage.warDrums"});
			defenderSideHero->setSecSkillLevel(defenderSkill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			defenderSideHero->applyPerkSelection({bloodrage, "new-horizons:bloodrage.unrelenting"});
			ASSERT_TRUE(defenderSideHero->hasActivePerk(bloodrage, "new-horizons:bloodrage.unrelenting"));
		}
		startTerrainBattle(terrain);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName(creature), BattleHex(10, 5), 100);
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(target, nullptr);
		beginCombat();
		activateActor();
		ASSERT_EQ(attackerSideHero->getEffectPower(SpellID(SpellID::SLOW).toSpell()), 0);
	}

	void activateActor()
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = actor->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	void nextRound()
	{
		advanceRound();
		activateActor();
	}

	void expectPair(const battle::Unit * unit, int speed, int initiative)
	{
		ASSERT_NE(unit, nullptr);
		EXPECT_EQ(unit->getMovementRange(), speed);
		EXPECT_EQ(unit->getInitiative(), initiative);
	}

	void expectLiveAndDetached(int speed, int initiative)
	{
		expectPair(target, speed, initiative);
		battle::ProjectedBattleWorldArena arena(*battle());
		expectPair(arena.findUnit(target->unitId()), speed, initiative);
	}

	size_t components(const battle::Unit * unit, BonusType type)
	{
		return unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::SLOW))).And(Selector::type()(type)))->size();
	}

	void projectAndCast(SpellID spell, int speed, int initiative, size_t speedCount = 1, size_t initiativeCount = 1)
	{
		const auto original = target->save();
		const auto mana = attackerSideHero->getManaAvailable();
		const bool mass = spell == massSlow();
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		SlowEnvironment environment(gameState());
		HypotheticBattle projected(&environment, callback);
		spells::BattleCast projectedCast(&projected, attackerSideHero, spells::Mode::HERO, spell.toSpell());
		const auto mechanics = spell.toSpell()->battleMechanics(&projectedCast);
		const spells::Target aim = mass ? spells::Target{spells::Destination(BattleHex::INVALID)}
			: spells::Target{spells::Destination(projected.battleGetUnitByID(target->unitId()))};
		ASSERT_TRUE(mechanics->canBeCastAt(aim));
		mechanics->castEval(projected.getServerCallback(), aim);
		const auto * result = projected.battleGetUnitByID(target->unitId());
		expectPair(result, speed, initiative);
		EXPECT_EQ(components(result, BonusType::STACKS_SPEED), speedCount);
		EXPECT_EQ(components(result, BonusType::STACKS_INITIATIVE), initiativeCount);
		EXPECT_EQ(target->save(), original);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);

		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = spell;
		if(mass) action.aimToHex(BattleHex::INVALID); else action.aimToUnit(target);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		expectLiveAndDetached(speed, initiative);
		EXPECT_EQ(components(target, BonusType::STACKS_SPEED), speedCount);
		EXPECT_EQ(components(target, BonusType::STACKS_INITIATIVE), initiativeCount);
	}

	std::shared_ptr<Bonus> independentSpeed(int value)
	{
		auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
			BonusSource::OTHER, value, BonusSourceID());
		target->addNewBonus(bonus);
		return bonus;
	}

	void installForm(CreatureID form)
	{
		auto state = target->acquireState();
		state->beginBattleForm(form, 2);
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state->save();
		gameHandler->sendAndApply(change);
		const SpellID polymorph(SpellID::decode("new-horizons:polymorph"));
		target->addNewBonus(std::make_shared<Bonus>(battle::polymorphMarker(polymorph, PlayerColor(0))));
	}
};
}

TEST_F(NewHorizonsSlowResolvedStatsTest, NativeFallbackAppliesTwentyPercentOnceToBothResolvedStats)
{
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::GRASS));
	expectLiveAndDetached(5, 5);
	projectAndCast(SpellID::SLOW, 4, 4);
}

TEST_F(NewHorizonsSlowResolvedStatsTest, NonNativeFallbackAppliesTwentyPercentOnceToBothResolvedStats)
{
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::SAND));
	expectLiveAndDetached(4, 4);
	projectAndCast(SpellID::SLOW, 3, 3);
}

TEST_F(NewHorizonsSlowResolvedStatsTest, NativeExplicitInitiativeRetainsItsSeparateBase)
{
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::SAND, "core:archMage"));
	ASSERT_EQ(target->unitType()->getBaseSpeed(), 5);
	ASSERT_EQ(target->unitType()->getBaseInitiative(), 7);
	// Native terrain supplies +1 Speed; explicit Initiative already has its own base.
	expectLiveAndDetached(6, 7);
	projectAndCast(SpellID::SLOW, 4, 5);
}

TEST_F(NewHorizonsSlowResolvedStatsTest, IndependentEnvironmentSpeedKeepsItsExistingCompositionOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::SAND, "core:pikeman", false, true));
	independentSpeed(1);
	auto & side = battle()->getSide(BattleSide::DEFENDER);
	ASSERT_EQ(side.bloodrageRank, MasteryLevel::ADVANCED);
	ASSERT_EQ(side.bloodrageCapPercent, 40);
	ASSERT_EQ(side.bloodrageSpeedBonus, 1);
	// Use the maintained Bloodrage threshold seam after setup casualties.
	side.bloodrageDamagePercent = 20;
	ASSERT_EQ(battle()->battleBloodrageSpeed(target), 1);
	expectLiveAndDetached(6, 6);
	// Movement: floor(5 * .8) + 1 = 5. Initiative: floor((5 + 1) * .8) = 4.
	projectAndCast(SpellID::SLOW, 5, 4);
}

TEST_F(NewHorizonsSlowResolvedStatsTest, IndependentInitiativePercentRemainsAdditiveWithSlow)
{
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::SAND));
	independentSpeed(9);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_INITIATIVE, BonusSource::OTHER, 18, BonusSourceID()));
	expectLiveAndDetached(13, 15);
	// floor(13 * .8) = 10; floor(13 * (100 + 18 - 20) / 100) = 12.
	projectAndCast(SpellID::SLOW, 10, 12);
}

TEST_F(NewHorizonsSlowResolvedStatsTest, OrdinaryMassOrdinaryRefreshReplacesBothResolvedComponents)
{
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::SAND, "core:pikeman", true));
	independentSpeed(2);
	expectLiveAndDetached(6, 6);
	projectAndCast(SpellID::SLOW, 4, 4);
	nextRound();
	// Mass Slow is 60% of 20% = 12%; floor(6 * .88) = 5 for each statistic.
	projectAndCast(massSlow(), 5, 5);
	nextRound();
	projectAndCast(SpellID::SLOW, 4, 4);
}

TEST_F(NewHorizonsSlowResolvedStatsTest, BothResolvedComponentsExpireAtTheSameBoundary)
{
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::SAND));
	projectAndCast(SpellID::SLOW, 3, 3);
	nextRound();
	expectLiveAndDetached(3, 3);
	nextRound();
	expectLiveAndDetached(4, 4);
	EXPECT_EQ(components(target, BonusType::STACKS_SPEED), 0u);
	EXPECT_EQ(components(target, BonusType::STACKS_INITIATIVE), 0u);
}

TEST_F(NewHorizonsSlowResolvedStatsTest, DispelRemovesBothWithoutChangingTheProjectedSource)
{
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::GRASS));
	projectAndCast(SpellID::SLOW, 4, 4);
	nextRound();
	projectAndCast(SpellID::DISPEL, 5, 5, 0, 0);
}

TEST_F(NewHorizonsSlowResolvedStatsTest, HistoricalCaptureKeepsInitiativeOnlyAndLegacySpeedTracking)
{
	historical = true;
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::GRASS));
	projectAndCast(SpellID::SLOW, 5, 4, 0, 1);
	const auto extra = independentSpeed(2);
	expectLiveAndDetached(7, 5);
	target->removeBonus(extra);
	expectLiveAndDetached(5, 4);
}

TEST_F(NewHorizonsSlowResolvedStatsTest, CurrentFallbackRequeriesIndependentSpeedAndChangedCreatureForm)
{
	ASSERT_NO_FATAL_FAILURE(prepare(TerrainId::SAND, "core:pikeman", true));
	projectAndCast(SpellID::SLOW, 3, 3);
	const auto extra = independentSpeed(3);
	expectLiveAndDetached(5, 5);
	target->removeBonus(extra);
	expectLiveAndDetached(3, 3);

	const auto form = creatureByName("core:pixie");
	ASSERT_EQ(form.toCreature()->getBaseSpeed(), 7);
	ASSERT_EQ(form.toCreature()->getBaseInitiative(), 7);
	const auto originalHealth = target->getAvailableHealth();
	installForm(form);
	ASSERT_TRUE(target->hasBattleForm());
	EXPECT_EQ(target->getAvailableHealth(), originalHealth);
	// The already-scheduled current-round activation keeps its prior initiative.
	expectLiveAndDetached(5, 3);
	nextRound();
	ASSERT_TRUE(target->hasBattleForm());
	expectLiveAndDetached(5, 5);
	nextRound();
	ASSERT_FALSE(target->hasBattleForm());
	// Temporalist keeps Slow for a third round after the two-round form ends.
	expectLiveAndDetached(3, 3);
	nextRound();
	expectLiveAndDetached(4, 4);
}
