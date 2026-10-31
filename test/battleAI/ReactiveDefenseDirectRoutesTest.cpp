/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/NewHorizonsReactiveDefenseFixture.h"
#include "../../AI/BattleAI/PhysicalStrikeProjection.h"
#include "../../AI/BattleAI/SelectedAttackReceipt.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/VisibleHeroPolicy.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/GameLibrary.h"
#include <tuple>
namespace
{
class DirectRouteEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DirectRouteEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class ReactiveDefenseDirectRoutesTest : public NewHorizonsReactiveDefenseFixture
{
protected:
	std::unique_ptr<DirectRouteEnvironment> environment;
	std::shared_ptr<HypotheticBattle> model;
	std::shared_ptr<HypotheticBattle> sibling;
	using RandomState = std::tuple<std::vector<std::byte>, std::vector<std::byte>, std::vector<std::byte>>;
	std::optional<RandomState> rngBefore;
	std::vector<std::pair<uint32_t, JsonNode>> liveBefore;
	RandomState randomState()
	{
		auto * authority = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
		auto * fixture = dynamic_cast<CRandomGenerator *>(getRNG());
		if(!authority || !fixture) throw std::runtime_error("Expected concrete fixture random generators");
		CMemorySerializer world, battleRandom, defaults;
		world.oser & *gameHandler->randomizer; battleRandom.oser & *fixture;
		defaults.oser & CRandomGenerator::getDefault();
		return {world.extractBuffer(), battleRandom.extractBuffer(), defaults.extractBuffer()};
	}
	void setup(int captureMode = 0, int perspective = -1)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(true, captureMode));
		environment = std::make_unique<DirectRouteEnvironment>(gameState());
		std::shared_ptr<CBattleInfoCallback> known = perspective < 0
			? std::shared_ptr<CBattleInfoCallback>(gameState(), battle())
			: std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(perspective));
		model = std::make_shared<HypotheticBattle>(environment.get(), known);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), known);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			liveBefore.emplace_back(unit->unitId(), unit->acquireState()->save());
		rngBefore = randomState();
	}
	void TearDown() override
	{
		if(rngBefore)
		{
			EXPECT_EQ(randomState(), *rngBefore);
			for(const auto & [id, state] : liveBefore)
			{
				EXPECT_EQ(battle()->battleGetUnitByID(id)->acquireState()->save(), state);
				EXPECT_EQ(sibling->battleGetUnitByID(id)->acquireState()->save(), state);
			}
		}
		NewHorizonsReactiveDefenseFixture::TearDown();
	}
	bool guarded() const
	{
		return model->battleGetUnitByID(subject->unitId())->hasBonus(
			CSelector(newHorizonsReactiveDefense::isSolidarityProtection));
	}
	BattleStackAttacked hit(uint32_t id, int64_t damage)
	{
		auto target = model->getForUpdate(id);
		BattleStackAttacked result; result.stackAttacked = id; result.attackerID = enemy->unitId();
		result.damageAmount = damage;
		CStack::prepareAttacked(result, *model->getServerCallback()->getRNG(), target->acquireState());
		return result;
	}
	void injuryPacket(int captureMode)
	{
		ASSERT_NO_FATAL_FAILURE(setup(captureMode));
		const auto demonHealth = subject->getAvailableHealth();
		StacksInjured packet; packet.battleID = BattleID(0);
		packet.stacks.push_back(hit(ally->unitId(), ally->getAvailableHealth()));
		packet.stacks.push_back(hit(subject->unitId(), 9));
		ASSERT_FALSE(guarded());
		model->getServerCallback()->apply(packet);
		EXPECT_FALSE(model->battleGetUnitByID(ally->unitId())->alive());
		EXPECT_EQ(model->battleGetUnitByID(subject->unitId())->getAvailableHealth(), demonHealth - 9);
		EXPECT_EQ(guarded(), captureMode == 0);
	}
	void controlledCasualty(bool puppet)
	{
		ASSERT_NO_FATAL_FAILURE(setup());
		const auto control = puppet
			? newHorizonsPuppetMaster::controlMarker(SpellID(SpellID::decode("new-horizons:puppetMaster")), PlayerColor(1))
			: Bonus(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
		for(const auto * native : {subject, ally}) model->getForUpdate(native->unitId())->addUnitBonus({control});
		const auto * demon = model->battleGetUnitByID(subject->unitId());
		EXPECT_EQ(model->battleGetOwner(demon), PlayerColor(puppet ? 0 : 1));
		EXPECT_EQ(model->battleGetActionController(demon), PlayerColor(1));
		StacksInjured packet; packet.battleID = BattleID(0);
		packet.stacks.push_back(hit(ally->unitId(), ally->getAvailableHealth()));
		model->getServerCallback()->apply(packet); EXPECT_TRUE(guarded());
	}

};
TEST_F(ReactiveDefenseDirectRoutesTest, AcceptedInjuryPublishesOnlyAfterWholePacket) { injuryPacket(0); }
TEST_F(ReactiveDefenseDirectRoutesTest, HistoricalAbsencePreservesInjuryOutcomeWithoutGuard) { injuryPacket(1); }
TEST_F(ReactiveDefenseDirectRoutesTest, ExplicitZeroPreservesInjuryOutcomeWithoutGuard) { injuryPacket(2); }
TEST_F(ReactiveDefenseDirectRoutesTest, NegativeHealthUpdateUsesAcceptedTypedPacket)
{
	ASSERT_NO_FATAL_FAILURE(setup());
	const auto prepared = hit(ally->unitId(), ally->getAvailableHealth());
	BattleUnitsChanged packet; packet.battleID = BattleID(0); packet.changedStacks.push_back(prepared.newState);
	ASSERT_LT(packet.changedStacks.front().healthDelta, 0);
	model->getServerCallback()->apply(packet);
	EXPECT_FALSE(model->battleGetUnitByID(ally->unitId())->alive()); EXPECT_TRUE(guarded());
}
TEST_F(ReactiveDefenseDirectRoutesTest, GenericRemovalPaymentIsNotDamageReceipt)
{
	ASSERT_NO_FATAL_FAILURE(setup());
	BattleUnitsChanged packet; packet.battleID = BattleID(0);
	packet.changedStacks.emplace_back(ally->unitId(), UnitChanges::EOperation::REMOVE);
	model->getServerCallback()->apply(packet);
	EXPECT_FALSE(guarded());
}
TEST_F(ReactiveDefenseDirectRoutesTest, UnresolvedReadyNativeRebirthDoesNotInventDestruction)
{
	ASSERT_NO_FATAL_FAILURE(setup());
	auto target = model->getForUpdate(ally->unitId());
	target->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::REBIRTH, BonusSource::OTHER, 20, BonusSourceID()),
		Bonus(BonusDuration::ONE_BATTLE, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_TRUE(model->hasReadyNativeRebirth(target.get()));
	EXPECT_FALSE(PhysicalStrikeProjection::captureForecastDamage(*model, *target));
	const auto prepared = hit(target->unitId(), target->getAvailableHealth());
	ASSERT_TRUE(prepared.killed()); ASSERT_FALSE(prepared.willRebirth());
	StacksInjured packet; packet.battleID = BattleID(0); packet.stacks.push_back(prepared);
	model->getServerCallback()->apply(packet);
	EXPECT_TRUE(guarded());
}
TEST_F(ReactiveDefenseDirectRoutesTest, SuccessfulNativeSameUnitRebirthDoesNotPublish)
{
	ASSERT_NO_FATAL_FAILURE(setup());
	auto target = model->getForUpdate(ally->unitId());
	target->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()),
		Bonus(BonusDuration::ONE_BATTLE, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID())});
	const auto prepared = hit(target->unitId(), target->getAvailableHealth());
	ASSERT_TRUE(prepared.killed()); ASSERT_TRUE(prepared.willRebirth());
	StacksInjured packet; packet.battleID = BattleID(0); packet.stacks.push_back(prepared);
	model->getServerCallback()->apply(packet);
	EXPECT_TRUE(model->battleGetUnitByID(ally->unitId())->alive()); EXPECT_FALSE(guarded());
}
TEST_F(ReactiveDefenseDirectRoutesTest, PhysicalPoisonRealActivationDamagePublishes)
{
	ASSERT_NO_FATAL_FAILURE(setup());
	auto target = model->getForUpdate(ally->unitId());
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(target.get(), target->getAvailableHealth(), enemy->unitId()));
	model->nextTurn(target->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(target->alive()); EXPECT_TRUE(guarded());
}
TEST_F(ReactiveDefenseDirectRoutesTest, ExistingPreemptiveDamageConsumerPublishesWithoutAlkinSpend)
{
	ASSERT_NO_FATAL_FAILURE(setup());
	auto target = model->getForUpdate(ally->unitId());
	BattleAttackInfo reaction(model->battleGetUnitByID(enemy->unitId()), target.get(), 0, false);
	reaction.retaliation = true; reaction.preemptiveDamagePercent = 50;
	const auto expected = std::min(target->getAvailableHealth(), model->battleExpectedLuckDamage(reaction));
	ASSERT_EQ(expected, target->getAvailableHealth());
	EXPECT_EQ(PhysicalStrikeProjection::applyExpectedReactionDamage(*model, reaction, *target), expected);
	EXPECT_FALSE(target->alive()); EXPECT_TRUE(guarded());
	EXPECT_FALSE(target->hasBonus(CSelector(newHorizonsReactiveDefense::isIronHideSpent)));
}
TEST_F(ReactiveDefenseDirectRoutesTest, SelectedPremovementResidualDamageUsesExplicitReceipt)
{
	ASSERT_NO_FATAL_FAILURE(setup());
	auto target = model->getForUpdate(ally->unitId());
	selectedAttackReceipt::applyCapturedDamage(*model, target, target->getAvailableHealth());
	EXPECT_FALSE(target->alive()); EXPECT_TRUE(guarded());
}
TEST_F(ReactiveDefenseDirectRoutesTest, AcceptedCloneDamageDestructionPublishes)
{
	ASSERT_NO_FATAL_FAILURE(setup());
	auto target = model->getForUpdate(ally->unitId()); target->cloned = true;
	const auto prepared = hit(target->unitId(), target->getAvailableHealth());
	ASSERT_TRUE(prepared.cloneKilled());
	StacksInjured packet; packet.battleID = BattleID(0); packet.stacks.push_back(prepared);
	model->getServerCallback()->apply(packet);
	EXPECT_FALSE(target->alive()); EXPECT_TRUE(guarded());
}
TEST_F(ReactiveDefenseDirectRoutesTest, AcceptedSummonDamageDestructionPublishes)
{
	ASSERT_NO_FATAL_FAILURE(setup());
	battle::UnitInfo info; info.type = ally->creatureId(); info.count = 1;
	info.side = BattleSide::ATTACKER; info.position = BattleHex(6, 2); info.summoned = true;
	const auto id = static_cast<uint32_t>(battle()->nextUnitId());
	BattleUnitsChanged addition; addition.battleID = BattleID(0);
	auto & added = addition.changedStacks.emplace_back(id, UnitChanges::EOperation::ADD);
	info.save(added.data);
	model->getServerCallback()->apply(addition);
	auto target = model->getForUpdate(id);
	ASSERT_TRUE(target->isSummoned());
	StacksInjured packet; packet.battleID = BattleID(0);
	packet.stacks.push_back(hit(target->unitId(), target->getAvailableHealth()));
	model->getServerCallback()->apply(packet);
	EXPECT_FALSE(target->alive()); EXPECT_TRUE(guarded());
}
TEST_F(ReactiveDefenseDirectRoutesTest, HypnosisKeepsOriginalPassiveArmy)
{
	controlledCasualty(false);
}
TEST_F(ReactiveDefenseDirectRoutesTest, PuppetActionControllerDoesNotTransferPassiveArmy)
{
	controlledCasualty(true);
}
TEST_F(ReactiveDefenseDirectRoutesTest, HiddenOriginalSidePolicyCannotBeReadByPacketForecast)
{
	ASSERT_NO_FATAL_FAILURE(setup(0, 1));
	ASSERT_EQ(model->battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	StacksInjured packet; packet.battleID = BattleID(0);
	packet.stacks.push_back(hit(ally->unitId(), ally->getAvailableHealth()));
	model->getServerCallback()->apply(packet); EXPECT_FALSE(guarded());
}
TEST_F(ReactiveDefenseDirectRoutesTest, KnownPlayerSidePolicyMatchesAuthorityPacketForecast)
{
	ASSERT_NO_FATAL_FAILURE(setup(0, 0));
	ASSERT_EQ(model->battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::VISIBLE_HERO);
	StacksInjured packet; packet.battleID = BattleID(0);
	packet.stacks.push_back(hit(ally->unitId(), ally->getAvailableHealth()));
	model->getServerCallback()->apply(packet); EXPECT_TRUE(guarded());
}

TEST_F(ReactiveDefenseDirectRoutesTest, AlreadyDeadUnitDoesNotBecomeAnotherDestructionReceipt)
{
	ASSERT_NO_FATAL_FAILURE(setup());
	StacksInjured packet; packet.battleID = BattleID(0);
	packet.stacks.push_back(hit(ally->unitId(), ally->getAvailableHealth()));
	model->getServerCallback()->apply(packet); ASSERT_TRUE(guarded());
	EXPECT_TRUE(PhysicalStrikeProjection::captureDamagePacket(*model,
		std::span<const BattleStackAttacked>(packet.stacks)).empty());
}

}
