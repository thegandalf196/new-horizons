/*
 * HypotheticCloneTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CStack.h"
#include <vcmi/Environment.h>
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"

namespace
{
class CloneEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CloneEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class HypotheticCloneTest : public HeroCommandFixture
{
protected:
	uint32_t originalId = 0;
	uint32_t cloneId = 0;
	std::shared_ptr<CloneEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;

	void prepareClone()
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
		attackerSideHero->addSpellToSpellbook(SpellID::CLONE);
		const auto * spell = SpellID(SpellID::CLONE).toSpell();
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER,
			3 * attackerSideHero->getEffectPowerDivisor(spell), ChangeValueMode::ABSOLUTE);
		const auto * original = battle()->battleGetStackByID(battle()->battleActiveUnit()->unitId());
		ASSERT_NE(original, nullptr);
		originalId = original->unitId();
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::CLONE;
		action.aimToUnit(original);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		ASSERT_TRUE(original->hasClone());
		cloneId = static_cast<uint32_t>(original->cloneID);
		ASSERT_TRUE(battle()->battleGetUnitByID(cloneId)->isClone());
		advanceRound();
		ASSERT_TRUE(battle()->battleGetUnitByID(cloneId)->alive());
		environment = std::make_shared<CloneEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}
};

TEST_F(HypotheticCloneTest, RemovingCloneReleasesOriginalForRecastWithoutChangingLiveBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepareClone());
	HypotheticBattle model(environment.get(), callback);
	const auto * spell = SpellID(SpellID::CLONE).toSpell();
	spells::BattleCast cast(&model, attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_FALSE(spell->battleMechanics(&cast)->canBeCastAt(
		battle::Target{battle::Destination(model.battleGetUnitByID(originalId))}));

	model.removeUnit(cloneId);
	EXPECT_TRUE(model.battleGetUnitByID(cloneId)->isGhost());
	EXPECT_FALSE(model.battleGetUnitByID(originalId)->hasClone());
	EXPECT_TRUE(spell->battleMechanics(&cast)->canBeCastAt(
		battle::Target{battle::Destination(model.battleGetUnitByID(originalId))}));
	EXPECT_TRUE(battle()->battleGetUnitByID(originalId)->hasClone());
	EXPECT_TRUE(battle()->battleGetUnitByID(cloneId)->alive());
	EXPECT_FALSE(battle()->battleGetUnitByID(cloneId)->isGhost());
}

TEST_F(HypotheticCloneTest, NestedCloneRemovalDoesNotClearParentOrLiveLinks)
{
	ASSERT_NO_FATAL_FAILURE(prepareClone());
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	parent->getForUpdate(originalId);
	HypotheticBattle child(environment.get(), parent);
	child.removeUnit(cloneId);
	EXPECT_FALSE(child.battleGetUnitByID(originalId)->hasClone());
	EXPECT_TRUE(parent->battleGetUnitByID(originalId)->hasClone());
	EXPECT_TRUE(parent->battleGetUnitByID(cloneId)->alive());
	EXPECT_TRUE(battle()->battleGetUnitByID(originalId)->hasClone());
	EXPECT_TRUE(battle()->battleGetUnitByID(cloneId)->alive());
}

TEST_F(HypotheticCloneTest, RemovingOriginalStillRemovesCloneAndRepeatedRemovalIsHarmless)
{
	ASSERT_NO_FATAL_FAILURE(prepareClone());
	HypotheticBattle model(environment.get(), callback);
	model.removeUnit(originalId);
	EXPECT_TRUE(model.battleGetUnitByID(originalId)->isGhost());
	EXPECT_TRUE(model.battleGetUnitByID(cloneId)->isGhost());
	EXPECT_FALSE(model.battleGetUnitByID(originalId)->hasClone());
	model.removeUnit(originalId);
	model.removeUnit(cloneId);
	EXPECT_TRUE(model.battleGetUnitByID(originalId)->isGhost());
	EXPECT_TRUE(model.battleGetUnitByID(cloneId)->isGhost());
	EXPECT_TRUE(battle()->battleGetUnitByID(originalId)->alive());
	EXPECT_TRUE(battle()->battleGetUnitByID(cloneId)->alive());
}
