/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <stdexcept>
#include "../../lib/CRandomGenerator.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <vcmi/ServerCallback.h>
#include "../server/battles/BattleTestFixture.h"
#include "../../server/CGameHandler.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsGraveDust.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/battle/HealingReceipt.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../AI/BattleAI/InitialExportEffectQueries.h"
#include "../../AI/BattleAI/OwnedStackEffectPreflight.h"
namespace
{
class GraveDustEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit GraveDustEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class GraveDustSnapshotTest : public BattleTestFixture {};
TEST_F(GraveDustSnapshotTest, MaterializedChildrenKeepDustAfterParentAndLiveClearBeforeFirstExplicitQuery)
{
	startGame();
	startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr);
	beginCombat();
	int64_t damage = 15;
	live->damage(damage);
	ASSERT_EQ(damage, 15);
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(live->unitId(), dust);
	const auto originalHealth = live->getAvailableHealth();
	auto environment = std::make_shared<GraveDustEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	EXPECT_FALSE(parent->initialExportSubject()); // No ALL_KNOWING promotion.
	auto parentUnit = parent->getForUpdate(live->unitId());
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto childUnit = child->getForUpdate(live->unitId());
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto siblingUnit = sibling->getForUpdate(live->unitId());
	ASSERT_EQ(child->battleGetUnitByID(live->unitId()), childUnit.get());
	battle()->removeUnitBonus(live->unitId(), dust);
	parent->removeUnitBonus(live->unitId(), dust);
	// No explicit child coefficient/group query preceded removal of both ancestors' copies.
	EXPECT_EQ(battle::healingReceivedReduction(*childUnit), 5000);
	EXPECT_EQ(battle::healingReceivedReduction(*siblingUnit), 5000);
	EXPECT_EQ(battle::healingReceivedReduction(*parentUnit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*live), 0);
	int64_t requested = 1000;
	EXPECT_EQ(childUnit->heal(requested, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints, 8);
	EXPECT_EQ(childUnit->getAvailableHealth(), originalHealth + 8);
	EXPECT_EQ(siblingUnit->getAvailableHealth(), originalHealth);
	EXPECT_EQ(parentUnit->getAvailableHealth(), originalHealth);
	EXPECT_EQ(live->getAvailableHealth(), originalHealth);
	child->removeUnitBonus(live->unitId(), dust);
	EXPECT_EQ(battle::healingReceivedReduction(*childUnit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*siblingUnit), 5000);
}

TEST_F(GraveDustSnapshotTest, OrdinaryProjectionPreflightsPairBeforeAnyOverlayAndMatchesNativeTimeStop)
{
	startGame();
	startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr);
	beginCombat();
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(live->unitId(), dust);
	auto environment = std::make_shared<GraveDustEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), callback);
	ASSERT_FALSE(branch->initialExportSubject());
	auto unit = branch->getForUpdate(live->unitId());
	const auto before = unit->save();
	EXPECT_THROW(unit->updateUnitBonus(newHorizonsGraveDust::records(7500, PlayerColor(0))), std::invalid_argument);
	EXPECT_THROW(unit->removeUnitBonus(std::vector<Bonus>{dust.front()}), std::invalid_argument);
	EXPECT_EQ(unit->save(), before);
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	unit->removeUnitBonus(dust);
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
	unit->addUnitBonus(dust);
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	unit->addUnitBonus(std::vector<Bonus>{stopped});
	ASSERT_TRUE(unit->isTimeStopped());
	unit->removeUnitBonus(dust);
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	EXPECT_THROW(unit->updateUnitBonus(dust), std::invalid_argument);
	EXPECT_EQ(battle::healingReceivedReduction(*live), 5000);
	EXPECT_FALSE(live->isTimeStopped());
}

