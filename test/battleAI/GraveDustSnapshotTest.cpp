/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <stdexcept>
#include <limits>
#include "../../lib/CRandomGenerator.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include <vcmi/ServerCallback.h>
#include "../server/battles/BattleTestFixture.h"
#include "../../server/CGameHandler.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsGraveDust.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/ScopedStackEffectReplay.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/battle/HealingReceipt.h"
#include "../../lib/battle/BattleForm.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/InitialExportBattleSubject.h"
#include "../../AI/BattleAI/InitialExportEffectQueries.h"
#include "../../AI/BattleAI/OwnedStackEffectPreflight.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/AttackPreviewHitContext.h"
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

TEST_F(GraveDustSnapshotTest, OrdinaryProjectionPreflightsPairBeforeAnyOverlay)
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
	EXPECT_EQ(battle::healingReceivedReduction(*live), 5000);
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
	const Bonus luck(BonusDuration::ONE_BATTLE, BonusType::LUCK, BonusSource::OTHER, 1, BonusSourceID());
	for(const bool reversed : {false, true})
	{
		auto branch = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
		auto unit = branch->getForUpdate(live->unitId());
		unit->addUnitBonus(dust); unit->addUnitBonus({guardian, luck});
		ASSERT_EQ(unit->guardianSpiritHitPoints, 5);
		const auto before = unit->save();
		EXPECT_THROW(unit->removeUnitBonus(std::vector<Bonus>{luck, guardian, dust.front()}), std::invalid_argument);
		SetStackEffect partial; partial.battleID = BattleID(0);
		partial.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{luck});
		partial.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{dust.front()});
		EXPECT_THROW(branch->getServerCallback()->apply(partial), std::invalid_argument);
		EXPECT_EQ(unit->valOfBonuses(BonusType::LUCK), live->valOfBonuses(BonusType::LUCK) + 1);
		EXPECT_EQ(unit->save(), before); EXPECT_EQ(unit->guardianSpiritHitPoints, 5);
		EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
		std::vector<Bonus> removal{dust.at(reversed ? 1 : 0), luck, dust.at(reversed ? 0 : 1), guardian};
		EXPECT_NO_THROW(unit->removeUnitBonus(removal));
		EXPECT_EQ(unit->valOfBonuses(BonusType::LUCK), live->valOfBonuses(BonusType::LUCK));
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

TEST_F(GraveDustSnapshotTest, ExplicitOwnedOrderedPreflightKeepsParentSiblingNativeAndRandomStreamsExact)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	battle()->addUnitBonus(live->unitId(), dust);
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
	complete.toRemove.emplace_back(live->unitId(), std::vector<Bonus>{dust.front(), dust.back()});
	EXPECT_NO_THROW(graveDustAI::validateOwnedEffectChanges(*parent, complete));
	auto partial = complete; partial.toRemove.front().second.pop_back();
	EXPECT_THROW(graveDustAI::validateOwnedEffectChanges(*parent, partial), std::invalid_argument);
	auto foreign = complete; foreign.battleID = BattleID(777);
	EXPECT_THROW(graveDustAI::validateOwnedEffectChanges(*parent, foreign), std::invalid_argument);
	const auto parentSource = parent->initialExportSubject(); ASSERT_NE(parentSource, nullptr);
	EXPECT_THROW(parentSource->effectQueries(*sibling), std::invalid_argument);
	EXPECT_EQ(live->save(), nativeState); EXPECT_EQ(unit->save(), parentState); EXPECT_EQ(other->save(), siblingState);
	EXPECT_EQ(effects(*live), nativeEffects); EXPECT_EQ(effects(*unit), parentEffects); EXPECT_EQ(effects(*other), siblingEffects);
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 5000);
	EXPECT_EQ(dustRandomBytes(*nativeRandom), nativeBefore); EXPECT_EQ(dustRandomBytes(*fixtureRandom), fixtureBefore);
	EXPECT_EQ(dustRandomBytes(CRandomGenerator::getDefault()), globalBefore);
}

}

