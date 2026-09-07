/*
 * NewHorizonsMasteryAttackCountTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../server/queries/MapQueries.h"
#include "../../server/queries/QueriesProcessor.h"

namespace
{
class MasteryCountEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MasteryCountEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

/// A diagnostic against the real combat evaluator, not the adventure mastery
/// chooser. The legacy and Volley fixtures differ only in mastery activation and
/// its real accepted level-up choice. No attack-count implementation is replaced.
class NewHorizonsMasteryAttackCountTest : public HeroCommandFixture, public ::testing::WithParamInterface<bool>
{
protected:
	void mapLoaded(CMap * map) override
	{
		useCommands = false;
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode());
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_MASTERIES,
			GetParam() ? JsonNode(JsonPath::builtin("config/newHorizonsMasteries")) : JsonNode());
	}

	void prepareBallista()
	{
		startGame();
		for(SecondarySkill skill : {SecondarySkill::ARTILLERY, SecondarySkill::BALLISTICS,
			SecondarySkill::FIRST_AID, SecondarySkill::LOGISTICS, SecondarySkill::PATHFINDING,
			SecondarySkill::SCOUTING, SecondarySkill::NAVIGATION, SecondarySkill::DIPLOMACY})
			gameHandler->changeSecSkill(attackerSideHero, skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(GetParam())
		{
			gameHandler->onAdvInterfaceReady(PlayerColor(0));
			attackerSideHero->setExperience(LIBRARY->heroh->reqExp(2), ChangeValueMode::ABSOLUTE);
			gameHandler->levelUpHero(attackerSideHero);
			const auto normal = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler->queries->topQuery(PlayerColor(0)));
			ASSERT_TRUE(normal);
			ASSERT_TRUE(normal->hlu.skills.empty());
			ASSERT_TRUE(gameHandler->queryReply(normal->queryID, 0, PlayerColor(0)));
			const auto query = gameHandler->queries->topQuery(PlayerColor(0));
			ASSERT_TRUE(query);
			ASSERT_EQ(query->getType(), CHeroMasteryDialogQuery::TYPE);
			ASSERT_TRUE(attackerSideHero->getMasteryState().pending);
			const auto offer = *attackerSideHero->getMasteryState().pending;
			const auto volley = std::find_if(offer.options.begin(), offer.options.end(), [](const auto & option)
			{
				return option.effect == newHorizonsHeroes::MasteryEffect::ARTILLERY_VOLLEY;
			});
			ASSERT_NE(volley, offer.options.end());
			ASSERT_TRUE(gameHandler->heroMasteryReply(query->queryID, offer.hero, offer.sequence,
				static_cast<int>(volley - offer.options.begin()), offer.player));
		}
		giveArtifact(attackerSideHero, ArtifactID::BALLISTA, ArtifactPosition::MACH1);
		startBattle();
	}
};

TEST_P(NewHorizonsMasteryAttackCountTest, CombatPredictionIncludesEveryAuthoritativeHeroGrantedShot)
{
	ASSERT_NO_FATAL_FAILURE(prepareBallista());
	const auto machines = battle()->battleGetStacksIf([](const CStack * unit) { return unit->isBallista(); });
	ASSERT_EQ(machines.size(), 1u);
	auto * machine = const_cast<CStack *>(machines.front());
	// Fixture-only deterministic damage support, identical to other battle tests.
	forceMaximumDamage(machine);
	const BattleHex targetHex(14, 5);
	ASSERT_EQ(battle()->battleGetStackByPos(targetHex), nullptr);
	const auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), targetHex, 10000);
	ASSERT_NE(target, nullptr);
	const int expectedShots = GetParam() ? 3 : 2;
	ASSERT_EQ(machine->getTotalAttacks(true), 1);
	ASSERT_EQ(attackerSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
		BonusSubtypeID(machine->creatureId())), expectedShots - 1);
	ASSERT_TRUE(battle()->battleCanShoot(machine, target->getPosition()));

	const auto recordState = [&](const std::string & phase)
	{
		RecordProperty(phase + "HeroGrantedAttacks", attackerSideHero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS,
			BonusSubtypeID(machine->creatureId())));
		RecordProperty(phase + "ArtilleryRank", static_cast<int>(attackerSideHero->getSecSkillLevel(SecondarySkill::ARTILLERY)));
		RecordProperty(phase + "SelectedMasteries", static_cast<int>(attackerSideHero->getMasteryState().selected.size()));
		RecordProperty(phase + "InnateAttacks", machine->getTotalAttacks(true));
		RecordProperty(phase + "Ammo", machine->shots.available());
		RecordProperty(phase + "SameFightingHero", battle()->battleGetFightingHero(BattleSide::ATTACKER) == attackerSideHero);
	};
	recordState("BeforePrediction");

	// DamageCache represents ONE shot. AttackPossibility must apply that cached
	// damage for every attack the actual server will execute, not just innate ones.
	std::shared_ptr<CBattleInfoCallback> callback(gameState(), battle());
	DamageCache cache;
	cache.cacheDamage(machine, target, callback);
	const auto singleShot = cache.getDamage(machine, target, callback);
	ASSERT_GT(singleShot, 0);
	const auto health = target->getAvailableHealth();
	ASSERT_GT(health, singleShot * expectedShots);
	const BattleAttackInfo attack(machine, target, 0, true);
	const auto prediction = AttackPossibility::evaluate(attack, machine->getPosition(), cache, callback);
	const auto predictedTarget = std::find_if(prediction.affectedUnits.begin(), prediction.affectedUnits.end(), [&](const auto & unit)
	{
		return unit->unitId() == target->unitId();
	});
	ASSERT_NE(predictedTarget, prediction.affectedUnits.end());
	EXPECT_EQ(health - (*predictedTarget)->getAvailableHealth(), singleShot * expectedShots)
		<< "Combat AI must value all legacy Expert/Volley shots, not only the unit's innate attack";
	EXPECT_EQ(target->getAvailableHealth(), health) << "Prediction must not mutate the real battle";
	recordState("AfterPrediction");

	beginCombat();
	recordState("AfterBeginCombat");
	const int round = battle()->getRound();
	for(size_t turn = 0; turn < battle()->stacks.size() * 2; ++turn)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		if(active->unitId() == machine->unitId())
			break;
		ASSERT_EQ(battle()->getRound(), round);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), active->unitOwner(), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), machine->unitId());
	recordState("BeforeServerShot");
	ASSERT_EQ(battle()->battleGetStackByPos(target->getPosition()), target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(machine, target)));
	recordState("AfterServerShot");
	EXPECT_TRUE(target->alive()) << "The intended durable target must survive every counted shot";
	EXPECT_EQ(std::count_if(server.attacks.begin(), server.attacks.end(), [&](const auto & packet)
	{
		return packet.shot() && packet.stackAttacking == machine->unitId();
	}), expectedShots);
}

TEST_P(NewHorizonsMasteryAttackCountTest, TwoTurnExchangeCountsHeroGrantedShotsAfterTheInitialAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepareBallista());
	const auto machines = battle()->battleGetStacksIf([](const CStack * unit) { return unit->isBallista(); });
	ASSERT_EQ(machines.size(), 1u);
	const auto * machine = machines.front();
	const BattleHex targetHex(14, 5);
	ASSERT_EQ(battle()->battleGetStackByPos(targetHex), nullptr);
	const auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), targetHex, 10000);
	beginCombat();
	const auto round = battle()->getRound();
	for(size_t turn = 0; turn < battle()->stacks.size() * 2; ++turn)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_NE(active, nullptr);
		if(active->unitId() == machine->unitId())
			break;
		ASSERT_EQ(battle()->getRound(), round);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), active->unitOwner(), BattleAction::makeDefend(active)));
	}
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), machine->unitId());
	ASSERT_EQ(battle()->battleGetStackByPos(targetHex), target);
	const auto health = target->getAvailableHealth();
	const auto ammo = machine->shots.available();

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto environment = std::make_shared<MasteryCountEnvironment>(gameState());
	auto hypothetical = std::make_shared<HypotheticBattle>(environment.get(), callback);
	DamageCache cache;
	cache.buildDamageCache(hypothetical, BattleSide::ATTACKER);
	const auto attack = AttackPossibility::evaluate(BattleAttackInfo(machine, target, 0, true),
		machine->getPosition(), cache, hypothetical);
	PotentialTargets targets(machine, cache, hypothetical);
	BattleExchangeEvaluator evaluator(callback, environment, 1.0f, 2);
	evaluator.updateReachabilityMap(hypothetical);
	const auto units = evaluator.getExchangeUnits(attack, 0, targets, hypothetical);
	ASSERT_FALSE(units.units.empty());
	ASSERT_TRUE(units.enemyUnitsReachingAttacker.empty());
	// Only this distant friendly shooter participates. Enemy melee units cannot
	// reach it: the second exchange turn must use the non-initial attack loop.
	for(const auto & [turn, actors] : units.units)
		for(const auto * actor : actors)
			if(actor->unitSide() == BattleSide::ATTACKER)
				ASSERT_EQ(actor->unitId(), machine->unitId()) << "exchange turn " << turn;

	const auto actual = evaluator.evaluateExchange(attack, 0, targets, cache, hypothetical);
	// The initial prepared AP has its own scoring: it evaluates each hit against
	// the original defender, unlike the progressively damaged single-hit tracker.
	// Its 2/3-hit health result is independently tested above. Reuse that prepared
	// action, then independently track exactly 2/3 later hits without the helper.
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), callback);
	BattleExchangeVariant expected;
	expected.trackAttack(attack, reference, cache);
	RecordProperty("InitialPreparedAttackScore", ::testing::PrintToString(expected.getScore().enemyDamageReduce));
	reference->nextRound();
	const int expectedShots = GetParam() ? 3 : 2;
	RecordProperty("ExpectedLaterShots", expectedShots);
	for(int shot = 0; shot < expectedShots; ++shot)
		expected.trackAttack(reference->getForUpdate(machine->unitId()), reference->getForUpdate(target->unitId()),
			true, true, cache, reference);
	reference->nextRound();
	ASSERT_FLOAT_EQ(expected.getScore().ourDamageReduce, 0.0f);
	ASSERT_GT(expected.getScore().enemyDamageReduce, 0.0f);
	EXPECT_FLOAT_EQ(actual, expected.getScore().enemyDamageReduce);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(machine->shots.available(), ammo);
}

INSTANTIATE_TEST_SUITE_P(LegacyExpertAndVolley, NewHorizonsMasteryAttackCountTest, ::testing::Bool());
