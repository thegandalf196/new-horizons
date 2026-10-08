/*
 * NewHorizonsPerfectFortuneTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file in main folder
 */
#include "../StdInc.h"
#include "battles/HeroCommandFixture.h"

#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/PerfectFortuneState.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto LUCK_SKILL = "new-horizons:luck";
constexpr auto PERFECT_FORTUNE = "new-horizons:luck.perfectFortune";

JsonNode noLuckChance()
{
	JsonNode result;
	for(int i = 0; i < 10; ++i)
		result.Vector().emplace_back(0);
	return result;
}

class PerfectFortuneEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit PerfectFortuneEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsPerfectFortuneTest : public HeroCommandFixture
{
protected:
	CStack * source = nullptr;
	CStack * target = nullptr;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		auto & perks = rules["skills"][LUCK_SKILL]["perks"].Vector();
		const auto perk = std::ranges::find_if(perks, [](const auto & entry)
		{
			return entry["id"].String() == PERFECT_FORTUNE;
		});
		if(perk == perks.end())
			throw std::runtime_error("Missing Perfect Fortune registry entry");
		RecordProperty("perfect_fortune_registry_status", (*perk)["effect"]["status"].String());
		// Pre-activation production gate only. Once activated, the real registry
		// remains unchanged and the same legal offer path validates that profile.
		if((*perk)["effect"]["status"].String() == "planned")
		{
			(*perk)["effect"]["status"].String() = "active";
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
		}
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, noLuckChance());
		loaded->overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, noLuckChance());
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
	}

	void acceptPerk(CGHeroInstance * hero, const char * id, const char * skillId = LUCK_SKILL)
	{
		const auto rank = [hero](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rank, seed);
			const auto choice = std::ranges::find_if(offer, [id, skillId](const auto & entry)
			{
				return entry.selection.skillId == skillId && entry.selection.perkId == id;
			});
			if(choice == offer.end())
				continue;
			gameHandler->levelUpHero(hero, offer, std::distance(offer.begin(), choice), seed, false);
			ASSERT_TRUE(hero->hasActivePerk(skillId, id));
			return;
		}
		FAIL() << "Missing legitimate perk offer: " << id;
	}

	void acquire(CGHeroInstance * hero)
	{
		const auto skill = SecondarySkill(SecondarySkill::decode(LUCK_SKILL));
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(acceptPerk(hero, "new-horizons:luck.secondChance"));
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(acceptPerk(hero, "new-horizons:luck.chainOfFortune"));
		hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(acceptPerk(hero, PERFECT_FORTUNE));
	}

	void prepare(bool selected = true, bool opposingSelected = false, bool ranged = false)
	{
		startGame();
		if(selected)
		{
			ASSERT_NO_FATAL_FAILURE(acquire(attackerSideHero));
		}
		if(opposingSelected)
		{
			ASSERT_NO_FATAL_FAILURE(acquire(defenderSideHero));
		}
		startBattle();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(removed);
		source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(ranged ? leftHex + 4 : rightHex), 100);
		if(ranged)
		{
			source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::SHOOTER, BonusSource::OTHER, 1, BonusSourceID()));
			source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::SHOTS, BonusSource::OTHER, 12, BonusSourceID()));
		}
		beginCombat();
		activate(source);
		EXPECT_EQ(battle()->getPerfectFortuneState(BattleSide::ATTACKER).enabled, selected);
		EXPECT_EQ(battle()->getPerfectFortuneState(BattleSide::DEFENDER).enabled, opposingSelected);
	}

	void activate(CStack * unit)
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = unit->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	bool strike(CStack * attacker, CStack * defender, bool shooting = false)
	{
		activate(attacker);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(attacker),
			shooting ? BattleAction::makeShotAttack(attacker, defender)
				: BattleAction::makeMeleeAttack(attacker, defender->getPosition(), attacker->getPosition()));
	}

	const BattleAttack * lastAttack(uint32_t id, bool counter = false) const
	{
		const auto found = std::find_if(server.attacks.rbegin(), server.attacks.rend(), [id, counter](const auto & value)
		{
			return value.stackAttacking == id && value.counter() == counter;
		});
		return found == server.attacks.rend() ? nullptr : &*found;
	}
};