TEST_F(GraveDustSnapshotTest, OrdinaryPacketFormRemovalRetainsGlobalInvalidationAndNativeIsolation)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat(); GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto unit = parent->getForUpdate(live->unitId());
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	unit->addUnitBonus(dust);
	unit->beginBattleForm(creatureByName("core:pikeman"), 2);
	const auto marker = battle::polymorphMarker(SpellID(SpellID::decode("new-horizons:polymorph")), PlayerColor(0));
	parent->addUnitBonus(unit->unitId(), {marker});
	ASSERT_TRUE(unit->hasBattleForm());
	const auto nativeBefore = live->save();
	const auto version = parent->getTreeVersion();
	SetStackEffect changes; changes.battleID = parent->getBattleID();
	changes.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{marker, dust.front(), dust.back()});
	ASSERT_NO_THROW(parent->getServerCallback()->apply(changes));
	EXPECT_FALSE(unit->hasBattleForm());
	EXPECT_FALSE(unit->hasBonus(CSelector(battle::isPolymorphMarker)));
	EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
	EXPECT_EQ(parent->getTreeVersion(), version + 1); // Exactly the real IBattleState row invalidation.
	EXPECT_EQ(live->save(), nativeBefore);
}

TEST_F(GraveDustSnapshotTest, OrdinaryIncomingBonusesPublishCompleteDustAndIsolateSibling)
{
	startGame(); startBattle();
	auto * live = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2);
	ASSERT_NE(live, nullptr); beginCombat();
	GraveDustEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment,
		std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto luckBefore = live->valOfBonuses(BonusType::LUCK);
	const auto sourceState = live->save();
	const auto random = dustRandomBytes(*dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator()));
	const auto dust = newHorizonsGraveDust::records(5000, PlayerColor(0));
	const Bonus luck(BonusDuration::ONE_BATTLE, BonusType::LUCK, BonusSource::OTHER, 1, BonusSourceID());
	SetStackEffect incoming; incoming.battleID = BattleID(0);
	incoming.toAdd.emplace_back(live->unitId(), std::vector<Bonus>{luck, dust.front(), dust.back()});
	ASSERT_NO_THROW(child->getServerCallback()->apply(incoming));
	const auto changed = child->getForUpdate(live->unitId());
	EXPECT_EQ(changed->valOfBonuses(BonusType::LUCK), luckBefore + 1);
	EXPECT_EQ(battle::healingReceivedReduction(*changed), 5000);
	for(const auto & untouched : {parent, sibling})
	{
		const auto unit = untouched->getForUpdate(live->unitId());
		EXPECT_EQ(unit->valOfBonuses(BonusType::LUCK), luckBefore);
		EXPECT_EQ(battle::healingReceivedReduction(*unit), 0);
	}
	EXPECT_EQ(live->save(), sourceState);
	EXPECT_EQ(dustRandomBytes(*dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator())), random);
}

TEST_F(GraveDustSnapshotTest, HeroQueueReplayRestoresNoDustOntoKilledBody)
{
	// A Hero-queue replay copies strike riders from the preview board, where a
	// dusted target can still be alive, onto a destination where it was killed.
	// The authority removes a killed body's Grave Dust and installs none on it.
	startGame();
	startBattle();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:lich"), BattleHex(leftHex), 5);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 4), 3);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(target, nullptr);
	beginCombat();
	battle()->addUnitBonus(target->unitId(), newHorizonsGraveDust::records(5000, PlayerColor(0)));
	const CSelector dust(newHorizonsGraveDust::isEffect);
	auto environment = std::make_shared<GraveDustEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto preview = std::make_shared<HypotheticBattle>(environment.get(), callback);
	ASSERT_TRUE(preview->battleGetUnitByID(target->unitId())->hasBonus(dust));
	AttackPossibility action(shooter->getPosition(), target->getPosition(), BattleAttackInfo(shooter, target, 0, true));
	action.effectPreview = preview;

	auto killedBoard = std::make_shared<HypotheticBattle>(environment.get(), callback);
	auto killed = killedBoard->getForUpdate(target->unitId());
	int64_t lethal = killed->getAvailableHealth();
	killed->damage(lethal);
	ASSERT_FALSE(killed->alive());
	action.affectedUnits = {killed->acquireState()};
	EXPECT_NO_THROW(AttackPreviewHitContext::replayOrdinaryStrikeEffects(*killedBoard, action));
	EXPECT_FALSE(killedBoard->battleGetUnitByID(target->unitId())->hasBonus(dust));

	auto survivingBoard = std::make_shared<HypotheticBattle>(environment.get(), callback);
	auto survivor = survivingBoard->getForUpdate(target->unitId());
	action.affectedUnits = {survivor->acquireState()};
	EXPECT_NO_THROW(AttackPreviewHitContext::replayOrdinaryStrikeEffects(*survivingBoard, action));
	EXPECT_EQ(battle::healingReceivedReduction(*survivingBoard->battleGetUnitByID(target->unitId())), 5000);
}
