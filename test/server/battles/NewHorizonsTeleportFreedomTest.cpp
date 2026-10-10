/*
 * NewHorizonsTeleportFreedomTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/Problem.h"
#include <vcmi/Environment.h>

namespace
{
class TeleportPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit TeleportPredictionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsTeleportFreedomTest : public HeroCommandFixture
{
protected:
	bool omitPolicy = false;
	bool enablePolicy = true;
	CStack * traveler = nullptr;
	BattleHex destination;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		// The positive path exercises the shipped captured policy, not a private
		// admission override. Compatibility paths remove/disable only this key.
		if(omitPolicy)
			rules["spells"]["core:teleport"].Struct().erase("ignoreInterveningBarriers");
		else if(!enablePolicy)
			rules["spells"]["core:teleport"]["ignoreInterveningBarriers"].Bool() = false;
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(int rank = 0)
	{
		startGame(true);
		const auto & row = attackerSideHero->getMagicRules()["spells"]["core:teleport"];
		if(!omitPolicy && enablePolicy)
		{
			ASSERT_TRUE(row["ignoreInterveningBarriers"].isBool());
			ASSERT_TRUE(row["ignoreInterveningBarriers"].Bool());
		}
		const SecondarySkill sorcery(SecondarySkill::decode("new-horizons:sorceryMagic"));
		ASSERT_TRUE(sorcery.hasValue());
		attackerSideHero->setSecSkillLevel(sorcery, rank, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::TELEPORT);
		setTestSpellPointTotal(attackerSideHero, 1000);
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		ASSERT_GT(towns.front()->fortificationsLevel().wallsHealth, 0);
		startBattle(towns.front());
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		traveler = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
		addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(2, 2), 1);
		addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(15, 8), 30);
		const auto accessibility = battle()->getAccessibility(traveler);
		for(int y = 1; y < 10 && !destination.isValid(); ++y)
			for(int x = 13; x < 16; ++x)
			{
				const BattleHex hex(x, y);
				if(accessibility.accessible(hex, traveler)
					&& battle()->battleHasPenaltyOnLine(traveler->getPosition(), hex, true, false))
				{
					destination = hex;
					break;
				}
			}
		ASSERT_TRUE(destination.isAvailable());
		ASSERT_TRUE(battle()->battleHasPenaltyOnLine(traveler->getPosition(), destination, true, false));
		beginCombat();
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}

	bool castAt(BattleHex hex)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::TELEPORT;
		action.aimToUnit(traveler);
		action.aimToHex(hex);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void expectRejected(BattleHex hex)
	{
		const auto before = traveler->save();
		const auto mana = attackerSideHero->getManaAvailable();
		const auto actions = server.startedActions.size();
		EXPECT_FALSE(castAt(hex));
		EXPECT_EQ(traveler->save(), before);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
		EXPECT_EQ(server.startedActions.size(), actions);
	}
};

TEST_F(NewHorizonsTeleportFreedomTest, UntrainedPaidCastCrossesActualStandingWallWithoutChangingHealth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto health = traveler->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto actions = server.startedActions.size();
	ASSERT_TRUE(castAt(destination));
	EXPECT_EQ(traveler->getPosition(), destination);
	EXPECT_EQ(traveler->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 12);
	EXPECT_GT(server.startedActions.size(), actions);
	EXPECT_NE(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
}

TEST_F(NewHorizonsTeleportFreedomTest, BasicDetachedDestinationMatchesPaidCastAndDoesNotMutateLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC));
	const auto original = traveler->save();
	const auto mana = attackerSideHero->getManaAvailable();
	TeleportPredictionEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle predicted(&environment, callback);
	spells::BattleCast event(&predicted, attackerSideHero, spells::Mode::HERO, SpellID(SpellID::TELEPORT).toSpell());
	const auto mechanics = event.getSpell()->battleMechanics(&event);
	spells::detail::ProblemImpl problem;
	const spells::Target aim{spells::Destination(traveler), spells::Destination(destination)};
	ASSERT_TRUE(mechanics->canBeCast(problem));
	ASSERT_TRUE(mechanics->canBeCastAt(aim, problem));
	mechanics->castEval(predicted.getServerCallback(), aim);
	const auto * projected = predicted.battleGetUnitByID(traveler->unitId());
	ASSERT_NE(projected, nullptr);
	EXPECT_EQ(projected->getPosition(), destination);
	EXPECT_EQ(projected->getAvailableHealth(), traveler->getAvailableHealth());
	EXPECT_EQ(traveler->save(), original);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(castAt(destination));
	EXPECT_EQ(traveler->getPosition(), projected->getPosition());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 12);
}

TEST_F(NewHorizonsTeleportFreedomTest, AbsentCapturedPolicyPreservesUntrainedWallRejection)
{
	omitPolicy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectRejected(destination);
}

TEST_F(NewHorizonsTeleportFreedomTest, FalseCapturedPolicyPreservesBasicWallRejection)
{
	enablePolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepare(MasteryLevel::BASIC));
	expectRejected(destination);
}

TEST_F(NewHorizonsTeleportFreedomTest, OccupiedDestinationStillRejectsWithoutCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), destination, 1);
	expectRejected(destination);
}

TEST_F(NewHorizonsTeleportFreedomTest, TimeStoppedSourceStillRejectsWithoutCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	traveler->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID()));
	expectRejected(destination);
}

TEST_F(NewHorizonsTeleportFreedomTest, SpellLockedSourceStillRejectsWithoutCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID lock(SpellID::decode("new-horizons:spellLock"));
	ASSERT_TRUE(lock.hasValue());
	auto marker = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::MAGIC_RESISTANCE,
		BonusSource::SPELL_EFFECT, 100, BonusSourceID(lock));
	marker->turnsRemain = 2;
	traveler->addNewBonus(marker);
	expectRejected(destination);
}

TEST_F(NewHorizonsTeleportFreedomTest, PolicyRequiresBooleanTeleportV3AndPresenceNeedsNewFormat)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_TRUE(rules["spells"]["core:teleport"]["ignoreInterveningBarriers"].Bool());
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_NO_THROW(newHorizonsMagic::validateTeleportBarrierSerialization(rules, true));
	EXPECT_THROW(newHorizonsMagic::validateTeleportBarrierSerialization(rules, false), std::runtime_error);
	rules["spells"]["core:teleport"]["ignoreInterveningBarriers"].Bool() = false;
	EXPECT_THROW(newHorizonsMagic::validateTeleportBarrierSerialization(rules, false), std::runtime_error);
	rules["spells"]["core:teleport"]["ignoreInterveningBarriers"].String() = "true";
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	EXPECT_THROW(newHorizonsMagic::validateTeleportBarrierSerialization(rules, true), std::runtime_error);
	rules["spells"]["core:teleport"].Struct().erase("ignoreInterveningBarriers");
	EXPECT_NO_THROW(newHorizonsMagic::validateTeleportBarrierSerialization(rules, false));
	rules["spells"]["core:haste"]["ignoreInterveningBarriers"].Bool() = true;
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
}
}
