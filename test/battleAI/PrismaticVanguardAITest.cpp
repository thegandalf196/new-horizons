/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#if ENABLE_BATTLE_AI
#include "../server/battles/PrismaticVanguardFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/PrismaticVanguardProjection.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/ProjectedBattleWorldArena.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <vcmi/Environment.h>
namespace
{
namespace vanguard = newHorizonsPrismaticVanguard;
class VanguardEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit VanguardEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class PrismaticVanguardAITest : public PrismaticVanguardFixture
{
protected:
	std::unique_ptr<VanguardEnvironment> environment;
	std::shared_ptr<HypotheticBattle> parent;
	void SetUp() override
	{
		PrismaticVanguardFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons test profile";
	}
	void projection(std::optional<PlayerColor> observer = std::nullopt)
	{
		environment = std::make_unique<VanguardEnvironment>(gameState());
		std::shared_ptr<CBattleInfoCallback> known = observer
			? std::make_shared<CPlayerBattleCallback>(battle(), *observer)
			: std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
		parent = std::make_shared<HypotheticBattle>(environment.get(), known);
	}
	JsonNode addition(CreatureID creature, BattleHex position, bool phantom = false)
	{
		battle::UnitInfo info; info.type = creature; info.count = 3;
		info.side = BattleSide::ATTACKER; info.position = position; info.summoned = true;
		if(phantom) { info.phantomIntegrity = 4; info.phantomDuration = 2; }
		JsonNode data; info.save(data); return data;
	}
};
template<class T> auto vanguardWire(const T & value)
{
	CMemorySerializer memory; memory.oser & value; return memory.extractBuffer();
}
}
TEST_F(PrismaticVanguardAITest, FirstGenuineActivationUsesTheSharedNativeClockAndCurrentPhysicalDamageConsumer)
{
	prepare(); projection();
	for(const auto * native : {actor, upgraded})
	{
		const auto * before = parent->battleGetUnitByID(native->unitId());
		EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(before, victim, 0, false)), 0);
		const auto base = parent->calculateDmgRange(BattleAttackInfo(before, victim, 0, false)).damage;
		parent->nextTurn(native->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		const auto * current = parent->battleGetUnitByID(native->unitId());
		EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(current, victim, 0, false)), 20);
		const auto active = parent->calculateDmgRange(BattleAttackInfo(current, victim, 0, false)).damage;
		EXPECT_GT(active.min, base.min); EXPECT_GT(active.max, base.max);
		// The sole changed multiplier is the captured20%; permit only final integer rounding.
		EXPECT_GE(active.min, base.min * 120 / 100);
		EXPECT_LE(active.min, ((base.min + 1) * 120 + 99) / 100 - 1);
		EXPECT_GE(active.max, base.max * 120 / 100);
		EXPECT_LE(active.max, ((base.max + 1) * 120 + 99) / 100 - 1);
		BattleAttackInfo magic(current, victim, 0, false); magic.physicalDamage = false;
		EXPECT_EQ(vanguard::activeDamagePercent(magic), 0);
		EXPECT_EQ(markers(*native)->front()->duration, BonusDuration::ONE_BATTLE);
	}
}
TEST_F(PrismaticVanguardAITest, HeroAndRejectedContinuationsNeverCreateOrDuplicateTheFirstActivation)
{
	prepare(); projection();
	for(const auto reason : {BattleUnitTurnReason::HERO_SPELLCAST, BattleUnitTurnReason::UNIT_SPELLCAST,
		BattleUnitTurnReason::HERO_COMMAND, BattleUnitTurnReason::ACTION_REJECTED,
		BattleUnitTurnReason::PURSUIT_CONTINUATION, BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION,
		BattleUnitTurnReason::MASTER_GATE_CONTINUATION, BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION,
		BattleUnitTurnReason::CRISIS_ORDER, BattleUnitTurnReason::CRISIS_RESUME})
	{
		parent->nextTurn(actor->unitId(), reason);
		ASSERT_EQ(markers(*parent->battleGetUnitByID(actor->unitId()))->size(), 1u);
		EXPECT_EQ(markers(*parent->battleGetUnitByID(actor->unitId()))->front()->duration, BonusDuration::ONE_BATTLE);
	}
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::HERO_SPELLCAST);
	ASSERT_EQ(markers(*parent->battleGetUnitByID(actor->unitId()))->size(), 1u);
	EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(parent->battleGetUnitByID(actor->unitId()), victim, 0, false)), 20);
}
TEST_F(PrismaticVanguardAITest, WaitConsumesActiveOnlyAndDeferredWaitEntryCannotRearm)
{
	prepare(); projection(); auto current = parent->getForUpdate(actor->unitId());
	auto unrelated = *markers(*current)->front(); unrelated.stacking = "unrelated-monere"; unrelated.statusIdentity = "unrelated";
	current->addUnitBonus({unrelated});
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	parent->makeWait(parent->battleGetUnitByID(actor->unitId()));
	EXPECT_TRUE(markers(*parent->battleGetUnitByID(actor->unitId()))->empty());
	EXPECT_TRUE(current->hasBonus(CSelector([](const Bonus * bonus) { return bonus->stacking == "unrelated-monere"; })));
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(markers(*current)->empty());
	parent->nextRound(); parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(markers(*current)->empty());
}
TEST_F(PrismaticVanguardAITest, TimeStopSyntheticSlotPreservesPendingWhileFrozenUsesGenuineForfeitureClock)
{
	prepare(); projection(); auto stopped = parent->getForUpdate(actor->unitId());
	stopped->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID())});
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::AUTOMATIC_ACTION);
	ASSERT_EQ(markers(*stopped)->size(), 1u); EXPECT_EQ(markers(*stopped)->front()->duration, BonusDuration::ONE_BATTLE);
	stopped->removeUnitBonus(Selector::type()(BonusType::TIME_STOP));
	parent->nextRound();
	stopped->addUnitBonus({newHorizonsFrozen::marker(BonusSourceID(), parent->getRound())});
	ASSERT_TRUE(parent->battleBeginsActivation(stopped.get(), BattleUnitTurnReason::TURN_QUEUE));
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_EQ(markers(*stopped)->size(), 1u); EXPECT_EQ(markers(*stopped)->front()->duration, BonusDuration::STACK_ACTIVATION);
	// Existing accepted forfeiture/end consumers already own generic activation expiry.
	stopped->removeUnitBonus(Bonus::UntilActivationEnds);
	EXPECT_TRUE(markers(*stopped)->empty());
}
TEST_F(PrismaticVanguardAITest, FreshValidatedAddArmsBothFormsIncludingPhantomWithoutLocalInitOrUpdateRearm)
{
	prepare(); battle::ProjectedBattleWorldArena arena(*battle());
	const auto firstId = static_cast<uint32_t>(battle()->nextUnitId());
	std::vector<JsonNode> additions, canonicalMarkers;
	for(int i = 0; i < 2; ++i)
	{
		const auto creature = i ? upgraded->creatureId() : actor->creatureId();
		additions.push_back(addition(creature, BattleHex(12, 4 + 2 * i), i == 1));
		const auto & canonical = arena.addUnit(firstId + i, additions.back());
		ASSERT_EQ(markers(canonical)->size(), 1u);
		canonicalMarkers.push_back(markers(canonical)->toJsonNode());
	}
	projection();
	for(int i = 0; i < 2; ++i)
	{
		const auto id = firstId + i;
		parent->addUnit(id, additions[i]);
		auto fresh = parent->getForUpdate(id); ASSERT_EQ(markers(*fresh)->size(), 1u);
		EXPECT_EQ(markers(*fresh)->toJsonNode(), canonicalMarkers[i]);
		EXPECT_EQ(markers(*fresh)->front()->duration, BonusDuration::ONE_BATTLE);
		if(i) EXPECT_GT(fresh->getPhantomInitialIntegrity(), 0);
		parent->nextTurn(id, BattleUnitTurnReason::TURN_QUEUE); parent->makeWait(fresh.get());
		ASSERT_TRUE(markers(*fresh)->empty());
		const auto sameBody = fresh->save(); parent->updateUnit(id, sameBody, 0);
		EXPECT_TRUE(markers(*fresh)->empty());
	}
}
TEST_F(PrismaticVanguardAITest, SpentSameIdDeathHealingAndResurrectionNeverManufactureFreshEntry)
{
	prepare(); projection(); parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	auto current = parent->getForUpdate(actor->unitId()); parent->makeWait(current.get());
	auto dead = current->acquireState(); int64_t injury = dead->getAvailableHealth(); dead->damage(injury);
	parent->updateUnit(current->unitId(), dead->save(), -injury);
	auto revived = current->acquireState(); int64_t healing = actor->getMaxHealth();
	const auto receipt = revived->heal(healing, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_GT(receipt.healedHealthPoints, 0);
	parent->updateUnit(current->unitId(), revived->save(), receipt.healedHealthPoints);
	ASSERT_TRUE(current->alive()); EXPECT_TRUE(markers(*current)->empty());
	parent->nextTurn(current->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(markers(*current)->empty());
	auto rebound = std::make_shared<HypotheticBattle>(environment.get(), parent);
	EXPECT_TRUE(markers(*rebound->getForUpdate(current->unitId()))->empty());
}
TEST_F(PrismaticVanguardAITest, MaterializedChildrenKeepPendingWhenParentAndNativeEntryAreConsumed)
{
	prepare(); projection(); parent->getForUpdate(actor->unitId());
	auto child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto held = child->getForUpdate(actor->unitId()), other = sibling->getForUpdate(actor->unitId());
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE); parent->makeWait(parent->battleGetUnitByID(actor->unitId()));
	actor->removeBonuses(CSelector(vanguard::isEffect));
	ASSERT_EQ(markers(*held)->size(), 1u); EXPECT_EQ(markers(*held)->front()->duration, BonusDuration::ONE_BATTLE);
	child->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(held.get(), victim, 0, false)), 20);
	ASSERT_EQ(markers(*other)->size(), 1u); EXPECT_EQ(markers(*other)->front()->duration, BonusDuration::ONE_BATTLE);
}
TEST_F(PrismaticVanguardAITest, RememberedActorCacheTracksPendingActiveAndExpiredFirstActivation)
{
	prepare(); projection(); DamageCache root; root.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto base = root.getDamage(parent->battleGetUnitByID(actor->unitId()), parent->battleGetUnitByID(victim->unitId()), parent);
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
	branch->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto active = root.getDamage(branch->battleGetUnitByID(actor->unitId()), branch->battleGetUnitByID(victim->unitId()), branch);
	EXPECT_GT(active, base);
	branch->makeWait(branch->battleGetUnitByID(actor->unitId()));
	const auto spent = root.getDamage(branch->battleGetUnitByID(actor->unitId()), branch->battleGetUnitByID(victim->unitId()), branch);
	EXPECT_EQ(spent, base);
}
TEST_F(PrismaticVanguardAITest, RealCandidateCachedSelectedReplayUsesSharedTwentyPercentAndPreservesRng)
{
	prepare(); actor->setPosition(BattleHex(3, 5)); victim->setPosition(BattleHex(4, 5)); projection();
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	auto selected = std::make_shared<HypotheticBattle>(environment.get(), parent);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	const auto sourceBefore = actor->save(), victimBefore = victim->save();
	auto * nativeRng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRng = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(nativeRng, nullptr); ASSERT_NE(fixtureRng, nullptr);
	const auto nativeBefore = vanguardWire(*nativeRng), fixtureBefore = vanguardWire(*fixtureRng);
	DamageCache cache; PotentialTargets targets(parent->battleGetUnitByID(actor->unitId()), cache, parent);
	const auto chosen = std::ranges::find_if(targets.possibleAttacks, [this](const auto & ap)
		{ return ap.from == actor->getPosition() && ap.attack.defender->unitId() == victim->unitId(); });
	ASSERT_NE(chosen, targets.possibleAttacks.end()); ASSERT_FALSE(chosen->fortuneStrikes.empty());
	EXPECT_EQ(vanguard::activeDamagePercent(chosen->attack), 20);
	const auto expected = parent->battleExpectedLuckDamage(chosen->attack);
	ASSERT_FALSE(chosen->fortuneStrikes.front().hits.empty());
	EXPECT_EQ(chosen->fortuneStrikes.front().hits.front().second, expected);
	const auto cached = *chosen; BattleExchangeVariant exchange; exchange.trackAttack(cached, selected, cache);
	const auto affected = std::ranges::find_if(cached.affectedUnits, [this](const auto & unit)
		{ return unit->unitId() == victim->unitId(); });
	ASSERT_NE(affected, cached.affectedUnits.end());
	EXPECT_EQ(selected->battleGetUnitByID(victim->unitId())->getAvailableHealth(), (*affected)->getAvailableHealth());
	EXPECT_EQ(markers(*sibling->battleGetUnitByID(actor->unitId()))->front()->duration, BonusDuration::STACK_ACTIVATION);
	EXPECT_EQ(actor->save(), sourceBefore); EXPECT_EQ(victim->save(), victimBefore);
	EXPECT_EQ(vanguardWire(*nativeRng), nativeBefore); EXPECT_EQ(vanguardWire(*fixtureRng), fixtureBefore);
}
TEST_F(PrismaticVanguardAITest, NativeAcceptedDefendAndProjectedActivationEndUseExistingExpiryWithoutRearm)
{
	prepare(); beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), actor);
	ASSERT_EQ(markers(*actor)->front()->duration, BonusDuration::STACK_ACTIVATION);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(actor)));
	EXPECT_TRUE(markers(*actor)->empty()); projection();
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(markers(*parent->battleGetUnitByID(actor->unitId()))->empty());
}
TEST_F(PrismaticVanguardAITest, HiddenHeroCannotInventEntryOnFreshAddButObservedPendingStillProjects)
{
	prepare(); projection(PlayerColor(1));
	const auto id = battle()->nextUnitId(); parent->addUnit(id, addition(actor->creatureId(), BattleHex(12, 4)));
	EXPECT_TRUE(markers(*parent->battleGetUnitByID(id))->empty());
	ASSERT_EQ(markers(*actor)->size(), 1u);
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(parent->battleGetUnitByID(actor->unitId()), victim, 0, false)), 20);
}
class HistoricalVanguardAITest : public PrismaticVanguardAITest, public ::testing::WithParamInterface<int> {};
TEST_P(HistoricalVanguardAITest, MissingOrZeroCaptureNeverArmsDeployedOrFreshBodies)
{
	capture = GetParam(); prepare(); projection();
	const auto id = battle()->nextUnitId(); parent->addUnit(id, addition(actor->creatureId(), BattleHex(12, 4)));
	EXPECT_TRUE(markers(*parent->battleGetUnitByID(id))->empty());
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(markers(*parent->battleGetUnitByID(actor->unitId()))->empty());
}
INSTANTIATE_TEST_SUITE_P(HistoricalAndZero, HistoricalVanguardAITest, ::testing::Values(-1, 0));
TEST_F(PrismaticVanguardAITest, NativePreparedDeterministicSameIdRebirthRetainsOnlyItsExistingEntitlement)
{
	prepare(); projection();
	for(const bool spent : {false, true})
	{
		auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
		auto current = branch->getForUpdate(actor->unitId());
		if(spent)
		{
			branch->nextTurn(current->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			branch->makeWait(current.get());
		}
		current->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()),
			Bonus(BonusDuration::ONE_BATTLE, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID())});
		ASSERT_TRUE(current->canCast());
		BattleStackAttacked hit; hit.stackAttacked = current->unitId(); hit.damageAmount = current->getAvailableHealth();
		// Existing native preparation: integral100% revival has no fractional draw.
		CStack::prepareAttacked(hit, *branch->getServerCallback()->getRNG(), current->acquireState());
		ASSERT_TRUE(hit.killed()); ASSERT_TRUE(hit.willRebirth());
		StacksInjured packet; packet.battleID = BattleID(0); packet.stacks.push_back(hit);
		branch->getServerCallback()->apply(packet);
		ASSERT_TRUE(current->alive());
		EXPECT_EQ(current->unitId(), actor->unitId());
		EXPECT_EQ(markers(*current)->size(), spent ? 0u : 1u);
		branch->nextTurn(current->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(current.get(), victim, 0, false)), spent ? 0 : 20);
	}
}

