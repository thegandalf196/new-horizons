/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsAerialSupremacyFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/AerialRetaliationProjection.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsParalyzingGaze.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/spells/NewHorizonsMagic.h"
namespace
{
class AerialEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit AerialEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
	std::shared_ptr<HypotheticBattle> model(CBattleInfoCallback * source)
	{
		return std::make_shared<HypotheticBattle>(this,
			std::shared_ptr<CBattleInfoCallback>(source, [](CBattleInfoCallback *) {}));
	}
};
template<typename T> auto wireBytes(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}
class NewHorizonsAerialSupremacyAITest : public NewHorizonsAerialSupremacyBattleTest
{
protected:
	AttackPossibility preview(const std::shared_ptr<HypotheticBattle> & model, DamageCache & cache)
	{
		return AttackPossibility::evaluate(BattleAttackInfo(model->battleGetUnitByID(shooter->unitId()),
			model->battleGetUnitByID(griffin->unitId()), 0, true), shooter->getPosition(), cache, model);
	}
};
}
TEST_F(NewHorizonsAerialSupremacyAITest, PreviewKeepsOriginAndCounterResourcesPrivateWithoutRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, model);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const int liveCounterAllowance = griffin->counterAttacks.available();
	const int liveShots = griffin->shots.available();
	const auto before = griffin->save();
	const auto shooterBefore = shooter->save();
	const auto serverRng = wireBytes(*gameHandler->randomizer);
	const auto gameRng = wireBytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto defaultRng = wireBytes(CRandomGenerator::getDefault());
	const auto ap = preview(model, cache);
	const auto immediatelyAfterPreview = griffin->save();
	EXPECT_EQ(immediatelyAfterPreview, before)
		<< "before=" << before.toCompactString() << " after=" << immediatelyAfterPreview.toCompactString();
	ASSERT_TRUE(ap.effectPreview);
	const auto projected = ap.effectPreview->getForUpdate(griffin->unitId());
	EXPECT_EQ(projected->getPosition(), griffin->getPosition());
	EXPECT_EQ(projected->counterAttacks.available(), liveCounterAllowance - 1);
	EXPECT_EQ(projected->aerialRetaliationRound, battle()->getRound());
	EXPECT_EQ(projected->shots.available(), liveShots);
	EXPECT_EQ(std::ranges::count_if(ap.fortuneStrikes, [](const auto & s) { return s.aerialExcursion.has_value(); }), 1);
	EXPECT_EQ(griffin->save(), before);
	EXPECT_EQ(shooter->save(), shooterBefore);
	EXPECT_EQ(model->getForUpdate(griffin->unitId())->aerialRetaliationRound, -1);
	EXPECT_EQ(sibling->getForUpdate(griffin->unitId())->aerialRetaliationRound, -1);
	EXPECT_EQ(wireBytes(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(wireBytes(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	EXPECT_EQ(wireBytes(CRandomGenerator::getDefault()), defaultRng);
}
TEST_F(NewHorizonsAerialSupremacyAITest, RangedFirstStrikeCanKillShooterBeforeItsOpeningShot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	griffin->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::FIRST_STRIKE,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)));
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto ap = preview(model, cache);
	ASSERT_TRUE(ap.attackerState);
	EXPECT_FALSE(ap.attackerState->alive());
	EXPECT_EQ(std::ranges::count_if(ap.fortuneStrikes, [](const auto & s) { return s.shooting; }), 0);
	EXPECT_EQ(ap.attackerState->shots.available(), shooter->shots.available());
	const auto hit = std::ranges::find_if(ap.fortuneStrikes, [](const auto & s) { return s.aerialExcursion.has_value(); });
	ASSERT_NE(hit, ap.fortuneStrikes.end());
	EXPECT_FALSE(hit->shooting);
	EXPECT_TRUE(hit->retaliation);
}
TEST_F(NewHorizonsAerialSupremacyAITest, SelectedReplayRetainsOriginRoundAndOneOrdinaryCounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto ap = preview(model, cache);
	auto selected = std::make_shared<HypotheticBattle>(&environment, model);
	BattleExchangeVariant exchange;
	exchange.trackAttack(ap, selected, cache);
	const auto replayed = selected->getForUpdate(griffin->unitId());
	EXPECT_EQ(replayed->getPosition(), griffin->getPosition());
	EXPECT_EQ(replayed->counterAttacks.available(), griffin->counterAttacks.available() - 1);
	EXPECT_EQ(replayed->aerialRetaliationRound, battle()->getRound());
	EXPECT_EQ(selected->getForUpdate(shooter->unitId())->getAvailableHealth(), ap.attackerState->getAvailableHealth());
	EXPECT_EQ(model->getForUpdate(griffin->unitId())->aerialRetaliationRound, -1);
}
TEST_F(NewHorizonsAerialSupremacyAITest, FutureProbeIsIsolatedAndAppliedFullSequenceIsNotRepeated)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	auto actor = model->getForUpdate(shooter->unitId());
	auto target = model->getForUpdate(griffin->unitId());
	const auto before = actor->save();
	BattleExchangeVariant probe;
	probe.trackAttack(actor, target, true, true, cache, model, true);
	EXPECT_EQ(actor->save(), before);
	EXPECT_EQ(target->aerialRetaliationRound, -1);
	BattleExchangeVariant applied;
	BattleExchangeAttackResult result;
	applied.trackAttack(actor, target, true, true, cache, model, false, true,
		nullptr, nullptr, nullptr, &result);
	EXPECT_TRUE(result.completedAction);
	EXPECT_EQ(target->aerialRetaliationRound, battle()->getRound());
	EXPECT_EQ(target->getPosition(), griffin->getPosition());
}
TEST_F(NewHorizonsAerialSupremacyAITest, HistoricalAbsentPolicyDoesNotCreateTypedCounter)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto ap = preview(model, cache);
	EXPECT_FALSE(std::ranges::any_of(ap.fortuneStrikes,
		[](const auto & s) { return s.aerialAdmission || s.aerialExcursion; }));
	EXPECT_TRUE(ap.attackerState->alive());
}
TEST_F(NewHorizonsAerialSupremacyAITest, GazeCancellationSpendsCounterAndRoundWithoutPhysicalStrikeOrAmmo)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	griffin->addNewBonus(std::make_shared<Bonus>(newHorizonsParalyzingGaze::marker()));
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto ap = preview(model, cache);
	ASSERT_TRUE(ap.effectPreview);
	const auto projected = ap.effectPreview->getForUpdate(griffin->unitId());
	EXPECT_EQ(projected->counterAttacks.available(), griffin->counterAttacks.available() - 1);
	EXPECT_EQ(projected->aerialRetaliationRound, battle()->getRound());
	EXPECT_EQ(projected->shots.available(), griffin->shots.available());
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*projected));
	EXPECT_FALSE(std::ranges::any_of(ap.fortuneStrikes, [](const auto & s) { return s.aerialExcursion.has_value(); }));
	ASSERT_TRUE(std::ranges::any_of(ap.fortuneStrikes, [](const auto & s)
		{ return s.aerialAdmission && s.gazeConsumption && !s.gazeConsumption->ignoredByDefiant; }));
	EXPECT_TRUE(ap.attackerState->alive());
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*griffin));
}
TEST_F(NewHorizonsAerialSupremacyAITest, ForgedTypedCounterRejectsBeforeMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	auto actor = model->getForUpdate(griffin->unitId());
	auto target = model->getForUpdate(shooter->unitId());
	auto excursion = newHorizonsAerialSupremacy::prepare(*model, actor.get(), target.get());
	ASSERT_TRUE(excursion);
	const auto before = actor->save();
	excursion->origin = BattleHex(1, 1);
	BattleAttackInfo counter(actor.get(), target.get(), 0, false);
	counter.retaliation = true;
	EXPECT_THROW(AttackPossibility::evaluate(counter, actor->getPosition(), cache, model, false, excursion), std::runtime_error);
	EXPECT_EQ(actor->save(), before);
}
TEST_F(NewHorizonsAerialSupremacyAITest, RoyalHasNoAdditionalRoundCapButStillUsesNormalCounterCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(4, 3), 200);
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto ap = preview(model, cache);
	ASSERT_TRUE(ap.effectPreview);
	const auto projected = ap.effectPreview->getForUpdate(griffin->unitId());
	EXPECT_EQ(projected->aerialRetaliationRound, -1);
	EXPECT_FALSE(projected->counterAttacks.isLimited());
	EXPECT_EQ(projected->counterAttacks.available(), griffin->counterAttacks.available());
	EXPECT_EQ(projected->getPosition(), griffin->getPosition());
}