TEST_F(NewHorizonsPerfectFortuneTest, CapturedPerkGuaranteesOnlyFirstAuthoritativeMeleeStrike)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	blockRetaliation(target);
	const int ordinaryLuck = battle()->battleGetAttackLuck(source, target, false);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LUCK, BonusSource::OTHER, -ordinaryLuck, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetAttackLuck(source, target, false), 0);
	ASSERT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).available());
	ASSERT_TRUE(strike(source, target));
	const auto * first = lastAttack(source->unitId());
	ASSERT_NE(first, nullptr);
	EXPECT_TRUE(first->lucky());
	ASSERT_TRUE(first->perfectFortuneState);
	EXPECT_TRUE(first->perfectFortuneState->used);
	EXPECT_EQ(first->perfectFortuneSide, BattleSide::ATTACKER);
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).used);
	ASSERT_TRUE(strike(source, target));
	ASSERT_NE(lastAttack(source->unitId()), nullptr);
	EXPECT_FALSE(lastAttack(source->unitId())->lucky());
}

TEST_F(NewHorizonsPerfectFortuneTest, FirstOrdinaryRangedStrikeIsEligible)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false, true));
	ASSERT_TRUE(battle()->battleCanShoot(source, target->getPosition()));
	ASSERT_TRUE(strike(source, target, true));
	const auto * first = lastAttack(source->unitId());
	ASSERT_NE(first, nullptr);
	EXPECT_TRUE(first->shot());
	EXPECT_TRUE(first->lucky());
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).used);
}

TEST_F(NewHorizonsPerfectFortuneTest, RetaliationClaimsOpposingSideIndependently)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	ASSERT_TRUE(strike(source, target));
	const auto * first = lastAttack(source->unitId());
	const auto * retaliation = lastAttack(target->unitId(), true);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(retaliation, nullptr);
	EXPECT_TRUE(first->lucky());
	EXPECT_TRUE(retaliation->lucky());
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).used);
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::DEFENDER).used);
	EXPECT_EQ(retaliation->perfectFortuneSide, BattleSide::DEFENDER);
}

TEST_F(NewHorizonsPerfectFortuneTest, NoLuckSuppressionPreservesTokenForLaterEligibleAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	blockRetaliation(target);
	const Bonus suppressed(BonusDuration::ONE_BATTLE, BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID());
	source->addNewBonus(std::make_shared<Bonus>(suppressed));
	ASSERT_TRUE(strike(source, target));
	ASSERT_NE(lastAttack(source->unitId()), nullptr);
	EXPECT_FALSE(lastAttack(source->unitId())->lucky());
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).available());
	SetStackEffect removed;
	removed.battleID = BattleID(0);
	removed.toRemove.emplace_back(source->unitId(), std::vector<Bonus>{suppressed});
	gameHandler->sendAndApply(removed);
	ASSERT_FALSE(source->hasBonusOfType(BonusType::NO_LUCK));
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LUCK, BonusSource::OTHER, -20, BonusSourceID()));
	ASSERT_LT(battle()->battleGetAttackLuck(source, target, false), 0);
	ASSERT_TRUE(strike(source, target));
	ASSERT_NE(lastAttack(source->unitId()), nullptr);
	EXPECT_TRUE(lastAttack(source->unitId())->lucky());
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).used);
}

TEST_F(NewHorizonsPerfectFortuneTest, UnselectedArmyKeepsOrdinaryLuck)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	blockRetaliation(target);
	ASSERT_TRUE(strike(source, target));
	ASSERT_NE(lastAttack(source->unitId()), nullptr);
	EXPECT_FALSE(lastAttack(source->unitId())->lucky());
	EXPECT_EQ(battle()->getPerfectFortuneState(BattleSide::ATTACKER), PerfectFortuneState{});
	const BattleAttackInfo ordinary(source, target, 0, false);
	EXPECT_FALSE(battle()->battleCanTriggerPerfectFortune(ordinary));
}

