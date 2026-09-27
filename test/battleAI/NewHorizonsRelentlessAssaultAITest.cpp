/*
 * NewHorizonsRelentlessAssaultAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace
{
class RelentlessAssaultEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RelentlessAssaultEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsRelentlessAssaultAITest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void grantRelentlessAssault(CGHeroInstance * hero)
	{
		const int decodedOffense = SecondarySkill::decode(newHorizonsOffense::SKILL);
		ASSERT_GE(decodedOffense, 0);
		const SecondarySkill offense(decodedOffense);
		hero->setSecSkillLevel(offense, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT});
		ASSERT_TRUE(hero->hasActivePerk(
			newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT));
	}

	void startBattleWithRelentlessAssault()
	{
		startGame();
		ASSERT_NO_FATAL_FAILURE(grantRelentlessAssault(attackerSideHero));
		startBattle();
	}
};

std::vector<const FortuneStrikeProjection *> primaryAttacks(const AttackPossibility & possibility)
{
	std::vector<const FortuneStrikeProjection *> result;
	for(const auto & strike : possibility.fortuneStrikes)
		if(strike.attackerId == possibility.attack.attacker->unitId()
			&& !strike.retaliation && strike.cleaveDamagePercent == 0)
			result.push_back(&strike);
	return result;
}

int64_t hitOn(const FortuneStrikeProjection & strike, uint32_t unitId)
{
	for(const auto & [target, damage] : strike.hits)
		if(target == unitId)
			return damage;
	return -1;
}
}

TEST_F(NewHorizonsRelentlessAssaultAITest, ProtectMultistrikeUsesCapturedTargetAndUpdatesProjectedChainPerBlow)
{
	ASSERT_NO_FATAL_FAILURE(startBattleWithRelentlessAssault());
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * ward = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	BattleHex protectorHex = BattleHex::INVALID;
	for(const auto candidate : ward->getPosition().getNeighbouringTiles())
		if(candidate.isAvailable() && candidate != attacker->getPosition()
			&& !battle()->battleGetUnitByPos(candidate, true))
		{
			protectorHex = candidate;
			break;
		}
	ASSERT_TRUE(protectorHex.isAvailable());
	auto * protector = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), protectorHex, 100);
	BattleTestFixture::blockRetaliation(ward);
	BattleTestFixture::blockRetaliation(protector);
	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(beginCombat());

	HeroOrderState protect;
	protect.command = HeroCommand::PROTECT;
	protect.issuedRound = battle()->battleGetRound();
	protect.primaryTargetUnitId = protector->unitId();
	protect.secondaryTargetUnitId = ward->unitId();
	battle()->setHeroOrderState(BattleSide::DEFENDER, protect);
	ASSERT_EQ(battle()->battleResolveHeroOrderTarget(attacker, ward, false)->unitId(), protector->unitId());

	RelentlessAssaultState streak;
	streak.beginActivation();
	streak.recordAttack(protector->unitId());
	streak.beginActivation();
	battle()->setRelentlessAssaultState(BattleSide::ATTACKER, streak);

	RelentlessAssaultEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	DamageCache cache;
	const auto evaluate = [&](bool seedStreak)
	{
		auto model = std::make_shared<HypotheticBattle>(&environment, callback);
		if(!seedStreak)
		{
			RelentlessAssaultState empty;
			empty.beginActivation();
			model->setRelentlessAssaultState(BattleSide::ATTACKER, empty);
		}
		auto projectedAttacker = model->getForUpdate(attacker->unitId());
		auto projectedWard = model->getForUpdate(ward->unitId());
		return std::pair{
			AttackPossibility::evaluate(BattleAttackInfo(projectedAttacker.get(), projectedWard.get(), 0, false),
				attacker->getPosition(), cache, model),
			model};
	};

	auto [ordinary, ordinaryModel] = evaluate(false);
	auto [boosted, projectedModel] = evaluate(true);
	auto ordinaryStrikes = primaryAttacks(ordinary);
	auto boostedStrikes = primaryAttacks(boosted);
	ASSERT_EQ(ordinaryStrikes.size(), 2u);
	ASSERT_EQ(boostedStrikes.size(), 2u);
	EXPECT_EQ(ordinaryStrikes[0]->defenderId, protector->unitId());
	EXPECT_EQ(boostedStrikes[0]->defenderId, protector->unitId());
	EXPECT_EQ(ordinaryStrikes[1]->defenderId, ward->unitId());
	EXPECT_EQ(boostedStrikes[1]->defenderId, ward->unitId());
	EXPECT_TRUE(boostedStrikes[0]->protectIntercepted);
	EXPECT_FALSE(boostedStrikes[1]->protectIntercepted);
	EXPECT_TRUE(boostedStrikes[0]->relentlessAssaultEligible);
	EXPECT_TRUE(boostedStrikes[1]->relentlessAssaultEligible);
	EXPECT_GT(hitOn(*boostedStrikes[0], protector->unitId()), hitOn(*ordinaryStrikes[0], protector->unitId()));
	EXPECT_EQ(hitOn(*boostedStrikes[1], ward->unitId()), hitOn(*ordinaryStrikes[1], ward->unitId()));
	EXPECT_GT(boosted.attackValue(), ordinary.attackValue());

	const auto liveStreakBefore = battle()->getRelentlessAssaultState(BattleSide::ATTACKER);
	BattleExchangeVariant exchange;
	exchange.trackAttack(boosted, projectedModel, cache);
	const auto & projectedStreak = projectedModel->battleGetRelentlessAssaultState(BattleSide::ATTACKER);
	EXPECT_EQ(projectedStreak.targetUnitId, ward->unitId());
	EXPECT_EQ(projectedStreak.tier, 0);
	ASSERT_TRUE(projectedModel->battleGetHeroOrderState(BattleSide::DEFENDER).has_value());
	EXPECT_TRUE(projectedModel->battleGetHeroOrderState(BattleSide::DEFENDER)->protectIntercepted);
	EXPECT_EQ(battle()->getRelentlessAssaultState(BattleSide::ATTACKER), liveStreakBefore);
	ASSERT_TRUE(battle()->battleGetHeroOrderState(BattleSide::DEFENDER).has_value());
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::DEFENDER)->protectIntercepted);
	EXPECT_EQ(ordinaryModel->battleGetRelentlessAssaultState(BattleSide::ATTACKER).targetUnitId,
		RelentlessAssaultState::INVALID_TARGET);
}

TEST_F(NewHorizonsRelentlessAssaultAITest, DefendingHeroStreakDoesNotIncreaseRetaliationDamage)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(grantRelentlessAssault(defenderSideHero));
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
	ASSERT_NO_FATAL_FAILURE(beginCombat());

	RelentlessAssaultState streak;
	streak.beginActivation();
	streak.recordAttack(attacker->unitId());
	streak.beginActivation();
	battle()->setRelentlessAssaultState(BattleSide::DEFENDER, streak);
	ASSERT_EQ(battle()->battleGetRelentlessAssaultDamagePercent(defender, attacker), 10);

	RelentlessAssaultEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	const auto retaliationDamage = [&](bool seeded)
	{
		auto model = std::make_shared<HypotheticBattle>(&environment, callback);
		if(!seeded)
		{
			RelentlessAssaultState empty;
			empty.beginActivation();
			model->setRelentlessAssaultState(BattleSide::DEFENDER, empty);
		}
		auto projectedAttacker = model->getForUpdate(attacker->unitId());
		auto projectedDefender = model->getForUpdate(defender->unitId());
		const auto healthBefore = projectedAttacker->getAvailableHealth();
		DamageCache cache;
		BattleExchangeVariant exchange;
		exchange.trackAttack(projectedAttacker, projectedDefender, false, true, cache, model);
		return healthBefore - projectedAttacker->getAvailableHealth();
	};

	const auto ordinaryRetaliation = retaliationDamage(false);
	const auto seededRetaliation = retaliationDamage(true);
	EXPECT_GT(ordinaryRetaliation, 0);
	EXPECT_EQ(seededRetaliation, ordinaryRetaliation);
}