template<typename Random> auto dustRandomBytes(Random & random)
{
	CMemorySerializer wire; wire.oser & random; return wire.extractBuffer();
}
TEST_F(GraveDustSnapshotTest, InvalidLaterPacketGroupLeavesEarlierTargetParentSiblingAndBothRandomStreamsExact)
{
	startGame(); startBattle();
	auto * first = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	auto * last = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 2);
	ASSERT_NE(first, nullptr); ASSERT_NE(last, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto firstUnit = parent->getForUpdate(first->unitId()), lastUnit = parent->getForUpdate(last->unitId());
	auto siblingFirst = sibling->getForUpdate(first->unitId());
	const auto firstState = firstUnit->save(), lastState = lastUnit->save();
	auto * nativeRandom = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRandom = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(nativeRandom, nullptr); ASSERT_NE(fixtureRandom, nullptr);
	const auto nativeBefore = dustRandomBytes(*nativeRandom), fixtureBefore = dustRandomBytes(*fixtureRandom);
	SetStackEffect packet; packet.battleID = BattleID(0);
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	packet.toAdd.emplace_back(first->unitId(), dust);
	packet.toAdd.emplace_back(last->unitId(), std::vector<Bonus>{dust.front()});
	EXPECT_THROW(parent->getServerCallback()->apply(packet), std::invalid_argument);
	EXPECT_EQ(firstUnit->save(), firstState); EXPECT_EQ(lastUnit->save(), lastState);
	EXPECT_EQ(battle::healingReceivedReduction(*firstUnit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*lastUnit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*siblingFirst), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*first), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*last), 0);
	EXPECT_EQ(dustRandomBytes(*nativeRandom), nativeBefore);
	EXPECT_EQ(dustRandomBytes(*fixtureRandom), fixtureBefore);
}
TEST_F(GraveDustSnapshotTest, PreparedNativeRebirthCleansDustThroughExistingVisitorAndRestoresUnreducedHealing)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0)); battle()->addUnitBonus(live->unitId(), dust);
	GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto unit = parent->getForUpdate(live->unitId()); auto other = sibling->getForUpdate(live->unitId());
	unit->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()),
		Bonus(BonusDuration::ONE_BATTLE, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_TRUE(unit->canCast()); const auto maximum = unit->getAvailableHealth();
	BattleStackAttacked hit; hit.stackAttacked = unit->unitId(); hit.damageAmount = maximum;
	CStack::prepareAttacked(hit, *parent->getServerCallback()->getRNG(), unit->acquireState());
	ASSERT_TRUE(hit.killed()); ASSERT_TRUE(hit.willRebirth());
	StacksInjured injury; injury.battleID = BattleID(0); injury.stacks.push_back(hit);
	parent->getServerCallback()->apply(injury);
	EXPECT_TRUE(unit->alive()); EXPECT_EQ(unit->getAvailableHealth(), maximum);
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
	EXPECT_EQ(battle::healingReceivedReduction(*other), 5000);
	EXPECT_EQ(battle::healingReceivedReduction(*live), 5000);
	int64_t damage = 15; unit->damage(damage);
	int64_t requested = 1000;
	EXPECT_EQ(unit->heal(requested, EHealLevel::HEAL, EHealPower::PERMANENT).healedHealthPoints, 15);
	EXPECT_EQ(unit->getAvailableHealth(), maximum);
}
TEST_F(GraveDustSnapshotTest, TimeStopAlsoProtectsSelectorRemovalInTheOrdinaryOverlay)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = branch->getForUpdate(live->unitId());
	unit->addUnitBonus(newHorizonsGraveDust::records(5000, PlayerColor(0)));
	unit->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID())});
	unit->removeUnitBonus(CSelector(newHorizonsGraveDust::isEffect));
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	unit->removeUnitBonus(Selector::type()(BonusType::TIME_STOP));
	unit->removeUnitBonus(CSelector(newHorizonsGraveDust::isEffect));
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
}
TEST_F(GraveDustSnapshotTest, PartialSelectorRemovalRejectsBeforeEitherCompanionChanges)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = branch->getForUpdate(live->unitId());
	unit->addUnitBonus(newHorizonsGraveDust::records(5000, PlayerColor(0)));
	const auto before = unit->save();
	EXPECT_THROW(unit->removeUnitBonus(Selector::type()(BonusType::HEALING_RECEIVED_REDUCTION_BASIS_POINTS)), std::invalid_argument);
	EXPECT_EQ(unit->save(), before); EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	EXPECT_EQ(unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect))->size(), 2u);
}