TEST_F(NewHorizonsPerfectFortuneTest, ZeroMaximumLuckPreservesTokenUntilSuppressionIsRemoved)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	blockRetaliation(target);
	const Bonus cap(BonusDuration::ONE_BATTLE, BonusType::MAXIMUM_LUCK, BonusSource::OTHER, 0, BonusSourceID());
	source->addNewBonus(std::make_shared<Bonus>(cap));
	ASSERT_TRUE(strike(source, target));
	ASSERT_NE(lastAttack(source->unitId()), nullptr);
	EXPECT_FALSE(lastAttack(source->unitId())->lucky());
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).available());
	SetStackEffect removed;
	removed.battleID = BattleID(0);
	removed.toRemove.emplace_back(source->unitId(), std::vector<Bonus>{cap});
	gameHandler->sendAndApply(removed);
	ASSERT_TRUE(strike(source, target));
	ASSERT_NE(lastAttack(source->unitId()), nullptr);
	EXPECT_TRUE(lastAttack(source->unitId())->lucky());
}

TEST_F(NewHorizonsPerfectFortuneTest, WarMachineAndNonphysicalSecondaryDamageCannotClaimToken)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	blockRetaliation(target);
	auto * machine = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(70), 1);
	ASSERT_TRUE(machine->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ASSERT_TRUE(strike(machine, target, true));
	ASSERT_NE(lastAttack(machine->unitId()), nullptr);
	EXPECT_FALSE(lastAttack(machine->unitId())->lucky());
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).available());
	BattleAttackInfo secondary(source, target, 0, false);
	secondary.secondaryAttack = true;
	EXPECT_FALSE(battle()->battleCanTriggerPerfectFortune(secondary));
	BattleAttackInfo nonphysical(source, target, 0, false);
	nonphysical.physicalDamage = false;
	EXPECT_FALSE(battle()->battleCanTriggerPerfectFortune(nonphysical));
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).available());
	ASSERT_TRUE(strike(source, target));
	ASSERT_NE(lastAttack(source->unitId()), nullptr);
	EXPECT_TRUE(lastAttack(source->unitId())->lucky());
}

TEST_F(NewHorizonsPerfectFortuneTest, DetachedGuaranteedStrikeMatchesLiveDamageAndKeepsSiblingAndRngIsolated)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	blockRetaliation(target);
	const auto original = source->acquireState()->save();
	CMemorySerializer rngBefore;
	gameHandler->randomizer->serialize(rngBefore.oser);
	const auto rngBytes = rngBefore.extractBuffer();
	auto environment = std::make_shared<PerfectFortuneEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	HypotheticBattle sibling(environment.get(), model);
	auto projectedSource = model->getForUpdate(source->unitId());
	auto projectedTarget = model->getForUpdate(target->unitId());
	BattleAttackInfo attack(projectedSource.get(), projectedTarget.get(), 0, false);
	const auto captured = model->captureFortuneStrikeOutcome(attack);
	EXPECT_EQ(captured, ProjectedLuckOutcome::POSITIVE);
	auto lucky = attack;
	lucky.luckyStrike = true;
	const auto actualLuckyRange = model->calculateDmgRange(lucky).damage;
	EXPECT_EQ(model->battleExpectedLuckDamage(attack), actualLuckyRange.min);
	ASSERT_EQ(actualLuckyRange.min, actualLuckyRange.max);
	model->projectFortuneStrike(attack, {{target->unitId(), actualLuckyRange.min}}, projectedSource.get(), false);
	EXPECT_TRUE(model->getPerfectFortuneState(BattleSide::ATTACKER).used);
	EXPECT_EQ(model->captureFortuneStrikeOutcome(attack), ProjectedLuckOutcome::NEUTRAL);
	EXPECT_TRUE(sibling.getPerfectFortuneState(BattleSide::ATTACKER).available());
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).available());
	EXPECT_EQ(source->acquireState()->save(), original);
	CMemorySerializer rngAfter;
	gameHandler->randomizer->serialize(rngAfter.oser);
	EXPECT_EQ(rngAfter.extractBuffer(), rngBytes);
	const auto healthBefore = target->getAvailableHealth();
	ASSERT_TRUE(strike(source, target));
	EXPECT_EQ(healthBefore - target->getAvailableHealth(), actualLuckyRange.min);
}

