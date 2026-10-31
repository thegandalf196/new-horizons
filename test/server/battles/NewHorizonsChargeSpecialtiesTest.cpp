/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../server/battles/BattleActionProcessor.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsChargeSpecialties.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/scripting/ScriptService.h"
#include "../../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../../server/battles/BattlePrimaryStrikeAftermath.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/AttackPreviewHitContext.h"
#include "../../../AI/BattleAI/BattleExchangeAftermath.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/VisibleCandidateWorld.h"
#include "../../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <vcmi/Environment.h>
#endif
namespace
{
namespace charge = newHorizonsChargeSpecialties;
// Positive cases require the separately composed two-row/captured-domain cohort.
// No installed defaults or compatibility admission are changed by these tests.
class ChargeCallerServer : public RecordingGameServer
{
public:
	uint32_t actorId = 0;
	uint32_t victimId = 0;
	std::vector<int> defenseBeforeCounter;
	std::vector<bool> formationBeforeCounter;
	std::vector<BattleStackMoved> moves;
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * moved = dynamic_cast<const BattleStackMoved *>(&pack))
			moves.push_back(*moved);
		if(const auto * attack = dynamic_cast<const BattleAttack *>(&pack);
			attack && attack->counter() && attack->stackAttacking == victimId)
		{
			const auto * actor = gameState->getBattle(attack->battleID)->battleGetUnitByID(actorId);
			defenseBeforeCounter.push_back(actor->getDefense(false));
			formationBeforeCounter.push_back(actor->hasBonus(CSelector(charge::isLanceFormation)));
		}
		RecordingGameServer::applyPack(pack);
	}
};
class NewHorizonsChargeSpecialtiesTest : public HeroCommandFixture
{
protected:
	int captureMode = 0;
	ChargeCallerServer observed;
	void TearDown() override
	{
		gameHandler.reset(); observed.gameState.reset();
		HeroCommandFixture::TearDown();
	}
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		policy["lanceFormationDefensePerHex"].Integer() = captureMode == 2 ? 0 : 1;
		policy["lanceFormationMaximumDefense"].Integer() = 5;
		policy["stormWingsDefenseIgnorePercent"].Integer() = captureMode == 2 ? 0 : 25;
		policy["stormWingsMinimumFlightDistance"].Integer() = 4;
		if(captureMode == 1)
			for(const char * key : {"lanceFormationDefensePerHex", "lanceFormationMaximumDefense",
				"stormWingsDefenseIgnorePercent", "stormWingsMinimumFlightDistance"})
				policy.Struct().erase(key);
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(bool shiva = false, bool upgraded = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(shiva ? "core:shiva" : "core:tyris")), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creatureByName(shiva ? (upgraded ? "core:thunderbird" : "core:roc")
				: (upgraded ? "core:champion" : "core:cavalier")), 20}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:walkingDead"), 200}});
		startWithMap(std::move(builder)); server.gameState = gameState(); observed.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(observed, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0)); defenderSideHero = findHeroByOwner(PlayerColor(1));
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0))
				(unit->unitSide() == BattleSide::ATTACKER ? actor : victim) = unit.get();
		ASSERT_NE(actor, nullptr); ASSERT_NE(victim, nullptr);
		actor->setPosition(BattleHex(3, 5)); victim->setPosition(BattleHex(4, 5));
		observed.actorId = actor->unitId(); observed.victimId = victim->unitId();
	}

	void deterministicNative(bool suppressCounter)
	{
		for(auto * unit : {actor, victim})
		{
			for(const auto type : {BonusType::NO_MORALE, BonusType::NO_LUCK})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type,
					BonusSource::OTHER, 1, BonusSourceID()));
			for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			{
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
					BonusSource::OTHER, unit == actor ? 1000 : 10, BonusSourceID(), BonusSubtypeID(), bound));
				for(const auto subtype : {BonusCustomSubtype::creatureDamageMin, BonusCustomSubtype::creatureDamageMax})
					unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE,
						BonusSource::OTHER, unit == actor ? 2 : 1, BonusSourceID(), subtype, bound));
			}
		}
		if(suppressCounter)
			actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION,
				BonusSource::OTHER, 1, BonusSourceID()));
	}
	void fixedDefense(int value)
	{
		victim->removeBonuses(CSelector([](const Bonus * bonus) { return bonus->stacking == "charge-test-defense"; }));
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
		{
			auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
				BonusSource::OTHER, value, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE), bound);
			bonus->stacking = "charge-test-defense";
			victim->addNewBonus(bonus);
		}
	}
	DamageEstimation expectedAttack(int distance, int flight, BattleHex destination)
	{
		BattleAttackInfo expected(actor, victim, distance, false);
		expected.attackerPos = destination; expected.defenderPos = victim->getPosition();
		expected.voluntaryFlightDistance = flight;
		return battle()->calculateDmgRange(expected);
	}
	std::vector<int64_t> primaryDamageReceived() const
	{
		std::vector<int64_t> result;
		for(const auto & attack : observed.attacks)
			if(attack.stackAttacking == actor->unitId() && !attack.counter())
				for(const auto & hit : attack.bsa)
					if(hit.stackAttacked == victim->unitId() && !(hit.flags & BattleStackAttacked::SECONDARY))
						result.push_back(hit.damageAmount);
		return result;
	}
	int64_t rawDefenseControl(int defense, int mark, int storm, int intrinsicIgnore = 0, bool physical = true)
	{
		fixedDefense(defense);
		actor->removeBonuses(CSelector([](const Bonus * bonus) { return bonus->stacking == "charge-test-ignore"; }));
		if(intrinsicIgnore > 0)
		{
			auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::ENEMY_DEFENCE_REDUCTION,
				BonusSource::OTHER, intrinsicIgnore, BonusSourceID());
			bonus->stacking = "charge-test-ignore"; actor->addNewBonus(bonus);
		}
		DamageAttackInfo info;
		info.attacker = actor; info.defender = victim;
		info.attackerHex = actor->getPosition(); info.defenderHex = victim->getPosition();
		info.physicalDamage = physical;
		info.coordinatedMeleeDefenseIgnorePercent = mark; info.stormWingsDefenseIgnorePercent = storm;
		info.attackFactorPerPoint = 0.05; info.attackFactorCap = 100;
		info.defenseFactorPerPoint = 0.025; info.defenseFactorCap = 100;
		const auto * calculator = LIBRARY->scriptTypes()->getDamageCalculator();
		if(!calculator) throw std::runtime_error("Missing native calculator for charge controls");
		const auto damage = calculator->calculate(*battle(), info).damage;
		EXPECT_EQ(damage.min, damage.max);
		return damage.min;
	}
};
#if ENABLE_BATTLE_AI
class ChargeEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ChargeEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}
TEST(NewHorizonsChargeMathTest, CommittedHexCountRatherThanTerrainCostControlsCappedDefense)
{
	EXPECT_EQ(charge::defenseFromCharge(0, 1, 5), 0);
	EXPECT_EQ(charge::defenseFromCharge(1, 1, 5), 1);
	EXPECT_EQ(charge::defenseFromCharge(4, 1, 5), 4);
	EXPECT_EQ(charge::defenseFromCharge(7, 1, 5), 5);
	EXPECT_EQ(charge::defenseFromCharge(7, 0, 5), 0);
	EXPECT_THROW(charge::defenseFromCharge(-1, 1, 5), std::invalid_argument);
	EXPECT_THROW(charge::defenseFromCharge(4, 1, 0), std::invalid_argument);
}
TEST_F(NewHorizonsChargeSpecialtiesTest, TyrisUsesNativeChargeDistanceWithoutRequiringTheOrder)
{
	prepare();
	ASSERT_TRUE(actor->hasBonusOfType(BonusType::JOUSTING));
	for(int distance : {0, 1, 4, 7})
	{
		BattleAttackInfo attack(actor, victim, distance, false);
		const auto plan = charge::lanceFormation(*battle(), attack);
		if(distance == 0) { EXPECT_TRUE(plan.add.empty()); continue; }
		ASSERT_EQ(plan.add.size(), 1u); EXPECT_EQ(plan.target, actor->unitId());
		EXPECT_EQ(plan.add.front().val, std::min(distance, 5));
		EXPECT_TRUE(charge::isLanceFormation(&plan.add.front()));
	}
}
TEST_F(NewHorizonsChargeSpecialtiesTest, ChampionCompletedZeroDamagePrimaryPreparesFormationButMissingHitDoesNot)
{
	prepare(false, true);
	BattleAttackInfo attack(actor, victim, 4, false);
	EXPECT_TRUE(battlePrimaryStrikeAftermath::prepare(*battle(), attack, {}).lanceFormation.add.empty());
	BattleStackAttacked hit; hit.attackerID = actor->unitId(); hit.stackAttacked = victim->unitId(); hit.damageAmount = 0;
	const auto prepared = battlePrimaryStrikeAftermath::prepare(*battle(), attack, {hit});
	ASSERT_EQ(prepared.lanceFormation.add.size(), 1u); EXPECT_EQ(prepared.lanceFormation.add.front().val, 4);
	// This tests accepted-hit planning, not movement execution or retaliation order.
}
TEST_F(NewHorizonsChargeSpecialtiesTest, NativeChargeImmunityAndNonordinaryBlowsCannotGrantFormation)
{
	prepare(); BattleAttackInfo attack(actor, victim, 4, false);
	for(int excluded = 0; excluded < 7; ++excluded)
	{
		auto other = attack;
		if(excluded == 0) other.retaliation = true;
		if(excluded == 1) other.secondaryAttack = true;
		if(excluded == 2) other.bracePreemptive = true;
		if(excluded == 3) other.preemptiveDamagePercent = 50;
		if(excluded == 4) other.cleaveDamagePercent = 50;
		if(excluded == 5) other.ordinaryAttackIndex = 1;
		if(excluded == 6) other.physicalDamage = false;
		EXPECT_TRUE(charge::lanceFormation(*battle(), other).add.empty());
	}
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::CHARGE_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_TRUE(charge::lanceFormation(*battle(), attack).add.empty());
}
TEST_F(NewHorizonsChargeSpecialtiesTest, FormationRefreshRetainsOneStrongestMarkerAndRejectsForgedShape)
{
	prepare(); BattleAttackInfo attack(actor, victim, 7, false);
	const auto strong = charge::lanceFormation(*battle(), attack); ASSERT_EQ(strong.add.size(), 1u);
	actor->addNewBonus(std::make_shared<Bonus>(strong.add.front()));
	attack.chargeDistance = 2; const auto refreshed = charge::lanceFormation(*battle(), attack);
	ASSERT_EQ(refreshed.remove.size(), 1u); ASSERT_EQ(refreshed.add.size(), 1u);
	EXPECT_EQ(refreshed.add.front().val, 5);
	auto forged = strong.add.front(); forged.source = BonusSource::OTHER;
	EXPECT_THROW(charge::isLanceFormation(&forged), std::runtime_error);
	forged = strong.add.front(); forged.duration = BonusDuration::ONE_BATTLE;
	EXPECT_THROW(charge::isLanceFormation(&forged), std::runtime_error);
}
TEST_F(NewHorizonsChargeSpecialtiesTest, HistoricalAbsencePreservesNoConvertedChargeOrFlightEffect)
{
	captureMode = 1; prepare(); BattleAttackInfo attack(actor, victim, 7, false);
	EXPECT_TRUE(charge::lanceFormation(*battle(), attack).add.empty());
	attack.voluntaryFlightDistance = 7;
	EXPECT_EQ(charge::stormWingsDefenseIgnorePercent(*battle(), attack), 0);
}
TEST_F(NewHorizonsChargeSpecialtiesTest, ExplicitZeroKeepsStormWingsInactive)
{
	captureMode = 2; prepare(true); BattleAttackInfo attack(actor, victim, 4, false); attack.voluntaryFlightDistance = 4;
	EXPECT_EQ(charge::stormWingsDefenseIgnorePercent(*battle(), attack), 0);
}
TEST_F(NewHorizonsChargeSpecialtiesTest, ThunderbirdRequiresExplicitLongVoluntaryFlightForThatFirstBlow)
{
	prepare(true, true); BattleAttackInfo attack(actor, victim, 4, false);
	EXPECT_EQ(charge::stormWingsDefenseIgnorePercent(*battle(), attack), 0);
	attack.voluntaryFlightDistance = 3; attack.chargeDistance = 3;
	EXPECT_EQ(charge::stormWingsDefenseIgnorePercent(*battle(), attack), 0);
	attack.voluntaryFlightDistance = 4; attack.chargeDistance = 4;
	EXPECT_EQ(charge::stormWingsDefenseIgnorePercent(*battle(), attack), 25);
	attack.ordinaryAttackIndex = 1;
	EXPECT_EQ(charge::stormWingsDefenseIgnorePercent(*battle(), attack), 0);
	attack.ordinaryAttackIndex = 0; attack.voluntaryFlightDistance = 5;
	EXPECT_EQ(charge::stormWingsDefenseIgnorePercent(*battle(), attack), 0);
	EXPECT_EQ(attack.reverse().voluntaryFlightDistance, 0);
}
TEST_F(NewHorizonsChargeSpecialtiesTest, RocRetaliationAndReactionFlagsCannotReuseFlightFacts)
{
	prepare(true); BattleAttackInfo attack(actor, victim, 4, false); attack.voluntaryFlightDistance = 4;
	EXPECT_EQ(charge::stormWingsDefenseIgnorePercent(*battle(), attack), 25);
	for(int excluded = 0; excluded < 7; ++excluded)
	{
		auto other = attack;
		if(excluded == 0) other.retaliation = true;
		if(excluded == 1) other.secondaryAttack = true;
		if(excluded == 2) other.bracePreemptive = true;
		if(excluded == 3) other.preemptiveDamagePercent = 50;
		if(excluded == 4) other.cleaveDamagePercent = 50;
		if(excluded == 5) other.shooting = true;
		if(excluded == 6) other.physicalDamage = false;
		EXPECT_EQ(charge::stormWingsDefenseIgnorePercent(*battle(), other), 0);
	}
}
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsChargeSpecialtiesTest, FormationSnapshotsBelongToMaterializedChildrenUntilTheirOwnActivation)
{
	prepare(); const auto plan = charge::lanceFormation(*battle(), BattleAttackInfo(actor, victim, 4, false));
	ASSERT_EQ(plan.add.size(), 1u); actor->addNewBonus(std::make_shared<Bonus>(plan.add.front()));
	ChargeEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto original = parent->getForUpdate(actor->unitId());
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	auto copied = child->getForUpdate(actor->unitId()); auto untouched = sibling->getForUpdate(actor->unitId());
	original->removeUnitBonus(CSelector(Bonus::UntilNextCreatureActivation));
	EXPECT_FALSE(original->hasBonus(CSelector(charge::isLanceFormation)));
	EXPECT_TRUE(copied->hasBonus(CSelector(charge::isLanceFormation)));
	copied->removeUnitBonus(CSelector(Bonus::UntilNextCreatureActivation));
	EXPECT_FALSE(copied->hasBonus(CSelector(charge::isLanceFormation)));
	EXPECT_TRUE(untouched->hasBonus(CSelector(charge::isLanceFormation)));
	EXPECT_TRUE(actor->hasBonus(CSelector(charge::isLanceFormation)));
}
#endif

