/*
	* NewHorizonsMoraleActivationTest.cpp, part of VCMI engine
	* License: GNU General Public License v2.0 or later; see license.txt.
	*/
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CStack.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#endif

namespace
{
class NewHorizonsMoraleActivationTest : public BattleTestFixture
{
protected:
	int percent = 75;
	CStack * actor = nullptr;
	CStack * target = nullptr;
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::COMBAT_MORALE_EXTRA_DAMAGE_PERCENT, JsonNode(percent));
		JsonNode curve;
		for(int i = 0; i < 10; ++i) curve.Vector().emplace_back(100);
		map->overrideGameSetting(EGameSettings::COMBAT_GOOD_MORALE_CHANCE, curve);
		// Current saved Morale curves take precedence over the legacy option.
		// Use the real captured producer with a deterministic local positive curve.
		JsonNode rules = map->getSettings().getValue(EGameSettings::MAGIC_NEW_HORIZONS);
		if(rules["morale"].isStruct())
		{
			for(auto & chance : rules["morale"]["goodChance"].Vector())
				chance.Integer() = rules["morale"]["diceSize"].Integer();
			map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
		}
	}
	void prepare(const std::string & actorCreature = "core:griffin", BattleHex targetPosition = BattleHex(rightHex))
	{
		startGame(); startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName(actorCreature), BattleHex(leftHex), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), targetPosition, 10000);
		forceMaximumDamage(actor);
		blockRetaliation(actor); blockRetaliation(target);
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::MORALE, BonusSource::OTHER, 10, BonusSourceID()));
		beginCombat();
	}
	void activate(BattleUnitTurnReason reason)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0); pack.stack = actor->unitId(); pack.reason = reason;
		gameHandler->sendAndApply(pack);
	}
};
#if ENABLE_BATTLE_AI
class MoraleEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MoraleEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}

TEST_F(NewHorizonsMoraleActivationTest, GenuineMoraleActivationScalesDirectAttackDamage)
{
	prepare();
	BattleAttackInfo attackInfo(actor, target, 0, false);
	const auto full = battle()->calculateDmgRange(attackInfo).damage.max;
	activate(BattleUnitTurnReason::MORALE);
	ASSERT_TRUE(actor->moraleExtraActivation);
	EXPECT_TRUE(actor->hadMorale);
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(attackInfo), 75);
	EXPECT_EQ(battle()->calculateDmgRange(attackInfo).damage.max, full * 75 / 100);
	attackInfo.physicalDamage = false;
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(attackInfo), 75);
	EXPECT_EQ(battle()->battleGetActivationOutputPercent(actor), 100); // Healing/effects keep their existing output.
}

TEST_F(NewHorizonsMoraleActivationTest, RetaliationAndBraceAreNotReduced)
{
	prepare(); activate(BattleUnitTurnReason::MORALE);
	BattleAttackInfo attackInfo(actor, target, 0, false);
	attackInfo.retaliation = true;
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(attackInfo), 100);
	attackInfo.retaliation = false; attackInfo.bracePreemptive = true;
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(attackInfo), 100);
	battle()->activeStack = target->unitId();
	attackInfo.bracePreemptive = false;
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(attackInfo), 100);
}

TEST_F(NewHorizonsMoraleActivationTest, NormalActivationClearsOriginButNotRoundHistory)
{
	prepare(); activate(BattleUnitTurnReason::MORALE);
	ASSERT_TRUE(actor->hadMorale);
	activate(BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(actor->moraleExtraActivation);
	EXPECT_TRUE(actor->hadMorale);
	BattleAttackInfo attackInfo(actor, target, 0, false);
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(attackInfo), 100);
}

TEST_F(NewHorizonsMoraleActivationTest, ContinuationsPreserveOriginAndActivationSerial)
{
	prepare(); activate(BattleUnitTurnReason::MORALE);
	const auto serial = battle()->getActivationSerial();
	for(const auto reason : {BattleUnitTurnReason::ACTION_REJECTED,
		BattleUnitTurnReason::MASTER_GATE_CONTINUATION, BattleUnitTurnReason::PURSUIT_CONTINUATION,
		BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION})
	{
		activate(reason);
		EXPECT_TRUE(actor->moraleExtraActivation);
		EXPECT_EQ(battle()->getActivationSerial(), serial);
	}
}

TEST_F(NewHorizonsMoraleActivationTest, LegacyCapturedPercentPreservesFullMoraleOutput)
{
	percent = 100; prepare(); activate(BattleUnitTurnReason::MORALE);
	EXPECT_EQ(battle()->getMoraleExtraDamagePercent(), 100);
	EXPECT_FALSE(actor->moraleExtraActivation);
	BattleAttackInfo attackInfo(actor, target, 0, false);
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(attackInfo), 100);
}

