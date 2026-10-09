/*
 * NewHorizonsSwiftRebirthTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/battle/NewHorizonsElementalRebirth.h"
#include "../../../lib/battle/NewHorizonsSwiftRebirth.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/battle/PhysicalAffliction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/mapObjects/army/CArmedInstance.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

#include <array>

namespace
{
constexpr std::string_view SKILL = "new-horizons:elementalRebirth";
constexpr std::string_view SWIFT = "new-horizons:elementalRebirth.swiftRebirth";
constexpr std::string_view CHAIN = "new-horizons:elementalRebirth.rebirthChain";

class SwiftEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SwiftEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

struct SwiftPrefixProbe
{
	using Version = ESerializationVersion;
	bool saving = true;
	bool loadingGamestate = false;
	int fields = 0;
	bool hasFeature(Version feature) const { return feature <= Version::NEW_HORIZONS_BLOODRAGE_DEATH_PERKS; }
	template<typename T> SwiftPrefixProbe & operator&(T &)
	{
		++fields;
		throw std::runtime_error("Reached packet payload");
	}
};

class NewHorizonsSwiftRebirthTest : public HeroCommandFixture
{
protected:
	CStack * source = nullptr;
	CStack * enemy = nullptr;
	CStack * reserve = nullptr;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires shipped-active New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		bool found = false;
		for(const auto & entry : perks["skills"][std::string(SKILL)]["perks"].Vector())
			if(entry["id"].String() == SWIFT)
			{
				ASSERT_EQ(entry["effect"]["status"].String(), "active");
				found = true;
			}
		ASSERT_TRUE(found);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		JsonNode morale;
		for(int i = 0; i < 10; ++i)
			morale.Vector().emplace_back(100);
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_MORALE_CHANCE, morale);
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
		// Saved New Horizons Morale curves supersede the legacy engine vectors.
		// Keep the production rules/perks, controlling only the actual roll curve.
		JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
		for(auto & chance : magic["morale"]["goodChance"].Vector())
			chance.Integer() = magic["morale"]["diceSize"].Integer();
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
	}
	void select(std::string_view perk, CGHeroInstance * hero = nullptr)
	{
		if(!hero)
			hero = defenderSideHero;
		const auto lookup = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(lookup, seed);
			for(size_t i = 0; i < offers.size(); ++i)
				if(offers[i].selection.skillId == SKILL && offers[i].selection.perkId == perk)
				{
					gameHandler->levelUpHero(hero, offers, i, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(std::string(SKILL), std::string(perk)));
					return;
				}
		}
		FAIL() << "No legal active Elemental Rebirth offer " << perk;
	}
	void prepare(bool chain = false, int sourceCount = 101)
	{
		startGame();
		defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		const SecondarySkill skill(SecondarySkill::decode(std::string(SKILL)));
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select("new-horizons:elementalRebirth.primalBurst", attackerSideHero));
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select(SWIFT));
		if(chain)
		{
			defenderSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			ASSERT_NO_FATAL_FAILURE(select(CHAIN));
		}
		// Rebirth sources must be real army slots. addStack's UnitInfo ADD uses
		// the summoned-slot placeholder even when info.summoned is false.
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:archangel"), 100));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), sourceCount));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:peasant"), 101));
		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
				enemy = battle()->getStack(unit->unitId(), false);
			if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0))
				source = battle()->getStack(unit->unitId(), false);
			if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(1))
				reserve = battle()->getStack(unit->unitId(), false);
		}
		ASSERT_NE(enemy, nullptr);
		ASSERT_NE(source, nullptr);
		ASSERT_NE(reserve, nullptr);
		// Arrange the real initial descriptors before the tactics-end boundary.
		for(const auto & [unit, hex] : std::array<std::pair<CStack *, int>, 3>{{
			{enemy, leftHex}, {source, rightHex}, {reserve, rightHex + 2 * GameConstants::BFIELD_WIDTH}}})
		{
			unit->initialPosition = BattleHex(hex);
			unit->setPosition(BattleHex(hex));
		}
		enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE,
			BonusSource::OTHER, 1, BonusSourceID()));
		beginCombat();
		ASSERT_EQ(battle()->battleActiveUnit(), enemy);
		for(const auto * unit : {enemy, source, reserve})
		{
			ASSERT_TRUE(newHorizonsElementalRebirth::isEligibleSource(*unit));
			ASSERT_GT(unit->getBattleStartMaximumAggregateHP(), 0);
		}
	}
	CStack * kill(CStack * unit)
	{
		const auto nextId = battle()->battleNextUnitId();
		StacksInjured packet;
		packet.battleID = BattleID(0);
		auto & hit = packet.stacks.emplace_back();
		hit.attackerID = enemy->unitId();
		hit.stackAttacked = unit->unitId();
		hit.damageAmount = unit->getAvailableHealth();
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), unit->acquireState());
		gameHandler->sendAndApply(packet);
		return battle()->getStack(nextId, false);
	}
	bool action(const battle::Unit * unit, bool wait = false)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(unit),
			wait ? BattleAction::makeWait(unit) : BattleAction::makeDefend(unit));
	}
	void reach(uint32_t id)
	{
		const auto round = battle()->getRound();
		for(int i = 0; i < 64 && battle()->battleActiveUnit()->unitId() != id; ++i)
			ASSERT_TRUE(action(battle()->battleActiveUnit()));
		ASSERT_EQ(battle()->getRound(), round);
		ASSERT_EQ(battle()->battleActiveUnit()->unitId(), id);
	}
	void effect(uint32_t id, const Bonus & bonus, bool update = false)
	{
		SetStackEffect packet;
		packet.battleID = BattleID(0);
		(update ? packet.toUpdate : packet.toAdd).emplace_back(id, std::vector<Bonus>{bonus});
		gameHandler->sendAndApply(packet);
	}
};
}

TEST_F(NewHorizonsSwiftRebirthTest, CapturedActiveProfileAndActualLiveDetachedSpawn)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto profile = newHorizonsElementalRebirth::activeProfile(defenderSideHero);
	ASSERT_TRUE(profile);
	EXPECT_TRUE(profile->swiftRebirth);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(std::string(SKILL), std::string(SWIFT)));
	const auto inactive = newHorizonsElementalRebirth::activeProfile(attackerSideHero);
	ASSERT_TRUE(inactive);
	EXPECT_FALSE(inactive->swiftRebirth);
	SwiftEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	auto projected = model->getForUpdate(source->unitId());
	const auto captured = model->captureElementalRebirthSource(*projected);
	ASSERT_TRUE(captured);
	auto damage = projected->getAvailableHealth();
	projected->damage(damage);
	const auto projectedId = model->projectElementalRebirth(projected.get(), *captured, true, false, false);
	ASSERT_TRUE(projectedId);
	const auto detached = newHorizonsSwiftRebirth::lifecycle(*model->battleGetUnitByID(*projectedId));
	ASSERT_TRUE(detached);
	std::vector<battle::Units> projectedQueue;
	model->battleGetTurnOrder(projectedQueue, 1, 1, -1);
	ASSERT_FALSE(projectedQueue.empty());
	ASSERT_FALSE(projectedQueue.front().empty());
	EXPECT_EQ(projectedQueue.front().front()->unitId(), *projectedId);
	auto * live = kill(source);
	ASSERT_NE(live, nullptr);
	const auto receipt = newHorizonsSwiftRebirth::lifecycle(*live);
	ASSERT_TRUE(receipt);
	EXPECT_EQ(receipt->birthRound, battle()->getRound());
	EXPECT_EQ(receipt->birthRound, detached->birthRound);
	EXPECT_TRUE(receipt->priorityPending);
	EXPECT_FALSE(receipt->normalCompleted);
	// The same active rank with a different public Basic selection must not
	// manufacture lifecycle metadata for its independently projected Rebirth.
	auto negativeCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto negativeModel = std::make_shared<HypotheticBattle>(&environment, negativeCallback);
	auto negativeSource = negativeModel->getForUpdate(enemy->unitId());
	const auto negativeSnapshot = negativeModel->captureElementalRebirthSource(*negativeSource);
	ASSERT_TRUE(negativeSnapshot);
	auto lethal = negativeSource->getAvailableHealth();
	negativeSource->damage(lethal);
	const auto ordinaryOutput = negativeModel->projectElementalRebirth(negativeSource.get(), *negativeSnapshot, true, false, false);
	ASSERT_TRUE(ordinaryOutput);
	EXPECT_FALSE(newHorizonsSwiftRebirth::lifecycle(*negativeModel->battleGetUnitByID(*ordinaryOutput)));
}

TEST_F(NewHorizonsSwiftRebirthTest, QueueKeepsCurrentActorThenStableSwiftIDsExactlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * first = kill(source);
	ASSERT_NE(first, nullptr);
	auto * second = kill(reserve);
	ASSERT_NE(second, nullptr);
	ASSERT_LT(first->unitId(), second->unitId());
	std::vector<battle::Units> queue;
	battle()->battleGetTurnOrder(queue, 0, 1, 0);
	ASSERT_GE(queue.front().size(), 3u);
	EXPECT_EQ(queue.front()[0], enemy);
	EXPECT_EQ(queue.front()[1], first);
	EXPECT_EQ(queue.front()[2], second);
	EXPECT_EQ(std::count(queue.front().begin(), queue.front().end(), first), 1);
	EXPECT_EQ(std::count(queue.front().begin(), queue.front().end(), second), 1);
	EXPECT_FALSE(newHorizonsSwiftRebirth::lifecycle(*enemy));
}

TEST_F(NewHorizonsSwiftRebirthTest, ActualEarnedMoralePrecedesSwiftWithoutDuplicatingActor)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(attack(enemy, source->getPosition()));
	ASSERT_FALSE(source->alive());
	ASSERT_TRUE(enemy->hadMorale);
	ASSERT_EQ(battle()->battleActiveUnit(), enemy);
	const auto morale = std::find_if(server.stackActivations.begin(), server.stackActivations.end(),
		[this](const BattleSetActiveStack & value)
		{ return value.stack == enemy->unitId() && value.reason == BattleUnitTurnReason::MORALE; });
	ASSERT_NE(morale, server.stackActivations.end());
	CStack * reborn = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(newHorizonsSwiftRebirth::lifecycle(*unit))
			reborn = battle()->getStack(unit->unitId(), false);
	ASSERT_NE(reborn, nullptr);
	ASSERT_TRUE(action(enemy));
	EXPECT_EQ(battle()->battleActiveUnit(), reborn);
	EXPECT_FALSE(newHorizonsSwiftRebirth::lifecycle(*reborn)->priorityPending);
}

TEST_F(NewHorizonsSwiftRebirthTest, WaitPostponesSameNormalActivationAndCompletedReceiptPreventsRepeat)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * reborn = kill(source);
	ASSERT_NE(reborn, nullptr);
	ASSERT_NO_FATAL_FAILURE(reach(reborn->unitId()));
	const auto round = battle()->getRound();
	ASSERT_TRUE(action(reborn, true));
	const auto waiting = newHorizonsSwiftRebirth::lifecycle(*reborn);
	ASSERT_TRUE(waiting);
	EXPECT_FALSE(waiting->priorityPending);
	EXPECT_FALSE(waiting->normalCompleted);
	EXPECT_TRUE(newHorizonsSwiftRebirth::blocksAdditionalActivation(*reborn, round));
	ASSERT_NO_FATAL_FAILURE(reach(reborn->unitId()));
	ASSERT_TRUE(action(reborn));
	EXPECT_TRUE(newHorizonsSwiftRebirth::normalActivationCompleted(*reborn, round));
	std::vector<battle::Units> queue;
	battle()->battleGetTurnOrder(queue, 0, 1, -1);
	if(battle()->getRound() == round)
		EXPECT_EQ(std::count(queue.front().begin(), queue.front().end(), reborn), 0);
}

TEST_F(NewHorizonsSwiftRebirthTest, BirthRoundRejectsSecondWindAndDetachedExtrasButNotContinuations)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * reborn = kill(source);
	ASSERT_NE(reborn, nullptr);
	const auto round = battle()->getRound();
	ASSERT_NO_FATAL_FAILURE(reach(reborn->unitId()));
	SwiftEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto continuing = std::make_shared<HypotheticBattle>(&environment, callback);
	for(const auto reason : {BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION,
		BattleUnitTurnReason::MASTER_GATE_CONTINUATION, BattleUnitTurnReason::PURSUIT_CONTINUATION})
	{
		continuing->nextTurn(reborn->unitId(), reason);
		EXPECT_FALSE(newHorizonsSwiftRebirth::normalActivationCompleted(*continuing->getForUpdate(reborn->unitId()), round));
		EXPECT_EQ(continuing->battleActiveUnit()->unitId(), reborn->unitId());
	}
	const auto completed = newHorizonsSwiftRebirth::completeNormalActivationPlan(*reborn, round);
	ASSERT_TRUE(completed);
	effect(reborn->unitId(), *completed, true);
	reborn->movedThisRound = true;
	ASSERT_NE(battle()->battleGetHeroOrderTargetRejection(BattleSide::DEFENDER, HeroCommand::SECOND_WIND,
		{reborn->unitId()}), heroCommands::TargetRejection::UNAVAILABLE);
	EXPECT_FALSE(battle()->battlePrepareHeroOrderState(BattleSide::DEFENDER, HeroCommand::SECOND_WIND,
		{reborn->unitId()}));
	auto order = BattleAction::makeTargetedHeroCommand(BattleSide::DEFENDER, HeroCommand::SECOND_WIND, reborn->unitId());
	const auto before = battle()->getHeroActionAllowances(BattleSide::DEFENDER);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), order));
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::DEFENDER), before);
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	for(const auto reason : {BattleUnitTurnReason::MORALE, BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION})
	{
		model->nextTurn(reborn->unitId(), reason);
		EXPECT_TRUE(model->getForUpdate(reborn->unitId())->movedThisRound);
	}
	model->nextTurn(reborn->unitId(), BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION);
	EXPECT_TRUE(newHorizonsSwiftRebirth::normalActivationCompleted(*model->getForUpdate(reborn->unitId()), round));
	EXPECT_EQ(model->battleActiveUnit()->unitId(), reborn->unitId());
}

TEST_F(NewHorizonsSwiftRebirthTest, FrozenAndTimeStopForfeitPersistCompletionWithoutDuplicates)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const bool stopped : {false, true})
	{
		auto * victim = stopped ? reserve : source;
		// Frozen's preceding automatic forfeiture can hand control to reserve.
		// Finish that accepted activation before destroying it as a later source;
		// an injury packet alone does not advance a now-dead current actor.
		if(battle()->battleActiveUnit() == victim)
			ASSERT_TRUE(action(victim));
		auto * reborn = kill(victim);
		ASSERT_NE(reborn, nullptr);
		if(stopped)
			effect(reborn->unitId(), Bonus(BonusDuration::N_TURNS, BonusType::TIME_STOP,
				BonusSource::SPELL_EFFECT, 0, BonusSourceID(SpellID(SpellID::decode("new-horizons:timeStop")))));
		else
			effect(reborn->unitId(), newHorizonsFrozen::marker(BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound()));
		SwiftEnvironment environment(gameState());
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		auto model = std::make_shared<HypotheticBattle>(&environment, callback);
		model->nextTurn(reborn->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		EXPECT_TRUE(newHorizonsSwiftRebirth::normalActivationCompleted(*model->getForUpdate(reborn->unitId()), battle()->getRound()));
		const auto round = battle()->getRound();
		for(int i = 0; i < 64 && !newHorizonsSwiftRebirth::normalActivationCompleted(*reborn, round); ++i)
			ASSERT_TRUE(action(battle()->battleActiveUnit()));
		EXPECT_TRUE(newHorizonsSwiftRebirth::normalActivationCompleted(*reborn, round));
		EXPECT_FALSE(newHorizonsSwiftRebirth::lifecycle(*reborn)->priorityPending);
		const auto receipt = newHorizonsSwiftRebirth::completeNormalActivationPlan(*reborn, round);
		EXPECT_FALSE(receipt);
		const auto none = reborn->getBonusesOfType(BonusType::NONE);
		EXPECT_EQ(std::count_if(none->begin(), none->end(), [](const auto & bonus)
		{ return newHorizonsSwiftRebirth::isLifecycleMarker(*bonus); }), 1);
		for(const auto & bonus : *none)
			if(newHorizonsSwiftRebirth::isLifecycleMarker(*bonus))
				ASSERT_NO_THROW(effect(reborn->unitId(), *bonus, true));
		Bonus malformed(newHorizonsSwiftRebirth::marker(round));
		malformed.source = BonusSource::OTHER;
		EXPECT_THROW(effect(reborn->unitId(), malformed), std::invalid_argument);
		EXPECT_TRUE(newHorizonsSwiftRebirth::lifecycle(*reborn)->normalCompleted);
	}
}

TEST_F(NewHorizonsSwiftRebirthTest, ChainGetsIndependentReceiptAndNextRoundReturnsOrdinaryEligibility)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	auto * first = kill(source);
	ASSERT_NE(first, nullptr);
	auto * second = kill(first);
	ASSERT_NE(second, nullptr);
	EXPECT_TRUE(battle()->getRebirthChainUsed(BattleSide::DEFENDER));
	const auto receipt = newHorizonsSwiftRebirth::lifecycle(*second);
	ASSERT_TRUE(receipt);
	EXPECT_TRUE(receipt->priorityPending);
	EXPECT_FALSE(receipt->normalCompleted);
	EXPECT_NE(first->unitId(), second->unitId());
	EXPECT_TRUE(physicalAfflictions::enumerate(*second).empty());
	EXPECT_TRUE(battle()->captureBattleEffects(second->unitId()).effects.empty());
	BattleNextRound next;
	next.battleID = BattleID(0);
	gameHandler->sendAndApply(next);
	EXPECT_FALSE(newHorizonsSwiftRebirth::blocksAdditionalActivation(*second, battle()->getRound()));
	EXPECT_FALSE(newHorizonsSwiftRebirth::priorityEligible(*second, battle()->getRound()));
	EXPECT_TRUE(second->willMove());
}

TEST_F(NewHorizonsSwiftRebirthTest, CurrentPersistenceAndEveryOlderOuterPrefixGuard)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const int stage : {0, 1, 2})
	{
		CStackBasicDescriptor descriptor(creatureByName("core:peasant"), 3);
		CStack original(&descriptor, PlayerColor(1), 70, BattleSide::DEFENDER);
		original.initialPosition = BattleHex(rightHex);
		original.addNewBonus(std::make_shared<Bonus>(newHorizonsSwiftRebirth::marker(battle()->getRound())));
		if(stage != 0)
		{
			const auto replacement = stage == 2
				? newHorizonsSwiftRebirth::completeNormalActivationPlan(original, battle()->getRound())
				: newHorizonsSwiftRebirth::reserveNormalActivationPlan(original, battle()->getRound());
			original.removeBonusesRecursive(Selector::type()(BonusType::NONE));
			original.addNewBonus(std::make_shared<Bonus>(*replacement));
		}
		CMemorySerializer memory;
		ASSERT_NO_THROW(original.serialize(memory.oser));
		CStack decoded;
		ASSERT_NO_THROW(decoded.serialize(memory.iser));
		decoded.localInit(battle());
		const auto restored = newHorizonsSwiftRebirth::lifecycle(decoded);
		ASSERT_TRUE(restored);
		EXPECT_EQ(restored->normalCompleted, stage == 2);
		EXPECT_EQ(restored->priorityPending, stage == 0);
		decoded.detachFromAll();
		SwiftPrefixProbe probe;
		EXPECT_THROW(original.serialize(probe), std::runtime_error);
		EXPECT_EQ(probe.fields, 0);
	}
	const auto marker = newHorizonsSwiftRebirth::marker(battle()->getRound());
	Bonus oldMarker(marker);
	SwiftPrefixProbe bonusProbe;
	EXPECT_THROW(oldMarker.serialize(bonusProbe), std::runtime_error);
	EXPECT_EQ(bonusProbe.fields, 0);
	for(int lane = 0; lane < 3; ++lane)
	{
		SetStackEffect packet;
		(lane == 0 ? packet.toAdd : lane == 1 ? packet.toUpdate : packet.toRemove)
			.emplace_back(source->unitId(), std::vector<Bonus>{marker});
		SwiftPrefixProbe probe;
		EXPECT_THROW(packet.serialize(probe), std::runtime_error);
		EXPECT_EQ(probe.fields, 0);
		CMemorySerializer memory;
		ASSERT_NO_THROW(packet.serialize(memory.oser));
		SetStackEffect restored;
		ASSERT_NO_THROW(restored.serialize(memory.iser));
		const auto & effects = lane == 0 ? restored.toAdd : lane == 1 ? restored.toUpdate : restored.toRemove;
		ASSERT_EQ(effects.size(), 1u);
		EXPECT_TRUE(newHorizonsSwiftRebirth::isLifecycleMarker(effects.front().second.front()));
	}
	Bonus invalid(marker);
	invalid.source = BonusSource::SPELL_EFFECT;
	EXPECT_THROW(newHorizonsSwiftRebirth::lifecycle(invalid), std::invalid_argument);
	EXPECT_THROW(newHorizonsSwiftRebirth::validateTransition(
		newHorizonsSwiftRebirth::Lifecycle{battle()->getRound(), false, true}, marker, false), std::invalid_argument);
	CStackBasicDescriptor plainDescriptor(creatureByName("core:peasant"), 3);
	CStack plainStack(&plainDescriptor, PlayerColor(0), 71, BattleSide::ATTACKER);
	CMemorySerializer oldPlain;
	oldPlain.oser.version = oldPlain.iser.version = ESerializationVersion::NEW_HORIZONS_BLOODRAGE_DEATH_PERKS;
	ASSERT_NO_THROW(plainStack.serialize(oldPlain.oser));
	CStack oldRestored;
	ASSERT_NO_THROW(oldRestored.serialize(oldPlain.iser));
	EXPECT_FALSE(newHorizonsSwiftRebirth::lifecycle(oldRestored));
	Bonus ordinary;
	SwiftPrefixProbe plain;
	EXPECT_THROW(ordinary.serialize(plain), std::runtime_error);
	EXPECT_EQ(plain.fields, 1);
	auto * reborn = kill(source);
	ASSERT_NE(reborn, nullptr);
	SwiftPrefixProbe battleProbe;
	EXPECT_THROW(battle()->serialize(battleProbe), std::runtime_error);
	EXPECT_EQ(battleProbe.fields, 0);
	BattleStart start;
	start.info = std::make_unique<BattleInfo>(nullptr);
	start.info->stacks.emplace_back(std::make_unique<CStack>());
	start.info->stacks.back()->addNewBonus(std::make_shared<Bonus>(marker));
	SwiftPrefixProbe startProbe;
	EXPECT_THROW(start.serialize(startProbe), std::runtime_error);
	EXPECT_EQ(startProbe.fields, 0);
}

TEST_F(NewHorizonsSwiftRebirthTest, AcceptedAttackFrozenByRetaliationCompletesBeforeEarlyReturn)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, 10000));
	const auto captured = newHorizonsElementalRebirth::captureDeathSource(*source, defenderSideHero);
	ASSERT_TRUE(captured);
	const auto candidates = newHorizonsElementalRebirth::legalCandidatePool(
		*battle(), battle()->getAccessibility(source), *captured);
	const auto earth = creatureByName("core:earthElemental");
	const auto chosen = std::find(candidates.begin(), candidates.end(), earth);
	ASSERT_NE(chosen, candidates.end());
	uint32_t rebirthSeed = 0;
	int sample = 0;
	for(; sample < 4096; ++sample)
	{
		// The first draws for adjacent small seeds need not span this pool.
		// Sample across the full uint32 range with an odd multiplicative stride.
		rebirthSeed = (static_cast<uint32_t>(sample) + 1u) * 2654435761u;
		CRandomGenerator candidate(rebirthSeed);
		if(candidate.nextInt64(0, candidates.size() - 1) == std::distance(candidates.begin(), chosen))
			break;
	}
	ASSERT_LT(sample, 4096);
	gameHandler->randomizer->setSeed(rebirthSeed);
	auto * reborn = kill(source);
	ASSERT_NE(reborn, nullptr);
	ASSERT_EQ(reborn->creatureId(), earth);
	auto * ice = addStack(BattleSide::ATTACKER, creatureByName("core:iceElemental"),
		BattleHex(rightHex + 2), 100);
	// Ice is double-wide: its attacker-side rear must be beside, not on,
	// the reborn stack's corpse hex.
	ASSERT_NE(ice->occupiedHex(), reborn->getPosition());
	ASSERT_TRUE(battle()->isMeleeAttackPossible(reborn, ice));
	for(auto * unit : {reborn, ice})
	{
		effect(unit->unitId(), Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_LUCK,
			BonusSource::OTHER, 1, BonusSourceID()));
		effect(unit->unitId(), Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE,
			BonusSource::OTHER, 1, BonusSourceID()));
		effect(unit->unitId(), Bonus(BonusDuration::ONE_BATTLE, BonusType::ALWAYS_MAXIMUM_DAMAGE,
			BonusSource::OTHER, 1, BonusSourceID()));
	}
	ASSERT_NO_FATAL_FAILURE(reach(reborn->unitId()));
	const auto round = battle()->getRound();
	const auto before = newHorizonsSwiftRebirth::lifecycle(*reborn);
	ASSERT_TRUE(before);
	ASSERT_FALSE(before->priorityPending);
	ASSERT_FALSE(before->normalCompleted);
	// Prime the private per-army ability RNG before isolating the actual 20%
	// Freezing Touch draw, exactly as the focused Frozen server fixture does.
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		ASSERT_FALSE(gameHandler->randomizer->rollCombatAbility(battle()->getSideArmy(side)->id, 0));
	int procSeed = 0;
	for(; procSeed < 4096; ++procSeed)
	{
		CRandomGenerator candidate(procSeed);
		if(candidate.nextInt(0, 99) < 20)
			break;
	}
	ASSERT_LT(procSeed, 4096);
	gameHandler->randomizer->setSeed(procSeed);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(reborn, ice->getPosition(), reborn->getPosition())));
	ASSERT_TRUE(reborn->alive());
	ASSERT_TRUE(ice->alive());
	EXPECT_EQ(std::ranges::count_if(server.attacks, [ice](const auto & packet)
	{ return packet.counter() && packet.stackAttacking == ice->unitId(); }), 1);
	ASSERT_EQ(battle()->getRound(), round);
	EXPECT_TRUE(newHorizonsSwiftRebirth::normalActivationCompleted(*reborn, round));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*reborn));
	EXPECT_EQ(reborn->frozenLastAppliedRound(), round);
	// Rebinding clears ordinary transient movement. The saved lifecycle must
	// independently prevent a second birth-round slot, not rely on moved.
	reborn->movedThisRound = false;
	std::vector<battle::Units> queue;
	battle()->battleGetTurnOrder(queue, 0, 1, -1);
	ASSERT_FALSE(queue.empty());
	EXPECT_EQ(std::count(queue.front().begin(), queue.front().end(), reborn), 0);
}

TEST_F(NewHorizonsSwiftRebirthTest, NestedSpawnLifecycleReplacementIsSingleAndBranchLocal)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SwiftEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto projectedSource = parent->getForUpdate(source->unitId());
	const auto captured = parent->captureElementalRebirthSource(*projectedSource);
	ASSERT_TRUE(captured);
	auto damage = projectedSource->getAvailableHealth();
	projectedSource->damage(damage);
	const auto spawnId = parent->projectElementalRebirth(projectedSource.get(), *captured, true, false, false);
	ASSERT_TRUE(spawnId);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto parentUnit = parent->getForUpdate(*spawnId);
	const auto childUnit = child->getForUpdate(*spawnId);
	const auto siblingUnit = sibling->getForUpdate(*spawnId);
	const auto countMarkers = [](const battle::Unit & unit)
	{
		const auto markers = unit.getBonusesOfType(BonusType::NONE);
		return std::count_if(markers->begin(), markers->end(), [](const auto & bonus)
		{ return newHorizonsSwiftRebirth::isLifecycleMarker(*bonus); });
	};
	ASSERT_EQ(countMarkers(*childUnit), 1);
	ASSERT_TRUE(newHorizonsSwiftRebirth::lifecycle(*childUnit));
	ASSERT_TRUE(newHorizonsSwiftRebirth::lifecycle(*childUnit)->priorityPending);
	child->nextTurn(*spawnId, BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_EQ(countMarkers(*childUnit), 1);
	ASSERT_FALSE(newHorizonsSwiftRebirth::lifecycle(*childUnit)->priorityPending);
	EXPECT_FALSE(newHorizonsSwiftRebirth::lifecycle(*childUnit)->normalCompleted);
	child->completeSwiftNormalActivation(*spawnId);
	EXPECT_TRUE(newHorizonsSwiftRebirth::lifecycle(*childUnit)->normalCompleted);
	EXPECT_EQ(countMarkers(*childUnit), 1);
	EXPECT_EQ(countMarkers(*parentUnit), 1);
	EXPECT_EQ(countMarkers(*siblingUnit), 1);
	EXPECT_TRUE(newHorizonsSwiftRebirth::lifecycle(*parentUnit)->priorityPending);
	EXPECT_FALSE(newHorizonsSwiftRebirth::lifecycle(*parentUnit)->normalCompleted);
	EXPECT_TRUE(newHorizonsSwiftRebirth::lifecycle(*siblingUnit)->priorityPending);
	EXPECT_FALSE(newHorizonsSwiftRebirth::lifecycle(*siblingUnit)->normalCompleted);
	EXPECT_TRUE(source->alive());
	EXPECT_FALSE(newHorizonsSwiftRebirth::lifecycle(*source));
	EXPECT_EQ(battle()->battleGetUnitByID(*spawnId), nullptr);
}