TEST_F(NewHorizonsAerialSupremacyAITest, TypedContactCollateralMatchesSharedQueryWithoutMovingDamageOrigin)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	griffin->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ATTACKS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	const auto planned = newHorizonsAerialSupremacy::prepare(*battle(), griffin, shooter);
	ASSERT_TRUE(planned);
	const auto footprint = griffin->getHexes(planned->landing);
	for(const auto hex : BattleHexArray::getNeighbouringTiles(planned->landing))
		if(!footprint.contains(hex) && hex != shooter->getPosition()
			&& !battle()->battleGetUnitByPos(hex, true))
			addStack(BattleSide::ATTACKER, creatureByName("core:peasant"), hex, 1);
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	const auto actual = newHorizonsAerialSupremacy::prepare(*model,
		model->battleGetUnitByID(griffin->unitId()), model->battleGetUnitByID(shooter->unitId()));
	ASSERT_TRUE(actual);
	const auto collateral = model->getAerialAttackedBattleUnits(*actual).first;
	ASSERT_FALSE(collateral.empty());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto ap = preview(model, cache);
	const auto hit = std::ranges::find_if(ap.fortuneStrikes, [](const auto & s) { return s.aerialExcursion.has_value(); });
	ASSERT_NE(hit, ap.fortuneStrikes.end());
	EXPECT_EQ(hit->aerialExcursion->origin, griffin->getPosition());
	for(const auto * unit : collateral)
		EXPECT_TRUE(std::ranges::any_of(hit->hits, [unit](const auto & damage) { return damage.first == unit->unitId(); }));
	EXPECT_EQ(ap.effectPreview->getForUpdate(griffin->unitId())->getPosition(), griffin->getPosition());
}
TEST_F(NewHorizonsAerialSupremacyAITest, AerialVampirismHealingSurvivesPreviewAndOuterSelectedReplay)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	int64_t wound = 500;
	griffin->damage(wound);
	const SpellID spell(SpellID::decode(std::string(newHorizonsMagic::SHADOW_VAMPIRISM_SPELL)));
	Bonus status(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER, BonusSource::SPELL_EFFECT,
		5000, BonusSourceID(spell), BonusSubtypeID(ScriptID(ScriptID::decode(std::string(newHorizonsMagic::SHADOW_VAMPIRISM_STATUS)))));
	status.turnsRemain = 3;
	griffin->addNewBonus(std::make_shared<Bonus>(status));
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto ap = preview(model, cache);
	const auto hit = std::ranges::find_if(ap.fortuneStrikes, [](const auto & s) { return s.aerialExcursion.has_value(); });
	ASSERT_NE(hit, ap.fortuneStrikes.end());
	EXPECT_GT(hit->aerialVampirismHealing, 0);
	auto selected = std::make_shared<HypotheticBattle>(&environment, model);
	BattleExchangeVariant exchange;
	exchange.trackAttack(ap, selected, cache);
	EXPECT_EQ(selected->getForUpdate(griffin->unitId())->getAvailableHealth(),
		ap.effectPreview->getForUpdate(griffin->unitId())->getAvailableHealth());
}
TEST_F(NewHorizonsAerialSupremacyAITest, TypedSourceAftermathReflectsBeforeHealingAndNeverRevives)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	AerialEnvironment environment(gameState());
	auto parent = environment.model(battle());
	auto model = std::make_shared<HypotheticBattle>(&environment, parent);
	auto actor = model->getForUpdate(griffin->unitId());
	int64_t wound = 100;
	actor->damage(wound);
	FortuneStrikeProjection strike;
	strike.attackerId = griffin->unitId();
	strike.defenderId = shooter->unitId();
	strike.aerialExcursion = newHorizonsAerialSupremacy::prepare(*model,
		actor.get(), model->battleGetUnitByID(shooter->unitId()));
	ASSERT_TRUE(strike.aerialExcursion);
	strike.aerialReflectionDamage = 20;
	strike.aerialVampirismHealing = 7;
	const auto before = actor->getAvailableHealth();
	EXPECT_EQ(aerialRetaliationAI::replaySourceAftermath(*model, strike), 20);
	EXPECT_EQ(actor->getAvailableHealth(), before - 13);
	EXPECT_EQ(actor->getPosition(), griffin->getPosition());
	EXPECT_EQ(parent->getForUpdate(griffin->unitId())->getAvailableHealth(), griffin->getAvailableHealth());
	strike.aerialReflectionDamage = actor->getAvailableHealth();
	strike.aerialVampirismHealing = 999;
	aerialRetaliationAI::replaySourceAftermath(*model, strike);
	EXPECT_FALSE(actor->alive());
}