TEST_F(GraveDustSnapshotTest, PreparedIndirectInjuryExpiresIndirectStatusAndExhaustedGuardianWithoutChangingSibling)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	Bonus guardian(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT, BonusSource::SPELL_EFFECT, 5, BonusSourceID(SpellID(SpellID::SHIELD)));
	guardian.turnsRemain = 2;
	unit->addUnitBonus({guardian, Bonus(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE,
		BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT, 9, BonusSourceID(SpellID(SpellID::SHIELD)),
		BonusSubtypeID(PrimarySkill::DEFENSE))});
	ASSERT_EQ(unit->guardianSpiritHitPoints, 5);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	const auto before = unit->getAvailableHealth();
	BattleStackAttacked hit; hit.stackAttacked = unit->unitId(); hit.damageAmount = 10;
	CStack::prepareAttacked(hit, *parent->getServerCallback()->getRNG(), unit->acquireState(),
		false, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	ASSERT_NE(hit.flags & BattleStackAttacked::GUARDIAN_SPIRIT_EXHAUSTED, 0);
	StacksInjured injury; injury.battleID = BattleID(0); injury.stacks.push_back(hit);
	parent->getServerCallback()->apply(injury);
	EXPECT_EQ(unit->getAvailableHealth(), before - 5);
	EXPECT_EQ(unit->guardianSpiritHitPoints, 0); EXPECT_EQ(unit->guardianSpiritRoundsRemaining, 0);
	EXPECT_TRUE(unit->getUnstackedBonuses(Selector::type()(BonusType::GUARDIAN_SPIRIT))->empty());
	EXPECT_TRUE(unit->getUnstackedBonuses(Bonus::UntilTakingIndirectDamage)->empty());
	EXPECT_EQ(other->getAvailableHealth(), before); EXPECT_EQ(other->guardianSpiritHitPoints, 5);
	EXPECT_FALSE(other->getUnstackedBonuses(Bonus::UntilTakingIndirectDamage)->empty());
	EXPECT_EQ(live->getAvailableHealth(), before); EXPECT_EQ(live->guardianSpiritHitPoints, 0);
}

TEST_F(GraveDustSnapshotTest, CompleteVectorInEitherOrderPreservesControlCleanupAndRejectedPrefix)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	Bonus guardian(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT, BonusSource::OTHER, 5, BonusSourceID());
	guardian.turnsRemain = 2;
	for(const bool reversed : {false, true})
	{
		auto branch = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		auto unit = branch->getForUpdate(live->unitId());
		unit->addUnitBonus(dust); unit->addUnitBonus({guardian});
		ASSERT_EQ(unit->guardianSpiritHitPoints, 5);
		const auto before = unit->save();
		EXPECT_THROW(unit->removeUnitBonus(std::vector<Bonus>{guardian, dust.front()}), std::invalid_argument);
		EXPECT_EQ(unit->save(), before); EXPECT_EQ(unit->guardianSpiritHitPoints, 5);
		EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
		std::vector<Bonus> removal{dust.at(reversed ? 1 : 0), dust.at(reversed ? 0 : 1), guardian};
		EXPECT_NO_THROW(unit->removeUnitBonus(removal));
		EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
		EXPECT_TRUE(unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect))->empty());
		EXPECT_FALSE(unit->hasBonusOfType(BonusType::GUARDIAN_SPIRIT));
		EXPECT_EQ(unit->guardianSpiritHitPoints, 0); EXPECT_EQ(unit->guardianSpiritRoundsRemaining, 0);
		EXPECT_EQ(live->guardianSpiritHitPoints, 0); EXPECT_EQ(battle::healingReceivedReduction(*live), 0);
	}
}

