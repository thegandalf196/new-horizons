/*
 * HypotheticWallTest.cpp, part of VCMI engine
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
#include "../../lib/CPlayerState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"

namespace
{
class WallEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit WallEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class HypotheticWallTest : public HeroCommandFixture
{
protected:
	std::shared_ptr<WallEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> callback;

	void prepareSiege()
	{
		ASSERT_NO_FATAL_FAILURE(startGame(true));
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		ASSERT_GT(towns.front()->fortificationsLevel().wallsHealth, 0);
		ASSERT_NO_FATAL_FAILURE(startBattle(towns.front()));
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		environment = std::make_shared<WallEnvironment>(gameState());
		callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}
};

TEST_F(HypotheticWallTest, FieldBattleSnapshotsDoNotInventWallsGatesOrBlockedTiles)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	environment = std::make_shared<WallEnvironment>(gameState());
	callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	HypotheticBattle child(environment.get(), parent);
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		EXPECT_EQ(parent->battleGetWallState(part), EWallState::NONE);
		EXPECT_EQ(child.battleGetWallState(part), EWallState::NONE);
	}
	EXPECT_EQ(parent->battleGetGateState(), EGateState::NONE);
	EXPECT_EQ(child.battleGetGateState(), EGateState::NONE);
	EXPECT_FALSE(parent->hasWallChanges());
	EXPECT_FALSE(child.hasWallChanges());
	const auto liveAccess = battle()->getAccessibility();
	const auto childAccess = child.getAccessibility();
	for(int hex = 0; hex < GameConstants::BFIELD_SIZE; ++hex)
		EXPECT_EQ(childAccess[hex], liveAccess[hex]);
}

TEST_F(HypotheticWallTest, ChangedWallInvalidatesOnlyLocalProjectionAndNestedCopies)
{
	ASSERT_NO_FATAL_FAILURE(prepareSiege());
	const auto original = battle()->battleGetWallState(EWallPart::BOTTOM_WALL);
	ASSERT_GT(static_cast<int>(original), static_cast<int>(EWallState::DESTROYED));
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	HypotheticBattle child(environment.get(), parent);
	EXPECT_FALSE(child.hasWallChanges());
	child.setWallState(EWallPart::BOTTOM_WALL, original);
	EXPECT_FALSE(child.hasWallChanges());
	child.setWallState(EWallPart::BOTTOM_WALL, EWallState::DESTROYED);
	EXPECT_TRUE(child.hasWallChanges());
	EXPECT_EQ(child.battleGetWallState(EWallPart::BOTTOM_WALL), EWallState::DESTROYED);
	EXPECT_EQ(parent->battleGetWallState(EWallPart::BOTTOM_WALL), original);
	EXPECT_FALSE(parent->hasWallChanges());
	EXPECT_EQ(battle()->battleGetWallState(EWallPart::BOTTOM_WALL), original);
}

TEST_F(HypotheticWallTest, DestroyedGateBecomesPassableWithoutChangingParentOrLiveGate)
{
	ASSERT_NO_FATAL_FAILURE(prepareSiege());
	battle()->si.gateState = EGateState::CLOSED;
	battle()->setWallState(EWallPart::GATE, EWallState::DAMAGED);
	auto parent = std::make_shared<HypotheticBattle>(environment.get(), callback);
	HypotheticBattle child(environment.get(), parent);
	EXPECT_EQ(child.battleGetGateState(), EGateState::CLOSED);
	EXPECT_FALSE(child.getAccessibility().accessible(BattleHex::GATE_OUTER, false, BattleSide::ATTACKER));
	child.setWallState(EWallPart::GATE, EWallState::DESTROYED);
	EXPECT_EQ(child.battleGetGateState(), EGateState::DESTROYED);
	EXPECT_TRUE(child.getAccessibility().accessible(BattleHex::GATE_OUTER, false, BattleSide::ATTACKER));
	EXPECT_EQ(parent->battleGetGateState(), EGateState::CLOSED);
	EXPECT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	EXPECT_EQ(battle()->battleGetWallState(EWallPart::GATE), EWallState::DAMAGED);
}

TEST_F(HypotheticWallTest, RealEarthquakeCanDestroyTheLastGateInModelAndAuthoritativeBattle)
{
	ASSERT_NO_FATAL_FAILURE(prepareSiege());
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::EARTHQUAKE);
	attackerSideHero->mana = 1000;
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
		battle()->setWallState(static_cast<EWallPart>(index), EWallState::DESTROYED);
	battle()->setWallState(EWallPart::GATE, EWallState::DAMAGED);
	battle()->si.gateState = EGateState::CLOSED;
	const auto * spell = SpellID(SpellID::EARTHQUAKE).toSpell();
	ASSERT_TRUE(spell->canBeCast(battle(), spells::Mode::HERO, attackerSideHero));
	HypotheticBattle model(environment.get(), callback);
	spells::BattleCast projected(&model, attackerSideHero, spells::Mode::HERO, spell);
	projected.castEval(model.getServerCallback(), {});
	EXPECT_EQ(model.battleGetWallState(EWallPart::GATE), EWallState::DESTROYED);
	EXPECT_EQ(model.battleGetGateState(), EGateState::DESTROYED);
	EXPECT_TRUE(model.hasWallChanges());
	EXPECT_TRUE(model.getAccessibility().accessible(BattleHex::GATE_OUTER, false, BattleSide::ATTACKER));
	EXPECT_EQ(battle()->battleGetWallState(EWallPart::GATE), EWallState::DAMAGED);
	EXPECT_EQ(battle()->battleGetGateState(), EGateState::CLOSED);
	EXPECT_EQ(attackerSideHero->mana, 1000);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::EARTHQUAKE;
	const auto cost = attackerSideHero->getSpellCost(spell);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(battle()->battleGetWallState(EWallPart::GATE), EWallState::DESTROYED);
	EXPECT_EQ(battle()->battleGetGateState(), EGateState::DESTROYED);
	EXPECT_EQ(attackerSideHero->mana, 1000 - cost);
}
