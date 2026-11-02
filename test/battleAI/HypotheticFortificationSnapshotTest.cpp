/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/SiegeInfo.h"
#include <vcmi/Environment.h>
namespace
{
class FortificationEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit FortificationEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class OrderedFortificationCallback final : public CPlayerBattleCallback
{
public:
	using CPlayerBattleCallback::CPlayerBattleCallback;
	mutable std::vector<std::pair<char, EWallPart>> calls;
	EWallState battleGetWallState(EWallPart part) const override
	{
		calls.emplace_back('W', part);
		return CPlayerBattleCallback::battleGetWallState(part);
	}
	int32_t getWallStructuralHP(EWallPart part) const override
	{
		calls.emplace_back('H', part);
		return CPlayerBattleCallback::getWallStructuralHP(part);
	}
};
class HypotheticFortificationSnapshotTest : public BattleTestFixture {};
TEST_F(HypotheticFortificationSnapshotTest, NonsiegeRootRetainsCanonicalValuesAndWallThenHPOrder)
{
	startGame(); startBattle();
	ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 2), nullptr);
	beginCombat();
	FortificationEnvironment environment(gameState());
	auto source = std::make_shared<OrderedFortificationCallback>(battle(), PlayerColor(0));
	ASSERT_EQ(source->battleGetDefendedTown(), nullptr);
	auto branch = std::make_shared<HypotheticBattle>(&environment, source);
	std::vector<std::pair<char, EWallPart>> expected;
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		expected.emplace_back('W', part); expected.emplace_back('H', part);
		EXPECT_EQ(branch->getWallState(part), EWallState::NONE);
		EXPECT_EQ(branch->getWallStructuralHP(part), 0);
	}
	EXPECT_EQ(source->calls, expected);
	EXPECT_EQ(branch->getGateState(), EGateState::NONE);
	// This is legacy source characterization, not an owned Batch import fixture.
	auto child = std::make_shared<HypotheticBattle>(&environment, branch);
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		EXPECT_EQ(child->getWallState(part), branch->getWallState(part));
		EXPECT_EQ(child->getWallStructuralHP(part), branch->getWallStructuralHP(part));
	}
	EXPECT_EQ(child->getGateState(), branch->getGateState());
}
}
