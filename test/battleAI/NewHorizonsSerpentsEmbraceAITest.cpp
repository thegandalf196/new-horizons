/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/NewHorizonsSerpentsEmbraceFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
namespace
{
class SerpentEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SerpentEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
	std::shared_ptr<HypotheticBattle> model(CBattleInfoCallback * source)
	{
		return std::make_shared<HypotheticBattle>(this,
			std::shared_ptr<CBattleInfoCallback>(source, [](CBattleInfoCallback *) {}));
	}
};
class NewHorizonsSerpentsEmbraceAITest : public NewHorizonsSerpentsEmbraceBattleTest {};
}
TEST_F(NewHorizonsSerpentsEmbraceAITest, OrdinaryPreviewSelectedFutureAndDescendantsPreserveIndependentHolds)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SerpentEnvironment environment(gameState());
	auto parent = environment.model(battle());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	ASSERT_FALSE(parent->mayHaveSerpentBindings());
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto ap = AttackPossibility::evaluate(BattleAttackInfo(source, target, 0, false),
		source->getPosition(), cache, parent);
	ASSERT_NE(ap.effectPreview, nullptr);
	ASSERT_TRUE(ap.effectPreview->mayHaveSerpentBindings());
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant replay;
	replay.trackAttack(ap, selected, cache);
	auto future = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant forecast;
	forecast.trackAttack(future->getForUpdate(source->unitId()), future->getForUpdate(target->unitId()),
		false, true, cache, future);
	for(const auto & branch : {ap.effectPreview, selected, future})
	{
		const auto * victim = branch->battleGetUnitByID(target->unitId());
		EXPECT_TRUE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*victim));
		EXPECT_EQ(victim->getAvailableHealth(), ap.affectedUnits.front()->getAvailableHealth());
	}
	EXPECT_FALSE(parent->mayHaveSerpentBindings());
	EXPECT_FALSE(sibling->mayHaveSerpentBindings());
	EXPECT_GT(parent->battleGetUnitByID(target->unitId())->acquireState()->getMovementRange(), 0);
	auto child = std::make_shared<HypotheticBattle>(&environment, selected);
	child->nextTurn(source->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*child->battleGetUnitByID(target->unitId())));
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*selected->battleGetUnitByID(target->unitId())));
	ASSERT_TRUE(attack(source, target->getPosition()));
	EXPECT_TRUE(heldBy(source));
	EXPECT_EQ(target->getAvailableHealth(), selected->battleGetUnitByID(target->unitId())->getAvailableHealth());
}
TEST_F(NewHorizonsSerpentsEmbraceAITest, SupportedDirectBonusMutationSetsOnlyItsOwnerAndConservativeBitCopies)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SerpentEnvironment environment(gameState());
	auto parent = environment.model(battle());
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	ASSERT_FALSE(parent->mayHaveSerpentBindings());
	ASSERT_FALSE(child->mayHaveSerpentBindings());
	child->getForUpdate(target->unitId())->addUnitBonus({
		newHorizonsSerpentsEmbrace::marker(source->unitId(), source->creatureId())});
	EXPECT_TRUE(child->mayHaveSerpentBindings());
	EXPECT_FALSE(parent->mayHaveSerpentBindings());
	child->removeUnitBonus(target->unitId(), newHorizonsSerpentsEmbrace::releasePlan(
		*child->battleGetUnitByID(target->unitId()), source->unitId()));
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*child->battleGetUnitByID(target->unitId())));
	auto grandchild = std::make_shared<HypotheticBattle>(&environment, child);
	EXPECT_TRUE(grandchild->mayHaveSerpentBindings()) << "An empty current marker set never clears copied admission";
}
TEST_F(NewHorizonsSerpentsEmbraceAITest, RootAdmissionUsesVisibleMarkersWithoutHiddenHeroPolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(source, target);
	SerpentEnvironment environment(gameState());
	auto player = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	ASSERT_EQ(player->battleGetFightingHero(BattleSide::ATTACKER), nullptr);
	auto parent = std::make_shared<HypotheticBattle>(&environment, player);
	EXPECT_TRUE(parent->mayHaveSerpentBindings());
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::qualifiesAttack(*parent, source, target, false, true, false));
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	child->nextTurn(source->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*child->battleGetUnitByID(target->unitId())));
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*parent->battleGetUnitByID(target->unitId())));
}
TEST_F(NewHorizonsSerpentsEmbraceAITest, ChangedAllegianceSourceDeathPublishesLateCompletePreviewAndSelectedCleanup)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(source), PlayerColor(1));
	auto * incoming = addStack(BattleSide::ATTACKER, CreatureID(CreatureID::decode("core:pikeman")),
		BattleHex(leftHex - GameConstants::BFIELD_WIDTH), 1000);
	forceMaximumDamage(incoming);
	battle()->activeStack = incoming->unitId();
	SerpentEnvironment environment(gameState());
	{
		auto ordinary = environment.model(battle());
		DamageCache cache;
		cache.buildDamageCache(ordinary, BattleSide::ATTACKER);
		const auto baseline = AttackPossibility::evaluate(BattleAttackInfo(incoming, source, 0, false),
			incoming->getPosition(), cache, ordinary);
		ASSERT_EQ(baseline.effectPreview, nullptr) << "This fixture must exercise the otherwise unowned death path";
	}
	// Install an old hold after the original source changed sides. Current source
	// entitlement cannot recreate it; the status carries its pre-control provenance.
	source->removeBonusesRecursive(Selector::type()(BonusType::HYPNOTIZED));
	install(source, target);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	auto parent = environment.model(battle());
	ASSERT_TRUE(parent->mayHaveSerpentBindings());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto ap = AttackPossibility::evaluate(BattleAttackInfo(incoming, source, 0, false),
		incoming->getPosition(), cache, parent);
	ASSERT_NE(ap.effectPreview, nullptr);
	ASSERT_FALSE(ap.effectPreview->battleGetUnitByID(source->unitId())->alive());
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*ap.effectPreview->battleGetUnitByID(target->unitId())));
	EXPECT_EQ(ap.effectPreview->battleGetUnitByID(incoming->unitId())->getAvailableHealth(), ap.attackerState->getAvailableHealth());
	EXPECT_TRUE(parent->battleGetUnitByID(source->unitId())->alive());
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*parent->battleGetUnitByID(target->unitId())));
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*sibling->battleGetUnitByID(target->unitId())));
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant replay;
	replay.trackAttack(ap, selected, cache);
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*selected->battleGetUnitByID(target->unitId())));
	EXPECT_EQ(selected->battleGetUnitByID(source->unitId())->getAvailableHealth(), ap.effectPreview->battleGetUnitByID(source->unitId())->getAvailableHealth());
}

