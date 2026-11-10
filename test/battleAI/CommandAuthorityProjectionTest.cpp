/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/CanonicalOrderValue.h"
#include "../../AI/BattleAI/BattleDamageValue.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/OrderFormation.h"
#include "../../lib/battle/HeroCommandRevision.h"
#include "../../lib/GameLibrary.h"

namespace
{
class ProjectionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ProjectionEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class CommandAuthorityProjectionTest : public HeroCommandFixture
{
protected:
	CommandAuthorityProjectionTest() { useRevisedOrders = true; }
	void veteran()
	{
		const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:command"));
		attackerSideHero->setSecSkillLevel(skill, 1, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:command", "new-horizons:command.battlePlan"});
		attackerSideHero->setSecSkillLevel(skill, 2, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:command", "new-horizons:command.veteranCommander"});
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:command", "new-horizons:command.veteranCommander"));
	}
};
}

TEST_F(CommandAuthorityProjectionTest, AcceptedOrderStatsMatchNativeAndSiblingRemainsUntouched)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(veteran());
	startBattle(); beginCombat();
	ProjectionEnvironment env(gameState());
	auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&env, live);
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	const auto id = battle()->battleActiveUnit()->unitId();
	const int before = parent->battleGetUnitByID(id)->getAttack(false);
	const int defense = parent->battleGetUnitByID(id)->getDefense(false);
	const auto allowance = child->prepareHeroOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(allowance);
	const auto effects = child->beginProjectedHeroOrder(BattleSide::ATTACKER, HeroCommand::CHARGE, {}, *allowance);
	ASSERT_TRUE(effects);
	ASSERT_TRUE(child->projectAcceptedHeroOrder(BattleSide::ATTACKER, *effects, *allowance));
	EXPECT_EQ(child->battleGetUnitByID(id)->getAttack(false), before + 2);
	EXPECT_EQ(child->battleGetUnitByID(id)->getDefense(false), defense + 2);
	EXPECT_EQ(parent->battleGetUnitByID(id)->getAttack(false), before);
	EXPECT_EQ(sibling->battleGetUnitByID(id)->getAttack(false), before);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(child->battleGetUnitByID(id)->getAttack(false), battle()->battleGetUnitByID(id)->getAttack(false));
	EXPECT_EQ(child->battleGetUnitByID(id)->getDefense(true), battle()->battleGetUnitByID(id)->getDefense(true));
	// Even a native Order accepted after capture cannot enter either sibling.
	EXPECT_EQ(parent->battleGetUnitByID(id)->getAttack(false), before);
	EXPECT_EQ(sibling->battleGetUnitByID(id)->getAttack(false), before);
	auto consumed = *child->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE);
	consumed.consumedUnitIds = {id};
	child->setHeroOrderState(BattleSide::ATTACKER, consumed);
	EXPECT_EQ(child->battleGetUnitByID(id)->getAttack(false), before);
}

TEST_F(CommandAuthorityProjectionTest, DescendantsCopyStatPolicyInsteadOfReadingChangedLiveHero)
{
	startGame(); ASSERT_NO_FATAL_FAILURE(veteran()); startBattle(); beginCombat();
	ProjectionEnvironment env(gameState());
	auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&env, live);
	const auto id = battle()->battleActiveUnit()->unitId();
	const int before = parent->battleGetUnitByID(id)->getAttack(false);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:command")), 0, ChangeValueMode::ABSOLUTE);
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	HeroOrderState charge;
	charge.command = HeroCommand::CHARGE; charge.issuedRound = child->battleGetRound();
	child->setHeroOrderState(BattleSide::ATTACKER, charge);
	EXPECT_EQ(child->unitWarfareStatBonus(child->battleGetUnitByID(id)), 2);
	EXPECT_EQ(child->battleGetUnitByID(id)->getAttack(false), before + 2);
	EXPECT_EQ(parent->battleGetUnitByID(id)->getAttack(false), before);
}