TEST_F(NewHorizonsAerialSupremacyAITest, TypedCounterRequiresOriginalDamageAnchorsBeforeStrictAdmission)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	auto actor = model->getForUpdate(griffin->unitId());
	auto target = model->getForUpdate(shooter->unitId());
	const auto excursion = newHorizonsAerialSupremacy::prepare(*model, actor.get(), target.get());
	ASSERT_TRUE(excursion);
	ASSERT_NE(excursion->landing, excursion->origin);
	BattleAttackInfo counter(actor.get(), target.get(), 0, false);
	counter.retaliation = true;
	const auto before = actor->save();
	EXPECT_THROW(aerialRetaliationAI::validateContext(*model, counter, excursion->origin, excursion), std::runtime_error);
	counter.attackerPos = excursion->origin;
	counter.defenderPos = excursion->target;
	EXPECT_NO_THROW(aerialRetaliationAI::validateContext(*model, counter, excursion->origin, excursion));
	counter.attackerPos = excursion->landing;
	EXPECT_THROW(aerialRetaliationAI::validateContext(*model, counter, excursion->origin, excursion), std::runtime_error);
	EXPECT_EQ(actor->save(), before);
	EXPECT_EQ(actor->getPosition(), excursion->origin);
}

TEST_F(NewHorizonsAerialSupremacyAITest, ColdPreviewKeepsWholeLiveStateBeforeAnyResourceOracle)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	AerialEnvironment environment(gameState());
	auto model = environment.model(battle());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, model);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	// No live resource availability query precedes this cold snapshot.
	const auto before = griffin->save();
	const auto shooterBefore = shooter->save();
	const auto serverRng = wireBytes(*gameHandler->randomizer);
	const auto gameRng = wireBytes(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto defaultRng = wireBytes(CRandomGenerator::getDefault());
	const auto ap = preview(model, cache);
	const auto immediatelyAfterPreview = griffin->save();
	const auto shooterImmediatelyAfterPreview = shooter->save();
	EXPECT_EQ(immediatelyAfterPreview, before)
		<< "before=" << before.toCompactString() << " after=" << immediatelyAfterPreview.toCompactString();
	EXPECT_EQ(shooterImmediatelyAfterPreview, shooterBefore)
		<< "before=" << shooterBefore.toCompactString() << " after=" << shooterImmediatelyAfterPreview.toCompactString();
	EXPECT_EQ(wireBytes(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(wireBytes(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	EXPECT_EQ(wireBytes(CRandomGenerator::getDefault()), defaultRng);
	ASSERT_TRUE(ap.effectPreview);
	EXPECT_EQ(model->getForUpdate(griffin->unitId())->save(), before);
	EXPECT_EQ(sibling->getForUpdate(griffin->unitId())->save(), before);
}