TEST_F(NewHorizonsMoraleActivationTest, ActualEarnedMoraleAttackUsesReducedOutputAndCannotRepeat)
{
	prepare();
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), actor->unitId());
	ASSERT_FALSE(battle()->battleIsFinished().has_value());
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(attack(actor, target->getPosition()));
	const auto firstDamage = before - target->getAvailableHealth();
	ASSERT_GT(firstDamage, 0);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), actor->unitId());
	ASSERT_TRUE(actor->moraleExtraActivation);
	const auto activationCount = server.stackActivations.size();
	const auto beforeExtra = target->getAvailableHealth();
	ASSERT_TRUE(attack(actor, target->getPosition()));
	EXPECT_EQ(beforeExtra - target->getAvailableHealth(), firstDamage * 75 / 100);
	EXPECT_FALSE(battle()->battleIsMoraleExtraActivation(actor));
	for(size_t i = activationCount; i < server.stackActivations.size(); ++i)
		EXPECT_FALSE(server.stackActivations[i].stack == actor->unitId()
			&& server.stackActivations[i].reason == BattleUnitTurnReason::MORALE);
}

TEST_F(NewHorizonsMoraleActivationTest, NativeMagogSpellLikeShotProjectsAndResolvesReducedEarnedMoraleDamage)
{
	prepare("core:magog", BattleHex(14, 5));
	ASSERT_TRUE(actor->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(battle()->battleCanShootAction(actor, target->getPosition()));
	BattleAttackInfo shot(actor, target, 0, true);
	ASSERT_FALSE(shot.physicalDamage); // Actual native ability classification, not a manually forged flag.
	const auto ordinary = battle()->calculateDmgRange(shot).damage;
	ASSERT_EQ(ordinary.min, ordinary.max);
	ASSERT_GT(ordinary.max, 0);
	const auto healthBefore = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(actor, target)));
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), ordinary.max);
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(actor->moraleExtraActivation);
	const auto reduced = battle()->calculateDmgRange(shot).damage;
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(shot), 75);
	EXPECT_EQ(reduced.max, ordinary.max * 75 / 100);
	EXPECT_EQ(battle()->battleGetActivationOutputPercent(actor), 100); // Healing/effects are not this attack payload.
#if ENABLE_BATTLE_AI
	MoraleEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projected(&environment, callback);
	auto projectedActor = projected.getForUpdate(actor->unitId());
	auto projectedTarget = projected.getForUpdate(target->unitId());
	BattleAttackInfo projectedShot(projectedActor.get(), projectedTarget.get(), 0, true);
	ASSERT_FALSE(projectedShot.physicalDamage);
	EXPECT_EQ(projected.calculateDmgRange(projectedShot).damage.max, reduced.max);
	EXPECT_TRUE(actor->moraleExtraActivation);
#endif
	const auto healthBeforeExtra = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(actor, target)));
	EXPECT_EQ(healthBeforeExtra - target->getAvailableHealth(), reduced.max);
	EXPECT_FALSE(battle()->battleIsMoraleExtraActivation(actor));
}

TEST_F(NewHorizonsMoraleActivationTest, JsonAdmissionRejectsMalformedWithoutChangingStateAndAbsentDefaultsFalse)
{
	prepare(); activate(BattleUnitTurnReason::MORALE);
	auto snapshot = actor->save();
	snapshot["state"]["moraleExtraActivation"] = JsonNode(1);
	EXPECT_THROW(actor->load(snapshot), std::runtime_error);
	EXPECT_TRUE(actor->moraleExtraActivation);
	snapshot = actor->save();
	snapshot["state"].Struct().erase("moraleExtraActivation");
	ASSERT_NO_THROW(actor->load(snapshot));
	EXPECT_FALSE(actor->moraleExtraActivation);
}

TEST_F(NewHorizonsMoraleActivationTest, CurrentStackBinaryAndRebindPreserveProvenance)
{
	prepare();
	CStackBasicDescriptor descriptor(creatureByName("core:griffin"), 3);
	CStack source(&descriptor, PlayerColor(0), 70, BattleSide::ATTACKER);
	source.initialPosition = BattleHex(leftHex);
	source.hadMorale = true; source.moraleExtraActivation = true;
	CMemorySerializer memory;
	ASSERT_NO_THROW(source.serialize(memory.oser));
	CStack restored;
	ASSERT_NO_THROW(restored.serialize(memory.iser));
	EXPECT_TRUE(restored.moraleExtraActivation);
	restored.localInit(battle());
	EXPECT_TRUE(restored.moraleExtraActivation);
	EXPECT_TRUE(restored.hadMorale);
	restored.detachFromAll();
}