TEST_F(CommandAuthorityProjectionTest, NonActiveBorrowedRecipientsRebindWithoutMaterializingTheOtherController)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(veteran());
	startBattle();
	auto * recipient = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(80), 200);
	auto * opponent = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(82), 200);
	beginCombat();
	const auto other = battle()->battleGetStacksIf([recipient](const CStack * unit)
		{ return unit != recipient && unit->unitSide() == BattleSide::ATTACKER; });
	ASSERT_FALSE(other.empty());
	BattleSetActiveStack active;
	active.battleID = BattleID(0); active.stack = other.front()->unitId(); active.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(active);
	ASSERT_NE(battle()->battleActiveUnit()->unitId(), recipient->unitId());
	recipient->defending = true;
	recipient->defensiveStanceMeleeBonus = 3;
	recipient->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE)));
	const int attack = recipient->getAttack(false);
	const int defense = recipient->getDefense(false);
	const auto native = gameState()->saveToMemory();
	ProjectionEnvironment env(gameState());
	auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&env, live);
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	const auto id = recipient->unitId();
	for(const auto & branch : {parent, child, sibling})
	{
		ASSERT_TRUE(branch->stackStates.contains(id));
		EXPECT_FALSE(branch->stackStates.contains(opponent->unitId()));
		const auto * view = branch->battleGetUnitByID(id);
		EXPECT_NE(view, recipient);
		const auto * state = dynamic_cast<const battle::CUnitState *>(view);
		ASSERT_NE(state, nullptr);
		EXPECT_TRUE(state->usesStateEnvironment(*branch->unitEnvironment()));
		EXPECT_EQ(view->getAttack(false), attack); // Warm each exact branch cache.
		EXPECT_EQ(view->getDefense(false), defense);
		EXPECT_EQ(view->getDefenseIgnoringDefensiveStance(false), defense - 3);
	}
	const auto damage = [&](const auto & branch)
	{
		return branch->battleEstimateDamage(BattleAttackInfo(branch->battleGetUnitByID(id),
			branch->battleGetUnitByID(opponent->unitId()), 0, false)).damage.min;
	};
	const auto beforeDamage = damage(parent);
	const auto allowance = child->prepareHeroOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(allowance);
	const auto effects = child->beginProjectedHeroOrder(BattleSide::ATTACKER, HeroCommand::CHARGE, {}, *allowance);
	ASSERT_TRUE(effects);
	ASSERT_TRUE(child->projectAcceptedHeroOrder(BattleSide::ATTACKER, *effects, *allowance));
	const auto * changed = child->battleGetUnitByID(id);
	EXPECT_EQ(child->unitWarfareStatBonus(changed), 2);
	EXPECT_EQ(changed->getAttack(false), attack + 2);
	EXPECT_EQ(changed->getDefense(false), defense + 2);
	EXPECT_EQ(changed->getDefenseIgnoringDefensiveStance(false), defense - 1);
	EXPECT_GT(damage(child), beforeDamage);
	EXPECT_EQ(damage(parent), beforeDamage);
	EXPECT_EQ(damage(sibling), beforeDamage);
	auto consumed = *child->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE);
	consumed.consumedUnitIds = {id};
	child->setHeroOrderState(BattleSide::ATTACKER, consumed);
	EXPECT_EQ(changed->getAttack(false), attack);
	EXPECT_EQ(changed->getDefenseIgnoringDefensiveStance(false), defense - 3);
	EXPECT_EQ(damage(child), beforeDamage);
	EXPECT_EQ(parent->battleGetUnitByID(id)->getAttack(false), attack);
	EXPECT_EQ(sibling->battleGetUnitByID(id)->getAttack(false), attack);
	EXPECT_EQ(gameState()->saveToMemory(), native);
}

