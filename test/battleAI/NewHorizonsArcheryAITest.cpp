/*
 * NewHorizonsArcheryAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"

namespace
{
class ArcheryAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ArcheryAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ArcheryAICallback final : public CBattleCallback
{
public:
	ArcheryAICallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
};

class NewHorizonsArcheryAITest : public BattleTestFixture
{
protected:
	std::shared_ptr<ArcheryAIEnvironment> environment;
	std::shared_ptr<ArcheryAICallback> callback;
	CStack * shooter = nullptr;
	CStack * target = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(bool withSkirmisher, bool blockCurrentShot = true, bool useMarksman = false)
	{
		startGame();
		if(withSkirmisher)
		{
			const int decoded = SecondarySkill::decode(std::string(newHorizonsArchery::SKILL));
			ASSERT_GE(decoded, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({std::string(newHorizonsArchery::SKILL),
				std::string(newHorizonsArchery::SKIRMISHER)});
			ASSERT_TRUE(newHorizonsArchery::hasSkirmisher(attackerSideHero));
		}
		startBattle();
		shooter = addStack(BattleSide::ATTACKER,
			creatureByName(useMarksman ? "core:marksman" : "core:titan"), BattleHex(3, 5), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 100);
		CStack * blocker = nullptr;
		if(withSkirmisher && blockCurrentShot)
			blocker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 5), 100);

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != shooter && unit != target && unit != blocker)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		beginCombat();
		battle()->activeStack = shooter->unitId();
		callback = std::make_shared<ArcheryAICallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<ArcheryAIEnvironment>(gameState());
	}

	BattleAction choose()
	{
		BattleEvaluator evaluator(environment, callback, shooter, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		return evaluator.selectStackAction(shooter);
	}
};
}

TEST_F(NewHorizonsArcheryAITest, OrdinaryShooterKeepsUsingDirectShotAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_TRUE(battle()->battleCanShoot(shooter, target->getPosition()));
	const BattleAction action = choose();
	EXPECT_EQ(action.actionType, EActionType::SHOOT);
	ASSERT_FALSE(action.target.empty());
	EXPECT_EQ(action.target.front().unitValue, target->unitId());
}

TEST_F(NewHorizonsArcheryAITest, SkirmisherUsesCanonicalWalkAndAttackOnlyForLegalMoveThenShot)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_FALSE(battle()->battleCanShoot(shooter, target->getPosition()));
	const auto candidates = battle()->battleGetSkirmisherAttackFromHexes(shooter, target->getPosition());
	ASSERT_FALSE(candidates.empty());

	const BattleAction action = choose();
	EXPECT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	EXPECT_TRUE(action.archerySkirmisherAttack);
	ASSERT_EQ(action.target.size(), 2u);
	EXPECT_EQ(action.target[1].hexValue, target->getPosition());
	EXPECT_TRUE(vstd::contains(candidates, action.target[0].hexValue))
		<< "AI should submit its scored legal destination; it need not match a nearest-hex tie-breaker";
}

TEST_F(NewHorizonsArcheryAITest, PotentialTargetsScoreSkirmisherPositionsAlongsideDirectShots)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false));
	ASSERT_TRUE(battle()->battleCanShoot(shooter, target->getPosition()));
	auto simulation = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(simulation, BattleSide::ATTACKER);
	const auto candidates = simulation->battleGetSkirmisherAttackFromHexes(
		simulation->battleGetUnitByID(shooter->unitId()), target->getPosition());
	ASSERT_FALSE(candidates.empty());
	PotentialTargets evaluated(shooter, cache, simulation);
	EXPECT_TRUE(std::ranges::any_of(evaluated.possibleAttacks, [target](const AttackPossibility & attack)
	{
		return attack.attack.shooting && attack.attack.defender->unitId() == target->unitId()
			&& !attack.from.isValid();
	})) << "Direct fire remains a candidate when legal";
	for(const BattleHex & candidate : candidates)
		EXPECT_TRUE(std::ranges::any_of(evaluated.possibleAttacks, [target, candidate](const AttackPossibility & attack)
		{
			return attack.attack.shooting && attack.attack.defender->unitId() == target->unitId()
				&& attack.from == candidate;
		})) << "Each legal move-and-fire destination is evaluated by the AI";
}

TEST_F(NewHorizonsArcheryAITest, SkirmisherForecastsEveryMarksmanRangedAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false, true));
	const int expectedAttackCount = shooter->getTotalAttacks(true);
	ASSERT_GT(expectedAttackCount, 1);
	auto simulation = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(simulation, BattleSide::ATTACKER);
	const auto candidates = simulation->battleGetSkirmisherAttackFromHexes(
		simulation->battleGetUnitByID(shooter->unitId()), target->getPosition());
	ASSERT_FALSE(candidates.empty());
	PotentialTargets evaluated(shooter, cache, simulation);
	for(const BattleHex & candidate : candidates)
	{
		const auto scored = std::ranges::find_if(evaluated.possibleAttacks,
			[target, candidate](const AttackPossibility & attack)
			{
				return attack.attack.shooting && attack.attack.defender->unitId() == target->unitId()
					&& attack.from == candidate;
			});
		ASSERT_NE(scored, evaluated.possibleAttacks.end());
		EXPECT_EQ(scored->fortuneStrikes.size(), static_cast<size_t>(expectedAttackCount))
			<< "AI forecasts the same sequence of ranged strikes that the server executes";
	}
}

TEST_F(NewHorizonsArcheryAITest, SkirmisherForecastProjectsMovePositionForRangeAndCounterfire)
{
	startGame();
	const int archery = SecondarySkill::decode(std::string(newHorizonsArchery::SKILL));
	ASSERT_GE(archery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(archery), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({std::string(newHorizonsArchery::SKILL),
		std::string(newHorizonsArchery::SKIRMISHER)});
	defenderSideHero->setSecSkillLevel(SecondarySkill(archery), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	defenderSideHero->applyPerkSelection({std::string(newHorizonsArchery::SKILL),
		std::string(newHorizonsArchery::COUNTERFIRE)});
	startBattle();
	shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(2, 5), 10);
	target = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(11, 5), 10);
	const Bonus range(BonusDuration::PERMANENT, BonusType::LIMITED_SHOOTING_RANGE,
		BonusSource::OTHER, 6, BonusSourceID());
	shooter->addNewBonus(std::make_shared<Bonus>(range));
	target->addNewBonus(std::make_shared<Bonus>(range));
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != target)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	beginCombat();
	ASSERT_FALSE(battle()->battleCanShoot(shooter, target->getPosition()))
		<< "The original position is outside the shooter's range";
	ASSERT_FALSE(battle()->battleCanShoot(target, shooter->getPosition()))
		<< "Counterfire cannot legally reach the original position";

	const auto candidates = battle()->battleGetSkirmisherAttackFromHexes(shooter, target->getPosition());
	const auto candidate = std::ranges::find_if(candidates, [this](const BattleHex & hex)
		{ return BattleHex::getDistance(hex, target->getPosition()) <= 6
			&& BattleHex::getDistance(hex, shooter->getPosition()) <= shooter->getMovementRange(0) / 2; });
	ASSERT_NE(candidate, candidates.end());
	const int moveDistance = static_cast<int>(BattleHex::getDistance(*candidate, shooter->getPosition()));

	environment = std::make_shared<ArcheryAIEnvironment>(gameState());
	callback = std::make_shared<ArcheryAICallback>();
	callback->onBattleStarted(battle());
	auto simulation = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(simulation, BattleSide::ATTACKER);
	const auto * simShooter = simulation->battleGetUnitByID(shooter->unitId());
	const auto * simTarget = simulation->battleGetUnitByID(target->unitId());
	BattleAttackInfo projectedShot(simShooter, simTarget, moveDistance, true);
	projectedShot.attackerPos = *candidate;
	projectedShot.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT;
	const auto possibility = AttackPossibility::evaluate(projectedShot, *candidate, cache, simulation);
	ASSERT_NE(possibility.attackerState, nullptr);
	EXPECT_EQ(possibility.attackerState->getPosition(), *candidate)
		<< "AI damage and reaction calculations use the firing destination, not the original hex";
	EXPECT_GT(possibility.defenderDamageReduce, 0.0f);
	EXPECT_GT(possibility.attackerDamageReduce, 0.0f)
		<< "The target's legal Counterfire at the moved destination is included in the forecast";
}