TEST_F(NewHorizonsChargeSpecialtiesTest, ActualTyrisChargePublishesDefenseBeforeCounterAndExpiresAtNextActivation)
{
	prepare(); deterministicNative(false); victim->setPosition(BattleHex(8, 5));
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const int before = actor->getDefense(false);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, BattleHex(7, 5))));
	ASSERT_FALSE(observed.defenseBeforeCounter.empty());
	EXPECT_EQ(observed.defenseBeforeCounter.front(), before + 4);
	EXPECT_TRUE(observed.formationBeforeCounter.front());
	ASSERT_TRUE(actor->alive()); ASSERT_EQ(battle()->battleActiveUnit(), victim);
	EXPECT_TRUE(actor->hasBonus(CSelector(charge::isLanceFormation)));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), BattleAction::makeDefend(victim)));
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	EXPECT_FALSE(actor->hasBonus(CSelector(charge::isLanceFormation)));
	EXPECT_EQ(actor->getDefense(false), before);
}
TEST_F(NewHorizonsChargeSpecialtiesTest, ActualShivaFlightQualifiesOnlyFirstBlowAndLaterBlowHasNoCarriedDistance)
{
	prepare(true); deterministicNative(true); victim->setPosition(BattleHex(8, 5));
	fixedDefense(actor->getAttack(false) + 20);
	actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::ADDITIONAL_ATTACK,
		BonusSource::OTHER, 1, BonusSourceID()));
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const auto qualified = expectedAttack(4, 4, BattleHex(7, 5)).damage;
	const auto ordinary = expectedAttack(0, 0, BattleHex(7, 5)).damage;
	ASSERT_EQ(qualified.min, qualified.max); ASSERT_EQ(ordinary.min, ordinary.max);
	ASSERT_GT(qualified.min, ordinary.min);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, BattleHex(7, 5))));
	const auto damage = primaryDamageReceived(); ASSERT_EQ(damage.size(), 2u);
	EXPECT_EQ(damage[0], qualified.min); EXPECT_EQ(damage[1], ordinary.min);
	EXPECT_TRUE(std::ranges::any_of(observed.moves, [this](const auto & move)
		{ return move.stack == actor->unitId() && !move.teleporting && move.distance == 4; }));
}
TEST_F(NewHorizonsChargeSpecialtiesTest, ActualForcedFlightUsesTheSameMovementButNoVoluntaryFlightBenefit)
{
	prepare(true); deterministicNative(true); victim->setPosition(BattleHex(8, 5));
	fixedDefense(actor->getAttack(false) + 20);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const auto ordinary = expectedAttack(4, 0, BattleHex(7, 5)).damage;
	const auto qualified = expectedAttack(4, 4, BattleHex(7, 5)).damage;
	ASSERT_GT(qualified.min, ordinary.min);
	BattleActionProcessor automatic(gameHandler->battles.get(), gameHandler.get());
	ASSERT_TRUE(automatic.makeAutomaticBattleAction(*battle(),
		BattleAction::makeMeleeAttack(actor, victim, BattleHex(7, 5))));
	const auto damage = primaryDamageReceived(); ASSERT_EQ(damage.size(), 1u);
	EXPECT_EQ(damage.front(), ordinary.min);
	EXPECT_TRUE(std::ranges::any_of(observed.moves, [this](const auto & move)
		{ return move.stack == actor->unitId() && !move.teleporting && move.distance == 4; }));
}
TEST_F(NewHorizonsChargeSpecialtiesTest, ActualTeleportDoesNotBecomeVoluntaryFlightForTheFollowingStrike)
{
	prepare(true); deterministicNative(true); victim->setPosition(BattleHex(8, 5));
	fixedDefense(actor->getAttack(false) + 20);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::TELEPORT); setTestSpellPointTotal(attackerSideHero, 100);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	BattleAction teleport; teleport.actionType = EActionType::HERO_SPELL;
	teleport.side = BattleSide::ATTACKER; teleport.spell = SpellID::TELEPORT;
	teleport.aimToUnit(actor); teleport.aimToHex(BattleHex(7, 5));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), teleport));
	ASSERT_EQ(actor->getPosition(), BattleHex(7, 5));
	const auto ordinary = expectedAttack(0, 0, actor->getPosition()).damage;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, actor->getPosition())));
	const auto damage = primaryDamageReceived(); ASSERT_EQ(damage.size(), 1u);
	EXPECT_EQ(damage.front(), ordinary.min);
	EXPECT_TRUE(std::ranges::any_of(observed.moves, [this](const auto & move)
		{ return move.stack == actor->unitId() && move.teleporting; }));
}
TEST_F(NewHorizonsChargeSpecialtiesTest, APreviousMoveActionDoesNotCreateASavedFlightCounter)
{
	prepare(true); deterministicNative(true); victim->setPosition(BattleHex(8, 5));
	fixedDefense(actor->getAttack(false) + 20);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeMove(actor, BattleHex(7, 5))));
	ASSERT_EQ(battle()->battleActiveUnit(), victim);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), BattleAction::makeDefend(victim)));
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const auto ordinary = expectedAttack(0, 0, actor->getPosition()).damage;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, actor->getPosition())));
	const auto damage = primaryDamageReceived(); ASSERT_EQ(damage.size(), 1u);
	EXPECT_EQ(damage.front(), ordinary.min);
}
TEST_F(NewHorizonsChargeSpecialtiesTest, DefenseContributorsKeepSeparateFloorsAndTheExistingIntrinsicIgnoreCap)
{
	prepare(true); deterministicNative(true);
	for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
			BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK), bound));
	ASSERT_EQ(actor->getAttack(false), 3);
	// 25% +25% of Defense3 is floor(0.75)+floor(0.75)=0, not floor(1.5)=1.
	EXPECT_EQ(rawDefenseControl(3, 25, 25), rawDefenseControl(3, 0, 0));
	EXPECT_EQ(rawDefenseControl(20, 25, 0), rawDefenseControl(15, 0, 0));
	EXPECT_EQ(rawDefenseControl(20, 0, 25), rawDefenseControl(15, 0, 0));
	EXPECT_EQ(rawDefenseControl(20, 25, 25), rawDefenseControl(10, 0, 0));
	// Native legacy reduction retains its +1: floor(20*.25)+1 +5 +5 =16 ignored.
	EXPECT_EQ(rawDefenseControl(20, 25, 25, 25), rawDefenseControl(4, 0, 0));
	EXPECT_EQ(rawDefenseControl(20, 100, 100, 100), rawDefenseControl(0, 0, 0));
	EXPECT_EQ(rawDefenseControl(20, 25, 25, 0, false), rawDefenseControl(20, 0, 0, 0, false));
}