TEST_F(NewHorizonsSerpentsEmbraceAITest, RepeatedSameSourceProjectionIsIdempotentAndNextActivationAllowsFreshAdd)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	SerpentEnvironment environment(gameState());
	auto parent = environment.model(battle());
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	child->projectSerpentBinding(source->unitId(), target->unitId(), 1, true);
	const auto first = newHorizonsSerpentsEmbrace::releasePlan(*child->battleGetUnitByID(target->unitId()), source->unitId());
	ASSERT_EQ(first.size(), 1u);
	for(int strike = 0; strike < 3; ++strike)
		child->projectSerpentBinding(source->unitId(), target->unitId(), 1, true);
	const auto repeated = newHorizonsSerpentsEmbrace::releasePlan(*child->battleGetUnitByID(target->unitId()), source->unitId());
	ASSERT_EQ(repeated.size(), 1u);
	EXPECT_EQ(repeated.front().statusIdentity, first.front().statusIdentity);
	EXPECT_EQ(repeated.front().turnsRemain, 0);
	child->nextTurn(source->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*child->battleGetUnitByID(target->unitId())));
	child->projectSerpentBinding(source->unitId(), target->unitId(), 1, true);
	EXPECT_EQ(newHorizonsSerpentsEmbrace::releasePlan(*child->battleGetUnitByID(target->unitId()), source->unitId()).size(), 1u);
	EXPECT_FALSE(parent->mayHaveSerpentBindings());
}
TEST_F(NewHorizonsSerpentsEmbraceAITest, ImpliedCloneDeathReleasesItsHoldWithoutChangingSharedCloneForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * clone = addStack(BattleSide::ATTACKER, creatureByName("core:naga"), BattleHex(3, 2), 10);
	source->cloneID = clone->unitId();
	clone->cloned = true;
	install(clone, target);
	SerpentEnvironment environment(gameState());
	{
	auto parent = environment.model(battle());
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto priorCloneHealth = selected->battleGetUnitByID(clone->unitId())->getAvailableHealth();
	selected->updateUnit(source->unitId(), source->save(), -source->getAvailableHealth());
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*selected->battleGetUnitByID(target->unitId())));
	EXPECT_GT(selected->battleGetUnitByID(target->unitId())->acquireState()->getMovementRange(), 0);
	EXPECT_TRUE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*parent->battleGetUnitByID(target->unitId())));
	// Characterize the pre-existing detached clone-HP gap separately from hold legality.
	EXPECT_EQ(selected->battleGetUnitByID(clone->unitId())->getAvailableHealth(), priorCloneHealth);
	}
	battle()->updateUnit(source->unitId(), source->save(), -source->getAvailableHealth());
	EXPECT_TRUE(clone->ghostPending);
	EXPECT_GT(target->getMovementRange(), 0);
}

