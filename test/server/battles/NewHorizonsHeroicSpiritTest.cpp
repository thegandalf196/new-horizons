/*
 * NewHorizonsHeroicSpiritTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsHeroicSpirit.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <vcmi/Environment.h>

namespace
{
class SpiritEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SpiritEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsHeroicSpiritTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * enemy = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode chance;
		for(int i = 0; i < 10; ++i)
			chance.Vector().emplace_back(100);
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_MORALE_CHANCE, chance);
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
		// The live randomizer reads captured New Horizons Morale before legacy settings.
		auto magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		magic["morale"]["goodChance"] = chance;
		magic["morale"]["diceSize"].Integer() = 100;
		magic.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
	}
	void select(const char * perk, MasteryLevel::Type rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsHeroicSpirit::SKILL)),
			rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.skillId == newHorizonsHeroicSpirit::SKILL
					&& offers[choice].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, choice, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsHeroicSpirit::SKILL, perk));
					return;
				}
		}
		FAIL() << "No legal offer for " << perk;
	}
	void activate(BattleUnitTurnReason reason)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = actor->unitId();
		pack.reason = reason;
		gameHandler->sendAndApply(pack);
	}
	void prepare(bool selected = true)
	{
		startGame();
		if(selected)
		{
			select("new-horizons:discipline.steadfast", MasteryLevel::BASIC);
			select("new-horizons:discipline.holdFast", MasteryLevel::ADVANCED);
			select(newHorizonsHeroicSpirit::PERK, MasteryLevel::EXPERT);
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(89), 20);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 1000);
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MORALE, BonusSource::OTHER, 3, BonusSourceID()));
		beginCombat();
		activate(BattleUnitTurnReason::TURN_QUEUE);
		ASSERT_GT(battle()->battleGetMorale(actor), 0);
		ASSERT_EQ(actor->counterAttacks.total(), 1);
	}
	void earn()
	{
		ASSERT_FALSE(actor->hadMorale);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeMove(actor, BattleHex(88))));
		ASSERT_TRUE(actor->hadMorale);
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
	}
};
}

TEST_F(NewHorizonsHeroicSpiritTest, ActualEarnedMoraleGrantsAvailableRetaliationThroughImmediateExtra)
{
	prepare();
	actor->counterAttacks.use();
	ASSERT_EQ(actor->counterAttacks.available(), 0);
	earn();
	EXPECT_TRUE(actor->heroicSpiritRetaliation);
	EXPECT_FALSE(actor->heroicSpiritMoralePending);
	EXPECT_EQ(actor->counterAttacks.total(), 2);
	EXPECT_EQ(actor->counterAttacks.available(), 1);
	actor->counterAttacks.use();
	EXPECT_EQ(actor->counterAttacks.available(), 0);
}

TEST_F(NewHorizonsHeroicSpiritTest, MoraleReasonWithoutEarnedEventDoesNotGrant)
{
	prepare();
	activate(BattleUnitTurnReason::MORALE);
	EXPECT_TRUE(actor->hadMorale);
	EXPECT_FALSE(actor->heroicSpiritRetaliation);
	EXPECT_EQ(actor->counterAttacks.total(), 1);
}

TEST_F(NewHorizonsHeroicSpiritTest, ActualUnselectedPositiveMoraleHasNoPerkRetaliation)
{
	prepare(false);
	earn();
	EXPECT_FALSE(actor->heroicSpiritRetaliation);
	EXPECT_EQ(actor->counterAttacks.total(), 1);
}

TEST_F(NewHorizonsHeroicSpiritTest, ContinuationsPreserveButFollowingNormalActivationExpiresWithoutCacheLatch)
{
	prepare();
	earn();
	ASSERT_EQ(actor->counterAttacks.total(), 2); // Prime ordinary cap cache.
	for(const auto reason : {BattleUnitTurnReason::HERO_COMMAND, BattleUnitTurnReason::HERO_SPELLCAST,
		BattleUnitTurnReason::UNIT_SPELLCAST, BattleUnitTurnReason::ACTION_REJECTED,
		BattleUnitTurnReason::MASTER_GATE_CONTINUATION, BattleUnitTurnReason::PURSUIT_CONTINUATION,
		BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION})
	{
		activate(reason);
		EXPECT_TRUE(actor->heroicSpiritRetaliation);
	}
	activate(BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(actor->heroicSpiritRetaliation);
	EXPECT_EQ(actor->counterAttacks.total(), 1);
}

TEST_F(NewHorizonsHeroicSpiritTest, IndependentExtraActivationExpires)
{
	prepare();
	earn();
	activate(BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION);
	EXPECT_FALSE(actor->heroicSpiritRetaliation);
	EXPECT_EQ(actor->counterAttacks.total(), 1);
}

TEST_F(NewHorizonsHeroicSpiritTest, OnlyTheGrantingMoraleActivationIsExempt)
{
	prepare();
	earn();
	ASSERT_FALSE(actor->heroicSpiritMoralePending);
	activate(BattleUnitTurnReason::MORALE);
	EXPECT_FALSE(actor->heroicSpiritRetaliation);
	EXPECT_EQ(actor->counterAttacks.total(), 1);
}

TEST_F(NewHorizonsHeroicSpiritTest, RepeatedGrantDoesNotStackAndRoundResetPreservesOrdinaryConsumption)
{
	prepare();
	earn();
	auto state = actor->acquireState();
	state->hadMorale = false; // Isolated already-earned event idempotence control.
	ASSERT_TRUE(newHorizonsHeroicSpirit::grantEarnedMorale(*state, attackerSideHero));
	ASSERT_TRUE(newHorizonsHeroicSpirit::grantEarnedMorale(*state, attackerSideHero));
	EXPECT_EQ(state->counterAttacks.total(), 2);
	actor->counterAttacks.use();
	actor->counterAttacks.use();
	EXPECT_EQ(actor->counterAttacks.available(), 0);
	advanceRound();
	EXPECT_EQ(actor->counterAttacks.available(), 2);
	activate(BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(actor->counterAttacks.available(), 1);
}

TEST_F(NewHorizonsHeroicSpiritTest, NoRetaliationIncapacityAndDeathRemainAuthoritative)
{
	prepare();
	earn();
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::NO_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(actor->counterAttacks.total(), 0);
	auto state = actor->acquireState();
	int64_t lethalDamage = state->getAvailableHealth();
	state->damage(lethalDamage);
	EXPECT_FALSE(state->alive());
	EXPECT_FALSE(state->isGhost());
	EXPECT_FALSE(state->heroicSpiritRetaliation);
	EXPECT_FALSE(state->heroicSpiritMoralePending);
	int64_t restoration = actor->getAvailableHealth();
	state->heal(restoration, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	EXPECT_TRUE(state->alive());
	EXPECT_FALSE(state->heroicSpiritRetaliation);
	EXPECT_FALSE(state->heroicSpiritMoralePending);
	state->makeGhost();
	EXPECT_FALSE(state->heroicSpiritRetaliation);
	EXPECT_FALSE(state->alive());
}

TEST_F(NewHorizonsHeroicSpiritTest, DetachedEarnedProjectionCopiesAndExpiresWithoutLiveOrSiblingMutation)
{
	prepare();
	SpiritEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	ASSERT_TRUE(child->projectEarnedMoraleActivation(actor->unitId()));
	EXPECT_EQ(child->getForUpdate(actor->unitId())->counterAttacks.total(), 2);
	EXPECT_TRUE(child->getForUpdate(actor->unitId())->hadMorale);
	EXPECT_EQ(parent->getForUpdate(actor->unitId())->counterAttacks.total(), 1);
	EXPECT_EQ(sibling->getForUpdate(actor->unitId())->counterAttacks.total(), 1);
	EXPECT_FALSE(actor->heroicSpiritRetaliation);
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::UNIT_SPELLCAST);
	EXPECT_TRUE(child->getForUpdate(actor->unitId())->heroicSpiritRetaliation);
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION);
	EXPECT_FALSE(child->getForUpdate(actor->unitId())->heroicSpiritRetaliation);
	EXPECT_EQ(child->getForUpdate(actor->unitId())->counterAttacks.total(), 1);
}

TEST_F(NewHorizonsHeroicSpiritTest, StasisQueuePassDoesNotExpireAndCurrentJsonRejectsMalformedBeforeMutation)
{
	prepare();
	earn();
	Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	SetStackEffect pack;
	pack.battleID = BattleID(0);
	pack.toAdd.emplace_back(actor->unitId(), std::vector<Bonus>{stopped});
	gameHandler->sendAndApply(pack);
	activate(BattleUnitTurnReason::AUTOMATIC_ACTION);
	EXPECT_TRUE(actor->heroicSpiritRetaliation);
	auto state = actor->acquireState();
	auto snapshot = state->save();
	snapshot["state"]["heroicSpiritRetaliation"].String() = "not a boolean";
	const auto before = state->save();
	EXPECT_THROW(state->load(snapshot), std::runtime_error);
	EXPECT_EQ(state->save(), before);
	snapshot = before;
	snapshot["state"]["heroicSpiritRetaliation"].Bool() = false;
	snapshot["state"]["heroicSpiritMoralePending"].Bool() = true;
	EXPECT_THROW(state->load(snapshot), std::runtime_error);
	EXPECT_EQ(state->save(), before);
	auto legacy = before;
	legacy["state"].Struct().erase("heroicSpiritRetaliation");
	ASSERT_NO_THROW(state->load(legacy));
	EXPECT_FALSE(state->heroicSpiritRetaliation);
}

TEST_F(NewHorizonsHeroicSpiritTest, CurrentStackJsonRoundTripAndOldEnclosingPrefixes)
{
	prepare();
	earn();
	auto copy = actor->acquireState();
	auto snapshot = actor->save();
	copy->heroicSpiritRetaliation = false;
	copy->load(snapshot);
	EXPECT_TRUE(copy->heroicSpiritRetaliation);
	EXPECT_EQ(copy->counterAttacks.available(), actor->counterAttacks.available());
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & *actor);
	CStack restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_TRUE(restored.heroicSpiritRetaliation);
	EXPECT_FALSE(restored.heroicSpiritMoralePending);
	restored.localInit(gameState()->getBattle(BattleID(0)));
	EXPECT_TRUE(restored.heroicSpiritRetaliation);
	EXPECT_EQ(restored.counterAttacks.total(), 2);
	const auto reject = [](const auto & value)
	{
		CMemorySerializer bytes;
		bytes.oser.version = ESerializationVersion::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES;
		EXPECT_THROW(bytes.oser & value, std::runtime_error);
		EXPECT_TRUE(bytes.extractBuffer().empty());
	};
	UnitChanges change(actor->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = snapshot;
	reject(change);
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	update.changedStacks = {change};
	reject(update);
	reject(*actor);
	reject(*battle());
	CGameState callback;
	callback.preInit(LIBRARY);
	BattleStart start;
	start.battleID = BattleID(0);
	start.info = std::make_unique<BattleInfo>(&callback);
	start.info->stacks.push_back(std::make_unique<CStack>());
	start.info->stacks.back()->heroicSpiritRetaliation = true;
	reject(start);
	change.data["state"].Struct().erase("heroicSpiritRetaliation");
	change.data["state"].Struct().erase("heroicSpiritMoralePending");
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES;
	legacy.iser.version = ESerializationVersion::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES;
	ASSERT_NO_THROW(legacy.oser & change);
	UnitChanges oldRestored;
	ASSERT_NO_THROW(legacy.iser & oldRestored);
	EXPECT_FALSE(battle::hasHeroicSpiritState(oldRestored.data));

	BattleTriggerEffect grant;
	grant.battleID = BattleID(0);
	grant.stackID = actor->unitId();
	grant.effect = BonusType::MORALE;
	grant.val = 1;
	grant.heroicSpiritGrant = true;
	reject(grant);
	CMemorySerializer eventBytes;
	eventBytes.oser.version = ESerializationVersion::CURRENT;
	eventBytes.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(eventBytes.oser & grant);
	BattleTriggerEffect eventRestored;
	ASSERT_NO_THROW(eventBytes.iser & eventRestored);
	EXPECT_TRUE(eventRestored.heroicSpiritGrant);
	EXPECT_EQ(eventRestored.effect, BonusType::MORALE);
	EXPECT_EQ(eventRestored.stackID, actor->unitId());
	grant.heroicSpiritGrant = false;
	CMemorySerializer oldEvent;
	oldEvent.oser.version = ESerializationVersion::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES;
	oldEvent.iser.version = ESerializationVersion::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES;
	ASSERT_NO_THROW(oldEvent.oser & grant);
	eventRestored.heroicSpiritGrant = true;
	ASSERT_NO_THROW(oldEvent.iser & eventRestored);
	EXPECT_FALSE(eventRestored.heroicSpiritGrant);
	const auto beforeInvalid = actor->save();
	grant.heroicSpiritGrant = true;
	// The real granting activation already set hadMorale: a stale repeat is invalid.
	EXPECT_THROW(gameHandler->sendAndApply(grant), std::runtime_error);
	EXPECT_EQ(actor->save(), beforeInvalid);
	grant.effect = BonusType::HP_REGENERATION;
	EXPECT_THROW(gameHandler->sendAndApply(grant), std::runtime_error);
	EXPECT_EQ(actor->save(), beforeInvalid);
	CMemorySerializer invalidBytes;
	invalidBytes.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(invalidBytes.oser & grant, std::runtime_error);
	EXPECT_TRUE(invalidBytes.extractBuffer().empty());
	grant.effect = BonusType::MORALE;
	grant.val = 0;
	EXPECT_THROW(gameHandler->sendAndApply(grant), std::runtime_error);
	EXPECT_EQ(actor->save(), beforeInvalid);
	grant.val = 1;
	grant.stackID = enemy->unitId(); // Current controller has no eligible owning hero.
	const auto enemyBefore = enemy->save();
	EXPECT_THROW(gameHandler->sendAndApply(grant), std::runtime_error);
	EXPECT_EQ(enemy->save(), enemyBefore);
}
