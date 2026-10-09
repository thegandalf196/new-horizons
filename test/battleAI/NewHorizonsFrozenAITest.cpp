/*
 * NewHorizonsFrozenAITest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/CSpell.h"

namespace
{
class FrozenEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit FrozenEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class FrozenCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	FrozenCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class NewHorizonsFrozenAITest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare()
	{
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
		startGame();
		startBattle();
		BattleNextRound round;
		round.battleID = BattleID(0);
		gameHandler->sendAndApply(round);
		ASSERT_EQ(newHorizonsFrozen::chancePercent(battle()->getMagicRules()), 20);
	}

	void freeze(CStack * unit)
	{
		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{newHorizonsFrozen::makeFrozenMarker(
			BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound())});
		gameHandler->sendAndApply(effect);
	}
};

TEST_F(NewHorizonsFrozenAITest, ShatterCandidateAndReplayPreservePrehitRetaliationVetoAndIsolation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 20);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 500);
	freeze(target);
	FrozenEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, model);
	const auto before = target->getAvailableHealth();
	const auto stamp = target->frozenLastAppliedRound();
	DamageCache cache;
	const auto frozenDamage = cache.getDamage(model->getForUpdate(source->unitId()).get(),
		model->getForUpdate(target->unitId()).get(), model);
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(model->getForUpdate(source->unitId()).get(),
		model->getForUpdate(target->unitId()).get(), 0, false), source->getPosition(), cache, model);
	ASSERT_NE(prediction.effectPreview, nullptr);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*prediction.effectPreview->getForUpdate(target->unitId())));
	EXPECT_LT(prediction.frozenControlValue, 0);
	EXPECT_FALSE(std::ranges::any_of(prediction.fortuneStrikes, [](const auto & strike) { return strike.retaliation; }));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*model->getForUpdate(target->unitId())));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*sibling->getForUpdate(target->unitId())));
	BattleExchangeVariant exchange;
	exchange.trackAttack(prediction, model, cache);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*model->getForUpdate(target->unitId())));
	EXPECT_EQ(model->getForUpdate(target->unitId())->frozenLastAppliedRound(), stamp);
	const BattleAttackInfo thawedAttack(model->getForUpdate(source->unitId()).get(),
		model->getForUpdate(target->unitId()).get(), 0, false);
	EXPECT_EQ(cache.getDamage(thawedAttack.attacker, thawedAttack.defender, model),
		model->battleExpectedLuckDamage(thawedAttack));
	EXPECT_LE(cache.getDamage(thawedAttack.attacker, thawedAttack.defender, model), frozenDamage);
	EXPECT_EQ(target->getAvailableHealth(), before);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*sibling->getForUpdate(target->unitId())));
}

TEST_F(NewHorizonsFrozenAITest, FreezingTouchHasExpectedControlWithoutInventingGuaranteedStateOrHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * ice = addStack(BattleSide::ATTACKER, creatureByName("core:iceElemental"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 500);
	FrozenEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(model->getForUpdate(ice->unitId()).get(),
		model->getForUpdate(target->unitId()).get(), 0, false), ice->getPosition(), cache, model);
	ASSERT_NE(prediction.effectPreview, nullptr);
	EXPECT_GT(prediction.frozenControlValue, 0);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*prediction.effectPreview->getForUpdate(target->unitId())));
	EXPECT_EQ(prediction.effectPreview->getForUpdate(target->unitId())->frozenLastAppliedRound(), -1);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), -1);
	const auto repeated = AttackPossibility::evaluate(BattleAttackInfo(model->getForUpdate(ice->unitId()).get(),
		model->getForUpdate(target->unitId()).get(), 0, false), ice->getPosition(), cache, model);
	EXPECT_FLOAT_EQ(prediction.frozenControlValue, repeated.frozenControlValue);
}

TEST_F(NewHorizonsFrozenAITest, ExtrasDoNotThawButNormalSlotForfeitsAndRetainsRoundReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 50);
	freeze(target);
	FrozenEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, model);
	model->nextTurn(target->unitId(), BattleUnitTurnReason::MORALE);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*model->getForUpdate(target->unitId())));
	model->nextTurn(target->unitId(), BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*model->getForUpdate(target->unitId())));
	model->nextTurn(target->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*model->getForUpdate(target->unitId())));
	EXPECT_TRUE(model->getForUpdate(target->unitId())->moved());
	EXPECT_EQ(model->getForUpdate(target->unitId())->frozenLastAppliedRound(), battle()->getRound());
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*sibling->getForUpdate(target->unitId())));
}

TEST_F(NewHorizonsFrozenAITest, CureChoosesAndSubmitsPhysicalFrozenSelectorWithoutMutatingLiveState)
{
	// Isolate this paid-spell consumer from competing legal Orders, as in the
	// existing Cure AI fixture. Current v3 magic/Frozen rules remain captured.
	useCommands = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	for(const auto spell : attackerSideHero->getSpellsInSpellbook())
		attackerSideHero->removeSpellFromSpellbook(spell);
	attackerSideHero->addSpellToSpellbook(SpellID::CURE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(4, 5), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 5), 10);
	freeze(ally);
	Bonus immobile;
	immobile.type = BonusType::STACKS_SPEED;
	immobile.duration = BonusDuration::ONE_BATTLE;
	immobile.val = -active->getMovementRange();
	active->addNewBonus(std::make_shared<Bonus>(immobile));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = active->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	auto environment = std::make_shared<FrozenEnvironment>(gameState());
	auto callback = std::make_shared<FrozenCallback>();
	callback->onBattleStarted(battle());
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.attemptCastingSpell(active));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto & action = callback->submitted.front();
	EXPECT_EQ(action.spell, SpellID::CURE);
	EXPECT_EQ(action.spellCureAffliction, SpellID::NONE);
	EXPECT_EQ(action.spellCurePhysicalAffliction, "frozen");
	const auto selected = action.getTarget(battle());
	ASSERT_EQ(selected.size(), 1u);
	EXPECT_EQ(selected.front().unitValue, ally);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*ally));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1000);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*ally));
	EXPECT_EQ(ally->frozenLastAppliedRound(), battle()->getRound());
}