TEST_F(NewHorizonsChargeSpecialtiesTest, ActualChampionChargeWithShieldOnlyDamageStillGrantsFormationBeforeCounter)
{
	prepare(false, true); deterministicNative(false); victim->setPosition(BattleHex(8, 5));
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const int defense = actor->getDefense(false);
	// A live ward pool is fixture input; the melee request and zero body damage are real.
	victim->guardianSpiritHitPoints = 100000; victim->guardianSpiritRoundsRemaining = 3;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, BattleHex(7, 5))));
	const auto damage = primaryDamageReceived(); ASSERT_EQ(damage.size(), 1u); EXPECT_EQ(damage.front(), 0);
	ASSERT_FALSE(observed.defenseBeforeCounter.empty()); EXPECT_EQ(observed.defenseBeforeCounter.front(), defense + 4);
	EXPECT_TRUE(observed.formationBeforeCounter.front());
}
TEST_F(NewHorizonsChargeSpecialtiesTest, ActualShivaFlightWithHistoricalPolicyAbsenceKeepsOrdinaryDamage)
{
	captureMode = 1; prepare(true); deterministicNative(true); victim->setPosition(BattleHex(8, 5));
	fixedDefense(actor->getAttack(false) + 20);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	const auto ordinary = expectedAttack(4, 0, BattleHex(7, 5)).damage;
	const auto explicitFlight = expectedAttack(4, 4, BattleHex(7, 5)).damage;
	ASSERT_EQ(ordinary.min, explicitFlight.min);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, BattleHex(7, 5))));
	const auto damage = primaryDamageReceived(); ASSERT_EQ(damage.size(), 1u); EXPECT_EQ(damage.front(), ordinary.min);
}