TEST_F(PrismaticVanguardAITest, ExistingFutureSingleHitConsumerReadsActiveBoostWithoutASecondSearchOrAction)
{
	prepare(); actor->setPosition(BattleHex(3, 5)); victim->setPosition(BattleHex(4, 5)); projection();
	parent->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	auto source = parent->getForUpdate(actor->unitId()), target = parent->getForUpdate(victim->unitId());
	const auto expected = parent->battleExpectedLuckDamage(BattleAttackInfo(source.get(), target.get(), 0, false));
	auto control = std::make_shared<HypotheticBattle>(environment.get(), parent);
	control->getForUpdate(actor->unitId())->removeUnitBonus(CSelector(vanguard::isEffect));
	const auto baseline = control->battleExpectedLuckDamage(BattleAttackInfo(control->battleGetUnitByID(actor->unitId()),
		control->battleGetUnitByID(victim->unitId()), 0, false));
	ASSERT_GT(expected, baseline);
	const auto before = target->getAvailableHealth(); DamageCache cache; BattleExchangeVariant exchange;
	exchange.trackAttack(source, target, false, true, cache, parent, false, true);
	EXPECT_EQ(before - target->getAvailableHealth(), expected);
	EXPECT_EQ(vanguard::activeDamagePercent(BattleAttackInfo(source.get(), target.get(), 0, false)), 20);
	// The enclosing existing future-action completion owns this exact expiry.
	source->removeUnitBonus(Bonus::UntilActivationEnds);
	EXPECT_TRUE(markers(*source)->empty());
}

#endif
