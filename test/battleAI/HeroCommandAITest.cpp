/*
 * HeroCommandAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/spells/CSpell.h"

namespace
{
class CommandEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit CommandEnvironment(std::shared_ptr<CGameState> state)
		: state(std::move(state))
	{
	}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class RecordingCommandCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;

	RecordingCommandCallback()
		: CBattleCallback(PlayerColor(0), nullptr)
	{
	}

	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
}

class HeroCommandAITest : public HeroCommandFixture
{
protected:
	const CStack * active = nullptr;
	const CStack * enemy = nullptr;
	std::shared_ptr<RecordingCommandCallback> callback;
	std::shared_ptr<CommandEnvironment> environment;

	void prepareEvaluation(bool book)
	{
		prepareCommands(book);
		active = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		callback = std::make_shared<RecordingCommandCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<CommandEnvironment>(gameState());
	}

	bool choose()
	{
		BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		evaluator.selectStackAction(active);
		return evaluator.canCastSpell() && evaluator.attemptCastingSpell(active);
	}

	void executeChosen()
	{
		ASSERT_EQ(callback->submitted.size(), 1u);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			callback->submitted.front()));
	}
};

TEST_F(HeroCommandAITest, BooklessHeroChoosesBeneficialCommandAndServerAccepts)
{
	prepareEvaluation(false);
	ASSERT_FALSE(attackerSideHero->hasSpellbook());
	const auto mana = attackerSideHero->mana;
	ASSERT_TRUE(choose());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().actionType, EActionType::HERO_COMMAND);
	EXPECT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, callback->submitted.front().command));
	executeChosen();
	EXPECT_EQ(attackerSideHero->mana, mana);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_FALSE(choose());
	EXPECT_EQ(callback->submitted.size(), 1u);
}

TEST_F(HeroCommandAITest, StrongOffensiveSpellCompetesWithOrdersAndServerAccepts)
{
	prepareEvaluation(true);
	attackerSideHero->addSpellToSpellbook(SpellID::IMPLOSION);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 99, ChangeValueMode::ABSOLUTE);
	attackerSideHero->mana = 1000;
	const auto * spell = SpellID(SpellID::IMPLOSION).toSpell();
	ASSERT_EQ(attackerSideHero->getEffectPower(spell), 99);
	ASSERT_TRUE(spell->canBeCast(callback->getBattle(BattleID(0)).get(), spells::Mode::HERO, attackerSideHero));
	// Magic Arrow at the capped power did not dominate commands for 100 Angels.
	// Establish this fixture's strength instead of assuming an unclamped 1000 power:
	// substantial nonlethal direct damage, exceeding half an ordinary melee attack.
	const auto spellDamage = spell->calculateDamage(attackerSideHero);
	const auto melee = battle()->calculateDmgRange(BattleAttackInfo(active, enemy, 0, false)).damage.max;
	ASSERT_GT(spellDamage, melee / 2);
	ASSERT_LT(spellDamage, enemy->getAvailableHealth());
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	ASSERT_TRUE(choose());
	ASSERT_EQ(callback->submitted.size(), 1u);
	EXPECT_EQ(callback->submitted.front().actionType, EActionType::HERO_SPELL);
	const auto mana = attackerSideHero->mana;
	executeChosen();
	EXPECT_LT(attackerSideHero->mana, mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
}
