/*
 * NewHorizonsPandemoniumAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/NewHorizonsPandemoniumFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
class PandemoniumEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit PandemoniumEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class PandemoniumCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	PandemoniumCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { submitted.push_back(action); }
};
}

class NewHorizonsPandemoniumAITest : public NewHorizonsPandemoniumFixture
{
};

TEST_F(NewHorizonsPandemoniumAITest, GlobalEnumerationIsOneEmptyTarget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell().toSpell());
	const auto mechanics = spell().toSpell()->battleMechanics(&event);
	const auto targets = SpellTargetEvaluator::getViableTargets(mechanics.get());
	ASSERT_EQ(targets.size(), 1u);
	EXPECT_TRUE(targets.front().empty());
}

TEST_F(NewHorizonsPandemoniumAITest, DetachedGlobalCastKeepsLiveHealthManaAndRngUntouched)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enemy->addNewBonus(status("slow"));
	friendly->addNewBonus(status("curse", BonusType::MORALE, SpellID::CURSE));
	auto callback = std::make_shared<PandemoniumCallback>();
	callback->onBattleStarted(battle());
	PandemoniumEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback->getBattle(BattleID(0)));
	const std::array<const CStack *, 3> units{enemy, friendly, clean};
	std::array<int64_t, 3> before;
	for(size_t index = 0; index < units.size(); ++index)
		before[index] = units[index]->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	CMemorySerializer rngBefore;
	rngBefore.oser & *gameHandler->randomizer;
	spells::BattleCast event(&projected, attackerSideHero, spells::Mode::HERO, spell().toSpell());
	const auto mechanics = spell().toSpell()->battleMechanics(&event);
	ASSERT_TRUE(mechanics->canBeCastAt({}));
	mechanics->castEval(projected.getServerCallback(), {});
	for(size_t index = 0; index < units.size(); ++index)
		EXPECT_EQ(units[index]->getAvailableHealth(), before[index]);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	CMemorySerializer rngAfter;
	rngAfter.oser & *gameHandler->randomizer;
	EXPECT_EQ(rngBefore.extractBuffer(), rngAfter.extractBuffer());
	ASSERT_TRUE(cast());
	for(const auto * unit : units)
		EXPECT_EQ(unit->getAvailableHealth(), projected.battleGetUnitByID(unit->unitId())->getAvailableHealth());
}

TEST_F(NewHorizonsPandemoniumAITest, ActualAISelectsPaidAsymmetricDetonationWithOrdersCompeting)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, 1));
	for(const auto * identity : {"slow", "curse", "disease", "poison", "misfortune", "weakness"})
		enemy->addNewBonus(status(identity));
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
	auto callback = std::make_shared<PandemoniumCallback>();
	callback->onBattleStarted(battle());
	auto environment = std::make_shared<PandemoniumEnvironment>(gameState());
	BattleEvaluator evaluator(environment, callback, friendly, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	evaluator.selectStackAction(friendly);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto enemyHP = enemy->getAvailableHealth();
	const auto friendlyHP = friendly->getAvailableHealth();
	ASSERT_TRUE(evaluator.attemptCastingSpell(friendly));
	ASSERT_EQ(callback->submitted.size(), 1u);
	ASSERT_EQ(callback->submitted.front().spell, spell());
	EXPECT_EQ(enemy->getAvailableHealth(), enemyHP);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), callback->submitted.front()));
	EXPECT_LT(enemy->getAvailableHealth(), enemyHP);
	EXPECT_EQ(friendly->getAvailableHealth(), friendlyHP);
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
}