#if ENABLE_BATTLE_AI
namespace
{
template<typename T> auto chargeAIWire(const T & value)
{
	CMemorySerializer memory; memory.oser & value; return memory.extractBuffer();
}

}
TEST_F(NewHorizonsChargeSpecialtiesTest, PrehitReceiptGrantsFormationAfterLethalPrimaryButNotWithoutAcceptedHit)
{
	prepare(); ChargeEnvironment env(gameState());
	auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known);
	auto source = branch->getForUpdate(actor->unitId()), target = branch->getForUpdate(victim->unitId());
	BattleAttackInfo attack(source.get(), target.get(), 4, false);
	FortuneStrikeProjection strike; strike.defenderId = victim->unitId();
	AttackPreviewHitContext::prepareVictimStrike(attack, *branch, {strike, *target, *target, 0, 100, false});
	ASSERT_EQ(strike.lanceFormation.add.size(), 1u);
	battleExchangeAftermath::replayPrimaryStatuses(*branch, attack, strike, {}, false, false, 0);
	EXPECT_FALSE(source->hasBonus(CSelector(charge::isLanceFormation)));
	int64_t lethal = target->getAvailableHealth(); target->damage(lethal);
	ASSERT_FALSE(target->alive());
	battleExchangeAftermath::replayPrimaryStatuses(*branch, attack, strike,
		{{target->unitId(), lethal}}, false, false, lethal);
	EXPECT_TRUE(source->hasBonus(CSelector(charge::isLanceFormation)));
	EXPECT_FALSE(actor->hasBonus(CSelector(charge::isLanceFormation)));
}
TEST_F(NewHorizonsChargeSpecialtiesTest, FullChargePreviewSelectedReplayAndNativeRetaliationAgree)
{
	prepare(); deterministicNative(false); victim->setPosition(BattleHex(8, 5)); beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ChargeEnvironment env(gameState());
	auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto parent = std::make_shared<HypotheticBattle>(&env, known);
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	auto selected = std::make_shared<HypotheticBattle>(&env, parent);
	auto * nativeRng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRng = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(nativeRng, nullptr); ASSERT_NE(fixtureRng, nullptr);
	const auto a = chargeAIWire(*nativeRng), b = chargeAIWire(*fixtureRng);
	const auto liveSource = actor->save(), liveTarget = victim->save();
	const auto reachable = parent->getReachability(actor);
	const BattleHex approach(7, 5);
	const int distance = SpellTargetEvaluator::physicalTravelDistance(reachable, approach);
	ASSERT_EQ(distance, 4); ASSERT_TRUE(parent->isMeleeAttackPossible(actor, victim, approach));
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(actor, victim, distance, false), approach, cache, parent);
	ASSERT_NE(preview.effectPreview, nullptr); ASSERT_FALSE(preview.fortuneStrikes.empty());
	ASSERT_EQ(preview.fortuneStrikes.front().lanceFormation.add.size(), 1u);
	EXPECT_EQ(preview.fortuneStrikes.front().lanceFormation.add.front().val, 4);
	BattleExchangeVariant exchange; exchange.trackAttack(preview, selected, cache);
	const auto * source = selected->battleGetUnitByID(actor->unitId());
	EXPECT_TRUE(source->hasBonus(CSelector(charge::isLanceFormation)));
	EXPECT_EQ(source->getAvailableHealth(), preview.attackerState->getAvailableHealth());
	EXPECT_EQ(actor->save(), liveSource); EXPECT_EQ(victim->save(), liveTarget);
	EXPECT_EQ(sibling->battleGetUnitByID(actor->unitId())->acquireState()->save(), liveSource);
	EXPECT_EQ(chargeAIWire(*nativeRng), a); EXPECT_EQ(chargeAIWire(*fixtureRng), b);
	BattleSetActiveStack activate; activate.battleID = BattleID(0); activate.stack = actor->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE; gameHandler->sendAndApply(activate);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, victim, approach, false)));
	EXPECT_EQ(actor->getAvailableHealth(), source->getAvailableHealth());
	EXPECT_EQ(victim->getAvailableHealth(), selected->battleGetUnitByID(victim->unitId())->getAvailableHealth());
	EXPECT_TRUE(actor->hasBonus(CSelector(charge::isLanceFormation)));
}
TEST_F(NewHorizonsChargeSpecialtiesTest, AlreadyComputedFlyingCandidatesCarryExactFlightButAbsentInputStaysZero)
{
	prepare(true); victim->setPosition(BattleHex(8, 5));
	ChargeEnvironment env(gameState()); auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known); DamageCache cache;
	PotentialTargets targets(branch->battleGetUnitByID(actor->unitId()), cache, branch);
	bool longFlight = false;
	for(const auto & candidate : targets.possibleAttacks)
		if(!candidate.attack.shooting && candidate.attack.chargeDistance >= 4)
		{
			longFlight = true;
			EXPECT_EQ(candidate.attack.voluntaryFlightDistance, candidate.attack.chargeDistance);
			EXPECT_EQ(charge::stormWingsDefenseIgnorePercent(*branch, candidate.attack), 25);
		}
	EXPECT_TRUE(longFlight);
	BattleAttackInfo missing(actor, victim, 4, false);
	EXPECT_EQ(missing.voluntaryFlightDistance, 0);
	EXPECT_EQ(charge::stormWingsDefenseIgnorePercent(*branch, missing), 0);
}
TEST_F(NewHorizonsChargeSpecialtiesTest, VictimMetadataClearsFlightForCollateralLaterAndReactionBlows)
{
	prepare(true);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(10, 5), 10);
	ASSERT_NE(collateral, nullptr);
	ChargeEnvironment env(gameState()); auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known);
	for(int variant = 0; variant < 5; ++variant)
	{
		BattleAttackInfo attack(actor, variant == 1 ? collateral : victim, 4, false); attack.voluntaryFlightDistance = 4;
		if(variant == 3) attack.retaliation = true;
		if(variant == 4) attack.bracePreemptive = true;
		FortuneStrikeProjection strike;
		AttackPreviewHitContext::prepareVictimStrike(attack, *branch,
			{strike, *victim, variant == 1 ? *collateral : *victim, variant == 2 ? 1 : 0, 100, false});
		EXPECT_EQ(attack.voluntaryFlightDistance, variant == 0 ? 4 : 0);
	}
}
TEST_F(NewHorizonsChargeSpecialtiesTest, CacheRemembersFormationAcrossItsActivationExpiry)
{
	prepare(); ChargeEnvironment env(gameState()); auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known);
	const auto plan = charge::lanceFormation(*branch, BattleAttackInfo(actor, victim, 4, false));
	ASSERT_FALSE(plan.add.empty());
	auto source = branch->getForUpdate(actor->unitId()); source->addUnitBonus(plan.add);
	DamageCache remembered; remembered.cacheDamage(victim, source.get(), branch);
	const auto protectedDamage = remembered.getDamage(victim, source.get(), branch);
	source->removeUnitBonus(CSelector(Bonus::UntilNextCreatureActivation));
	DamageCache fresh; const auto expected = fresh.getDamage(victim, source.get(), branch);
	EXPECT_GT(expected, protectedDamage);
	EXPECT_EQ(remembered.getDamage(victim, source.get(), branch), expected);
}
TEST_F(NewHorizonsChargeSpecialtiesTest, HiddenSourceCannotCaptureFormation)
{
	prepare(); ChargeEnvironment env(gameState());
	auto view = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto branch = std::make_shared<HypotheticBattle>(&env, view);
	EXPECT_FALSE(charge::requiresChargeMovementPreview(*branch, *actor, *victim));
	BattleAttackInfo attack(actor, victim, 4, false); FortuneStrikeProjection strike;
	AttackPreviewHitContext::prepareVictimStrike(attack, *branch, {strike, *victim, *victim, 0, 100, false});
	EXPECT_TRUE(strike.lanceFormation.add.empty());
}
#endif

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsChargeSpecialtiesTest, LongFlightUsesSharedShivaDefenseConsumerInFullPrimaryPreview)
{
	prepare(true); victim->setPosition(BattleHex(8, 5));
	victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::PRIMARY_SKILL, BonusSource::OTHER, 20, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE)));
	ChargeEnvironment env(gameState()); auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known);
	const BattleHex approach(7, 5);
	const auto reachability = branch->getReachability(actor);
	ASSERT_TRUE(reachability.params.flying);
	const int distance = SpellTargetEvaluator::physicalTravelDistance(reachability, approach);
	ASSERT_EQ(distance, 4);
	BattleAttackInfo attack(actor, victim, distance, false);
	attack.voluntaryFlightDistance = distance;
	const auto penetrated = branch->calculateDmgRange(attack).damage;
	DamageCache cache;
	const auto first = AttackPossibility::evaluate(attack, approach, cache, branch);
	attack.voluntaryFlightDistance = 0;
	const auto ordinary = branch->calculateDmgRange(attack).damage;
	const auto missing = AttackPossibility::evaluate(attack, approach, cache, branch);
	EXPECT_GT(penetrated.min, ordinary.min);
	EXPECT_GT(first.defenderDamageReduce, missing.defenderDamageReduce);
	EXPECT_FALSE(actor->hasBonus(CSelector(charge::isLanceFormation)));
}
#endif

#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsChargeSpecialtiesTest, FutureActorReusesExistingApproachForChargeWithoutOtherReactionTriggers)
{
	prepare(true); deterministicNative(false); victim->setPosition(BattleHex(8, 5));
	fixedDefense(30);
	ChargeEnvironment env(gameState()); auto known = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&env, known);
	auto source = branch->getForUpdate(actor->unitId()), target = branch->getForUpdate(victim->unitId());
	ASSERT_TRUE(charge::requiresChargeMovementPreview(*branch, *source, *target));
	const auto origin = source->getPosition(); const auto healthBefore = target->getAvailableHealth();
	const auto noFlight = branch->battleExpectedLuckDamage(BattleAttackInfo(source.get(), target.get(), 0, false));
	DamageCache cache; BattleExchangeVariant exchange;
	exchange.trackAttack(source, target, false, true, cache, branch);
	EXPECT_NE(source->getPosition(), origin);
	EXPECT_TRUE(branch->isMeleeAttackPossible(source.get(), target.get()));
	EXPECT_GT(healthBefore - target->getAvailableHealth(), noFlight);
	EXPECT_EQ(actor->getPosition(), origin);
	EXPECT_EQ(victim->getAvailableHealth(), healthBefore);
}
#endif