TEST_F(NewHorizonsPerfectFortuneTest, ActualCleaveCandidateKeepsFirstStrikeMetadataAndReplaysOnlyIntoItsBranch)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(acquire(attackerSideHero));
	const auto offense = SecondarySkill(SecondarySkill::decode(newHorizonsOffense::SKILL));
	attackerSideHero->setSecSkillLevel(offense, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptPerk(attackerSideHero, "new-horizons:offense.encirclement", newHorizonsOffense::SKILL));
	attackerSideHero->setSecSkillLevel(offense, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(acceptPerk(attackerSideHero, newHorizonsOffense::CLEAVE, newHorizonsOffense::SKILL));
	startBattle();
	source = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(92), 100);
	target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(93), 1);
	auto * followUp = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(76), 100);
	forceMaximumDamage(source);
	beginCombat();
	activate(source);
	const auto liveSource = source->acquireState()->save();
	const auto liveTarget = target->acquireState()->save();
	const auto liveFollowUp = followUp->acquireState()->save();
	auto environment = std::make_shared<PerfectFortuneEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	HypotheticBattle sibling(environment.get(), model);
	DamageCache cache;
	const auto candidate = AttackPossibility::evaluate(BattleAttackInfo(source, target, 0, false),
		source->getPosition(), cache, model);
	ASSERT_TRUE(candidate.defenderDead);
	ASSERT_NE(candidate.effectPreview, nullptr);
	ASSERT_GE(candidate.fortuneStrikes.size(), 2u);
	const auto & primary = candidate.fortuneStrikes.front();
	EXPECT_TRUE(primary.perfectFortune);
	EXPECT_EQ(primary.perfectFortuneSide, BattleSide::ATTACKER);
	EXPECT_EQ(primary.resolvedLuck, ProjectedLuckOutcome::POSITIVE);
	const auto cleave = std::ranges::find_if(candidate.fortuneStrikes, [](const auto & strike)
	{
		return strike.cleaveDamagePercent > 0;
	});
	ASSERT_NE(cleave, candidate.fortuneStrikes.end());
	EXPECT_FALSE(cleave->perfectFortune);
	EXPECT_EQ(cleave->perfectFortuneSide, BattleSide::ATTACKER);
	EXPECT_EQ(cleave->resolvedLuck, ProjectedLuckOutcome::NEUTRAL);
	EXPECT_EQ(cleave->defenderId, followUp->unitId());
	ASSERT_FALSE(cleave->hits.empty());
	EXPECT_GT(cleave->hits.front().second, 0);
	EXPECT_TRUE(candidate.effectPreview->getPerfectFortuneState(BattleSide::ATTACKER).used);
	EXPECT_TRUE(model->getPerfectFortuneState(BattleSide::ATTACKER).available());
	EXPECT_TRUE(sibling.getPerfectFortuneState(BattleSide::ATTACKER).available());
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).available());
	EXPECT_EQ(source->acquireState()->save(), liveSource);
	EXPECT_EQ(target->acquireState()->save(), liveTarget);
	EXPECT_EQ(followUp->acquireState()->save(), liveFollowUp);
	BattleExchangeVariant exchange;
	exchange.trackAttack(candidate, model, cache);
	EXPECT_TRUE(model->getPerfectFortuneState(BattleSide::ATTACKER).used);
	EXPECT_TRUE(sibling.getPerfectFortuneState(BattleSide::ATTACKER).available());
	EXPECT_TRUE(battle()->getPerfectFortuneState(BattleSide::ATTACKER).available());
	const auto projectedHealth = candidate.effectPreview->battleGetUnitByID(followUp->unitId())->getAvailableHealth();
	EXPECT_EQ(model->battleGetUnitByID(followUp->unitId())->getAvailableHealth(), projectedHealth);
	server.attacks.clear();
	ASSERT_TRUE(strike(source, target));
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_TRUE(server.attacks.front().lucky());
	EXPECT_FALSE(server.attacks.back().lucky());
	EXPECT_EQ(server.attacks.back().bsa.front().stackAttacked, followUp->unitId());
	EXPECT_FALSE(target->alive());
	EXPECT_EQ(followUp->getAvailableHealth(), projectedHealth);
}

