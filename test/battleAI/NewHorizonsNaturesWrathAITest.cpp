/*
 * NewHorizonsNaturesWrathAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/NewHorizonsNaturesWrathFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
class WrathEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit WrathEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class WrathCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	WrathCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
};
}

class NewHorizonsNaturesWrathAITest : public NewHorizonsNaturesWrathFixture
{
};

TEST_F(NewHorizonsNaturesWrathAITest, EnumerationIncludesFriendlyHealthyAndImmuneConductorsButNotDead)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	first->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SPELL_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(spell())));
	damage(reserve, reserve->getAvailableHealth());
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell().toSpell());
	const auto mechanics = spell().toSpell()->battleMechanics(&event);
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	std::set<uint32_t> ids;
	for(const auto & target : targets)
	{
		ASSERT_EQ(target.size(), 1u);
		ASSERT_NE(target.front().unitValue, nullptr);
		ids.insert(target.front().unitValue->unitId());
	}
	EXPECT_EQ(ids, (std::set<uint32_t>{first->unitId(), friendly->unitId(), third->unitId()}));
}

TEST_F(NewHorizonsNaturesWrathAITest, DetachedCastIsIsolatedAndMatchesPaidMixedChain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	damage(friendly, 9);
	auto callback = std::make_shared<WrathCallback>();
	callback->onBattleStarted(battle());
	WrathEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback->getBattle(BattleID(0)));
	const std::array<const CStack *, 4> units{first, friendly, third, reserve};
	std::array<int64_t, 4> original;
	for(size_t index = 0; index < units.size(); ++index)
		original[index] = units[index]->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	CMemorySerializer randomBefore;
	randomBefore.oser & *gameHandler->randomizer;
	spells::BattleCast event(&projected, attackerSideHero, spells::Mode::HERO, spell().toSpell());
	const auto mechanics = spell().toSpell()->battleMechanics(&event);
	const spells::Target target{spells::Destination(projected.battleGetUnitByID(first->unitId()))};
	ASSERT_TRUE(mechanics->canBeCastAt(target));
	mechanics->castEval(projected.getServerCallback(), target);
	for(size_t index = 0; index < units.size(); ++index)
		EXPECT_EQ(units[index]->getAvailableHealth(), original[index]);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	CMemorySerializer randomAfter;
	randomAfter.oser & *gameHandler->randomizer;
	EXPECT_EQ(randomBefore.extractBuffer(), randomAfter.extractBuffer());
	ASSERT_TRUE(cast(first));
	for(const auto * unit : units)
		EXPECT_EQ(unit->getAvailableHealth(), projected.battleGetUnitByID(unit->unitId())->getAvailableHealth());
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsNaturesWrathAITest, ActualAISelectsPaidWrathWithOrdersStillCompeting)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, 1));
	damage(friendly, 9);
	Bonus immobilized;
	immobilized.type = BonusType::STACKS_SPEED;
	immobilized.duration = BonusDuration::ONE_BATTLE;
	immobilized.val = -friendly->getMovementRange();
	friendly->addNewBonus(std::make_shared<Bonus>(immobilized));
	BattleSetActiveStack activate;
	activate.battleID = BattleID(0);
	activate.stack = friendly->unitId();
	activate.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activate);
	auto callback = std::make_shared<WrathCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<WrathEnvironment>(gameState());
	BattleEvaluator evaluator(environment, callback, friendly, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(friendly);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto before = first->getAvailableHealth() + third->getAvailableHealth();
	ASSERT_TRUE(evaluator.attemptCastingSpell(friendly));
	ASSERT_EQ(callback->submitted.size(), 1u);
	const auto action = callback->submitted.front();
	ASSERT_EQ(action.spell, spell());
	const auto target = action.getTarget(battle());
	ASSERT_EQ(target.size(), 1u);
	ASSERT_NE(target.front().unitValue, nullptr);
	EXPECT_TRUE(newHorizonsNaturesWrath::validConductor(target.front().unitValue));
	EXPECT_EQ(first->getAvailableHealth() + third->getAvailableHealth(), before);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_LT(first->getAvailableHealth() + third->getAvailableHealth(), before);
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
}