TEST_F(GraveDustSnapshotTest, AdmittedSameStrengthRefreshKeepsActivationDurationStampAndRejectsStrengthOrCasterChanges)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = branch->getForUpdate(live->unitId());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0)); unit->addUnitBonus(dust);
	const auto markers = unit->getUnstackedBonuses(Selector::type()(BonusType::PHYSICAL_AFFLICTION));
	ASSERT_EQ(markers->size(), 1u); const auto initial = physicalAfflictions::markerMetadata(*markers->front());
	ASSERT_TRUE(initial); ASSERT_GT(initial->applicationOrder, 0);
	for(int attempt = 0; attempt < 2; ++attempt)
	{
		EXPECT_NO_THROW(unit->updateUnitBonus(dust));
		const auto current = unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect));
		ASSERT_EQ(current->size(), 2u); EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
		for(const auto & effect : *current)
		{
			EXPECT_EQ(effect->duration, BonusDuration::STACK_ACTIVATION); EXPECT_EQ(effect->turnsRemain, 0);
			EXPECT_EQ(effect->spellCasterOwner, PlayerColor(0));
			if(const auto metadata = physicalAfflictions::markerMetadata(*effect))
				EXPECT_EQ(metadata->applicationOrder, initial->applicationOrder);
		}
	}
	const auto before = unit->save();
	const auto snapshot = [&] {
		std::vector<JsonNode> result;
		const auto records = unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect));
		for(const auto & effect : *records) result.push_back(effect->toJsonNode());
		return result;
	};
	const auto effects = snapshot();
	EXPECT_THROW(unit->updateUnitBonus(newHorizonsGraveDust::records(7500, PlayerColor(0))), std::invalid_argument);
	const auto other = battle()->getSidePlayer(BattleSide::DEFENDER); ASSERT_NE(other, PlayerColor(0));
	EXPECT_THROW(unit->updateUnitBonus(newHorizonsGraveDust::records(5000, other)), std::invalid_argument);
	EXPECT_EQ(unit->save(), before); EXPECT_EQ(snapshot(), effects);
	EXPECT_EQ(battle::healingReceivedReduction(*live), 0);
}

TEST_F(GraveDustSnapshotTest, MixedTimeStopRemovalPreflightsOrderedVectorBeforeOverlayMutation)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	unit->addUnitBonus(dust); unit->addUnitBonus({stopped});
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	const auto before = unit->save();
	auto * nativeRandom = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRandom = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(nativeRandom, nullptr); ASSERT_NE(fixtureRandom, nullptr);
	const auto nativeBefore = dustRandomBytes(*nativeRandom), fixtureBefore = dustRandomBytes(*fixtureRandom);
	for(const auto & partial : dust)
	{
		EXPECT_THROW(unit->removeUnitBonus(std::vector<Bonus>{stopped, partial}), std::invalid_argument);
		EXPECT_TRUE(unit->isTimeStopped()); EXPECT_EQ(unit->save(), before);
		EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	}
	SetStackEffect packet; packet.battleID = BattleID(0);
	packet.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{stopped});
	packet.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{dust.front()});
	EXPECT_THROW(parent->getServerCallback()->apply(packet), std::invalid_argument);
	EXPECT_TRUE(unit->isTimeStopped()); EXPECT_EQ(unit->save(), before);
	unit->removeUnitBonus(std::vector<Bonus>{dust.front(), stopped});
	EXPECT_FALSE(unit->isTimeStopped()); EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	EXPECT_TRUE(other->isTimeStopped()); EXPECT_EQ(battle::healingReceivedReduction(*other), 5000);
	EXPECT_FALSE(live->isTimeStopped()); EXPECT_EQ(battle::healingReceivedReduction(*live), 0);
	EXPECT_EQ(dustRandomBytes(*nativeRandom), nativeBefore);
	EXPECT_EQ(dustRandomBytes(*fixtureRandom), fixtureBefore);
}