TEST_F(NewHorizonsPerfectFortuneTest, ArmedBattleSaveAndUsedSidePacketRoundTripRejectLossyOlderFormat)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	blockRetaliation(target);
	const auto armed = battle()->getPerfectFortuneState(BattleSide::ATTACKER);
	ASSERT_TRUE(armed.available());
	const auto savedBattle = CMemorySerializer::deepCopy(*battle(), gameState().get());
	EXPECT_EQ(savedBattle->getPerfectFortuneState(BattleSide::ATTACKER), armed);
	ASSERT_TRUE(strike(source, target));
	const auto state = battle()->getPerfectFortuneState(BattleSide::ATTACKER);
	ASSERT_TRUE(state.enabled);
	ASSERT_TRUE(state.used);
	CMemorySerializer stateWire;
	stateWire.oser & state;
	PerfectFortuneState restored;
	stateWire.iser & restored;
	EXPECT_EQ(restored, state);
	const auto * attack = lastAttack(source->unitId());
	ASSERT_NE(attack, nullptr);
	CMemorySerializer packetWire;
	packetWire.oser & *attack;
	BattleAttack restoredAttack;
	packetWire.iser & restoredAttack;
	EXPECT_EQ(restoredAttack.perfectFortuneSide, attack->perfectFortuneSide);
	EXPECT_EQ(restoredAttack.perfectFortuneState, attack->perfectFortuneState);
	// Current whole-battle binary snapshots reject existing Veteran damage
	// history after a physical strike. Do not erase that history or claim an
	// ongoing-battle health round-trip here; the side payload still saves usage.
	CMemorySerializer sideWire;
	sideWire.oser & battle()->getSide(BattleSide::ATTACKER);
	SideInBattle restoredSide(gameState().get());
	sideWire.iser & restoredSide;
	EXPECT_EQ(restoredSide.perfectFortune, state);

	const auto previousVersion = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_PERFECT_FORTUNE) - 1);
	CMemorySerializer oldState;
	oldState.oser.version = previousVersion;
	EXPECT_THROW(oldState.oser & state, std::runtime_error);
	EXPECT_TRUE(oldState.extractBuffer().empty());
	CMemorySerializer oldPacket;
	oldPacket.oser.version = previousVersion;
	EXPECT_THROW(oldPacket.oser & *attack, std::runtime_error);
	CMemorySerializer oldBattle;
	oldBattle.oser.version = previousVersion;
	EXPECT_THROW(oldBattle.oser & *battle(), std::runtime_error);

	CMemorySerializer legacy;
	legacy.oser.version = legacy.iser.version = previousVersion;
	PerfectFortuneState disabled;
	legacy.oser & disabled;
	PerfectFortuneState stale = state;
	legacy.iser & stale;
	EXPECT_EQ(stale, disabled);
	CMemorySerializer legacyPacket;
	legacyPacket.oser.version = legacyPacket.iser.version = previousVersion;
	BattleAttack emptyPacket;
	emptyPacket.battleID = BattleID(0);
	legacyPacket.oser & emptyPacket;
	BattleAttack stalePacket = *attack;
	legacyPacket.iser & stalePacket;
	EXPECT_EQ(stalePacket.perfectFortuneSide, BattleSide::NONE);
	EXPECT_FALSE(stalePacket.perfectFortuneState);
	PerfectFortuneState invalid;
	invalid.used = true;
	EXPECT_THROW(invalid.validate(), std::runtime_error);
}