TEST_F(CommandAuthorityProjectionTest, CanonicalSecondWindUsesSharedFormationIssuancePercent)
{
	startGame();
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 60, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 40, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * spent = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(80), 10);
	ASSERT_NE(spent, nullptr);
	beginCombat();
	spent->movedThisRound = true;
	ProjectionEnvironment env(gameState());
	auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto projected = std::make_shared<HypotheticBattle>(&env, live);
	const std::vector<uint32_t> targets{spent->unitId()};
	const auto order = projected->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, targets);
	ASSERT_TRUE(order);
	const int percent = projected->battleSecondWindPercent(BattleSide::ATTACKER, *order, spent->unitId());
	EXPECT_EQ(percent, orderFormation::secondWindPercent(battle()->getHeroCommandRules(), 60, 40,
		attackerSideHero->getArmyCommand()->effective(), heroCommands::leadershipEfficiencyPercent(*attackerSideHero,
			order->divineMandateEfficiencyBonusPercent())));
	heroOrderValue::CanonicalOrderValue value(*projected, BattleSide::ATTACKER, HeroCommand::SECOND_WIND, targets);
	float best = 0;
	for(const auto * enemy : value.enemyUnits)
		best = std::max(best, battleDamageValue::averageOrderDamage(projected->battleEstimateDamage(
			BattleAttackInfo(projected->battleGetUnitByID(spent->unitId()), enemy, 0, false))));
	ASSERT_GT(best, 0);
	EXPECT_FLOAT_EQ(*value.directValue(std::nullopt).direct, best * percent / 100.0f);
}

TEST_F(CommandAuthorityProjectionTest, CanonicalVanguardUsesSamePreparedRevisionAsIssuance)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	ProjectionEnvironment env(gameState());
	auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto projected = std::make_shared<HypotheticBattle>(&env, live);
	const auto order = projected->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::VANGUARD, {});
	ASSERT_TRUE(order); ASSERT_TRUE(order->revision);
	const std::vector<uint32_t> targets;
	heroOrderValue::CanonicalOrderValue value(*projected, BattleSide::ATTACKER, HeroCommand::VANGUARD, targets);
	const auto projectedValue = value.directValue(std::nullopt);
	ASSERT_TRUE(projectedValue.direct);
	ASSERT_TRUE(issue(HeroCommand::VANGUARD));
	const auto accepted = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::VANGUARD);
	ASSERT_TRUE(accepted);
	ASSERT_TRUE(accepted->revision);
	EXPECT_EQ(order->revision->vanguardStatBonus, accepted->revision->vanguardStatBonus);
	EXPECT_EQ(order->revision->expiresAfterRound, accepted->revision->expiresAfterRound);
	EXPECT_FLOAT_EQ(*projectedValue.direct, static_cast<float>(
		accepted->revision->recipientUnitIds.size() * accepted->revision->vanguardStatBonus));
}

TEST_F(CommandAuthorityProjectionTest, NativeSlotEnhancementAndAuthorityStatsAreNotRepeatedByDescendants)
{
	startGame();
	const SecondarySkill authority(SecondarySkill::decode("new-horizons:authority"));
	attackerSideHero->setSecSkillLevel(authority, 1, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:authority", "new-horizons:authority.offensiveDrills"});
	attackerSideHero->level = 16;
	attackerSideHero->captureCommandProgression();
	ASSERT_TRUE(attackerSideHero->getCommandState().nextPending());
	attackerSideHero->selectCommandEnhancement(6, newHorizonsHeroes::CommandEnhancement::WEAPON_DRILL);
	attackerSideHero->selectCommandEnhancement(7, newHorizonsHeroes::CommandEnhancement::READINESS);
	startBattle(); beginCombat();
	const auto * native = battle()->battleActiveUnit();
	ASSERT_NE(native, nullptr);
	ASSERT_EQ(native->unitSlot(), SlotID(0));
	ProjectionEnvironment env(gameState());
	auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&env, live);
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	for(const auto & view : {parent, child, sibling})
	{
		const auto * unit = view->getForUpdate(native->unitId()).get();
		EXPECT_EQ(unit->getAttack(false), native->getAttack(false));
		EXPECT_EQ(unit->getAttack(true), native->getAttack(true));
		EXPECT_EQ(unit->getDefense(false), native->getDefense(false));
		EXPECT_EQ(unit->getMaxHealth(), native->getMaxHealth());
		EXPECT_EQ(unit->getAvailableHealth(), native->getAvailableHealth());
	}
}