TEST_F(GraveDustSnapshotTest, OrdinaryIncomingClockBeforeCompleteDustIsCanonicalProtectedNoOp)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	ASSERT_FALSE(parent->initialExportSubject()); // A P0 source is never silently promoted.
	auto unit = parent->getForUpdate(live->unitId());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	const auto nativeBefore = live->save(), siblingBefore = other->save();
	SetStackEffect incoming; incoming.battleID = BattleID(0);
	incoming.toAdd.emplace_back(live->unitId(), std::vector<Bonus>{stopped, dust.front(), dust.back()});
	EXPECT_NO_THROW(parent->getServerCallback()->apply(incoming));
	EXPECT_TRUE(unit->isTimeStopped());
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
	EXPECT_TRUE(unit->getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect))->empty());
	EXPECT_EQ(live->save(), nativeBefore); EXPECT_EQ(other->save(), siblingBefore);
	EXPECT_FALSE(live->isTimeStopped()); EXPECT_FALSE(other->isTimeStopped());
}

TEST_F(GraveDustSnapshotTest, ExplicitOwnedOrderedPreflightKeepsParentSiblingNativeAndRandomStreamsExact)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	battle()->addUnitBonus(live->unitId(), dust); battle()->addUnitBonus(live->unitId(), {stopped});
	GraveDustEnvironment environment(gameState());
	// This separate explicit full-information fixture is not a P0 capture or promotion.
	auto authority = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	auto exported = InitialExportBattleSubject::capture(authority, *battle());
	auto parent = std::make_shared<HypotheticBattle>(&environment, exported);
	auto unit = parent->getForUpdate(live->unitId());
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto other = sibling->getForUpdate(live->unitId());
	const auto nativeState = live->save(), parentState = unit->save(), siblingState = other->save();
	const auto effects = [](const battle::Unit & value)
	{
		std::vector<JsonNode> result;
		const auto records = value.getUnstackedBonuses(Selector::all);
		for(const auto & record : *records) result.push_back(record->toJsonNode());
		return result;
	};
	const auto nativeEffects = effects(*live), parentEffects = effects(*unit), siblingEffects = effects(*other);
	auto * nativeRandom = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRandom = dynamic_cast<CRandomGenerator *>(getRNG());
	ASSERT_NE(nativeRandom, nullptr); ASSERT_NE(fixtureRandom, nullptr);
	const auto nativeBefore = dustRandomBytes(*nativeRandom), fixtureBefore = dustRandomBytes(*fixtureRandom);
	const auto globalBefore = dustRandomBytes(CRandomGenerator::getDefault());
	SetStackEffect complete; complete.battleID = BattleID(0);
	complete.toRemove.emplace_back(live->unitId(), std::vector<Bonus>{stopped, dust.front(), dust.back()});
	EXPECT_NO_THROW(graveDustAI::validateOwnedEffectChanges(*parent, complete));
	auto partial = complete; partial.toRemove.front().second.pop_back();
	EXPECT_THROW(graveDustAI::validateOwnedEffectChanges(*parent, partial), std::invalid_argument);
	auto foreign = complete; foreign.battleID = BattleID(777);
	EXPECT_THROW(graveDustAI::validateOwnedEffectChanges(*parent, foreign), std::invalid_argument);
	const auto parentSource = parent->initialExportSubject(); ASSERT_NE(parentSource, nullptr);
	EXPECT_THROW(parentSource->effectQueries(*sibling), std::invalid_argument);
	EXPECT_EQ(live->save(), nativeState); EXPECT_EQ(unit->save(), parentState); EXPECT_EQ(other->save(), siblingState);
	EXPECT_EQ(effects(*live), nativeEffects); EXPECT_EQ(effects(*unit), parentEffects); EXPECT_EQ(effects(*other), siblingEffects);
	EXPECT_TRUE(unit->isTimeStopped()); EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	EXPECT_EQ(dustRandomBytes(*nativeRandom), nativeBefore); EXPECT_EQ(dustRandomBytes(*fixtureRandom), fixtureBefore);
	EXPECT_EQ(dustRandomBytes(CRandomGenerator::getDefault()), globalBefore);
}

}