TEST_F(NewHorizonsMoraleActivationTest, OldStackWriterRejectsBeforePrefixAndOrdinaryReaderResetsReusedOrigin)
{
	prepare();
	CStackBasicDescriptor descriptor(creatureByName("core:griffin"), 3);
	CStack source(&descriptor, PlayerColor(0), 70, BattleSide::ATTACKER);
	source.hadMorale = true; source.moraleExtraActivation = true;
	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_IMPLOSION;
	EXPECT_THROW(source.serialize(rejected.oser), std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());
	source.moraleExtraActivation = false;
	CMemorySerializer ordinary;
	ordinary.oser.version = ordinary.iser.version = ESerializationVersion::NEW_HORIZONS_IMPLOSION;
	ASSERT_NO_THROW(source.serialize(ordinary.oser));
	CStack restored; restored.hadMorale = true; restored.moraleExtraActivation = true;
	ASSERT_NO_THROW(restored.serialize(ordinary.iser));
	EXPECT_FALSE(restored.moraleExtraActivation);
}

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsMoraleActivationTest, DetachedBranchesRetainCapturedOriginAndIsolateNormalReset)
{
	prepare(); activate(BattleUnitTurnReason::MORALE);
	MoraleEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	auto childActor = child->getForUpdate(actor->unitId());
	auto childTarget = child->getForUpdate(target->unitId());
	BattleAttackInfo attackInfo(childActor.get(), childTarget.get(), 0, false);
	EXPECT_EQ(child->battleGetDirectActivationOutputPercent(attackInfo), 75);
	EXPECT_TRUE(sibling->getForUpdate(actor->unitId())->moraleExtraActivation);
	EXPECT_TRUE(actor->moraleExtraActivation);
	EXPECT_FALSE(parent->getForUpdate(actor->unitId())->moraleExtraActivation);
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(childActor->moraleExtraActivation);
	EXPECT_TRUE(sibling->getForUpdate(actor->unitId())->moraleExtraActivation);
	EXPECT_TRUE(actor->moraleExtraActivation);
}
#endif

TEST_F(NewHorizonsMoraleActivationTest, RangedPreviewUsesSameFinalPhysicalCoefficient)
{
	prepare();
	BattleAttackInfo ranged(actor, target, 0, true);
	ASSERT_TRUE(ranged.physicalDamage);
	ASSERT_EQ(actor->getCount(), 100);
	ASSERT_EQ(actor->getMaxDamage(true), 7);
	ASSERT_EQ(actor->getAttack(true), 9);
	ASSERT_EQ(target->getDefense(true), 1);
	ASSERT_DOUBLE_EQ(LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_ATTACK_POINT_DAMAGE_FACTOR), 0.05);
	ASSERT_TRUE(actor->getSurroundingHexes(actor->getPosition()).contains(target->getPosition()));
	// Independent exact stage arithmetic: base 700, Attack/Defense 140%,
	// adjacent ranged penalty 50%, and Morale 75%, with one final floor.
	// Scaling the already-floored ordinary preview would introduce a second
	// floor (its legacy floating product is just below the exact integer 490).
	const int64_t baseDamage = actor->getCount() * actor->getMaxDamage(true);
	const int64_t attackPercent = 100 + 5 * (actor->getAttack(true) - target->getDefense(true));
	const int64_t expectedDamage = baseDamage * attackPercent * 50 * 75 / (100 * 100 * 100);
	activate(BattleUnitTurnReason::MORALE);
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(ranged), 75);
	EXPECT_EQ(battle()->calculateDmgRange(ranged).damage.max, expectedDamage);
	ranged.retaliation = true;
	EXPECT_EQ(battle()->battleGetDirectActivationOutputPercent(ranged), 100);
}

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsMoraleActivationTest, DetachedFalseOriginDoesNotImportLaterParentMoraleActivation)
{
	prepare();
	ASSERT_FALSE(actor->moraleExtraActivation);
	MoraleEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::MORALE);
	ASSERT_TRUE(parent->getForUpdate(actor->unitId())->moraleExtraActivation);
	auto childActor = child->getForUpdate(actor->unitId());
	auto childTarget = child->getForUpdate(target->unitId());
	BattleAttackInfo attackInfo(childActor.get(), childTarget.get(), 0, false);
	EXPECT_FALSE(childActor->moraleExtraActivation);
	EXPECT_EQ(child->battleGetDirectActivationOutputPercent(attackInfo), 100);
	EXPECT_FALSE(sibling->getForUpdate(actor->unitId())->moraleExtraActivation);
	EXPECT_FALSE(actor->moraleExtraActivation);
}
#endif