TEST_F(NewHorizonsSerpentsEmbraceAITest, ConstructedAdmittedHitRechecksChangedAllegianceWithoutClaimingLegalCommand)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * phoenix = preparePhoenix(true);
	ASSERT_NO_FATAL_FAILURE(assertPhoenixMeleeGeometry(phoenix));
	ASSERT_EQ(battle()->battleGetOwner(phoenix), PlayerColor(1));
	ASSERT_FALSE(battle()->battleCanAttackUnitAction(source, phoenix));
	SerpentEnvironment environment(gameState());
	auto parent = environment.model(battle());
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	// This is an explicitly constructed admitted-hit flag, not a legal native
	// command: current physical-side hostility rejects this original-side target.
	// Challenge the post-hit guard after a real allegiance-marker removal.
	child->getForUpdate(phoenix->unitId())->removeUnitBonus(Selector::type()(BonusType::HYPNOTIZED));
	ASSERT_EQ(child->battleGetOwner(child->battleGetUnitByID(phoenix->unitId())), PlayerColor(0));
	EXPECT_NO_THROW(child->projectSerpentBinding(source->unitId(), phoenix->unitId(), 1, true));
	EXPECT_FALSE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*child->battleGetUnitByID(phoenix->unitId())));
	EXPECT_FALSE(child->mayHaveSerpentBindings());
	EXPECT_EQ(parent->battleGetOwner(parent->battleGetUnitByID(phoenix->unitId())), PlayerColor(1));
}

TEST_F(NewHorizonsSerpentsEmbraceAITest, CandidateExportRejectsOwnHypnotizedPhoenixAndOffersLegalEnemy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * phoenix = preparePhoenix(true);
	ASSERT_NO_FATAL_FAILURE(assertPhoenixMeleeGeometry(phoenix));
	ASSERT_EQ(battle()->battleGetOwner(phoenix), PlayerColor(1));
	ASSERT_FALSE(battle()->battleCanAttackUnitAction(source, phoenix));
	SerpentEnvironment environment(gameState());
	{
		auto parent = environment.model(battle());
		DamageCache cache;
		cache.buildDamageCache(parent, BattleSide::ATTACKER);
		PotentialTargets candidates(parent->battleGetUnitByID(source->unitId()), cache, parent);
		EXPECT_FALSE(std::ranges::any_of(candidates.possibleAttacks, [phoenix](const auto & ap)
			{ return ap.attack.defender->unitId() == phoenix->unitId(); }));
		EXPECT_FALSE(parent->mayHaveSerpentBindings());
		EXPECT_FALSE(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*parent->battleGetUnitByID(phoenix->unitId())));
	}
	// Finish borrowing the first native setup before changing its occupants.
	battle()->removeUnit(phoenix->unitId());
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:phoenix"), BattleHex(rightHex), 10);
	ASSERT_NO_FATAL_FAILURE(assertPhoenixMeleeGeometry(enemy));
	ASSERT_TRUE(battle()->battleCanAttackUnitAction(source, enemy));
	auto parent = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	PotentialTargets candidates(parent->battleGetUnitByID(source->unitId()), cache, parent);
	const auto offered = std::ranges::find_if(candidates.possibleAttacks, [enemy](const auto & ap)
		{ return ap.attack.defender->unitId() == enemy->unitId(); });
	ASSERT_NE(offered, candidates.possibleAttacks.end());
	ASSERT_NE(offered->effectPreview, nullptr);
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant replay;
	replay.trackAttack(*offered, selected, cache);
	EXPECT_EQ(newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*selected->battleGetUnitByID(enemy->unitId())),
		newHorizonsSerpentsEmbrace::blocksVoluntaryMovement(*offered->effectPreview->battleGetUnitByID(enemy->unitId())));
	EXPECT_FALSE(parent->mayHaveSerpentBindings());
	// Native Phoenix HP/rebirth forecasting is a separate shared omission; the
	// native legal-route test proves live rebirth/binding, without HP parity here.
}
