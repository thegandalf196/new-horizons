/*
 * AttackPreviewRainTargetsTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/AttackPreviewTargets.h"
#include "../../lib/CRandomGenerator.h"

TEST_F(ProjectedTargetHitFixture, RainTargetExtractionPreservesHealthHexAndOwnerPriority)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const BattleHex primaryHex(12, 8);
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), primaryHex, 100);
	std::vector<BattleHex> neighbors;
	for(int value = 0; value < GameConstants::BFIELD_SIZE; ++value)
	{
		const BattleHex hex(value);
		if(hex.isValid() && BattleHex::getDistance(hex, primaryHex) == 1
			&& !battle()->battleGetStackByPos(hex)) neighbors.push_back(hex);
	}
	ASSERT_GE(neighbors.size(), 3u);
	auto * first = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), neighbors[0], 1);
	auto * larger = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), neighbors[1], 2);
	addStack(BattleSide::ATTACKER, creatureByName("core:angel"), neighbors[2], 100);
	const auto rng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto defaults = projectedHitTest::serialized(CRandomGenerator::getDefault());
	EXPECT_EQ(attackPreview::rainSecondary(*child, child->getForUpdate(braced->unitId()).get(),
		primary, primary->getHexes())->unitId(), larger->unitId());
	auto changed = child->getForUpdate(larger->unitId());
	int64_t damage = changed->getMaxHealth();
	changed->damage(damage);
	EXPECT_EQ(attackPreview::rainSecondary(*child, child->getForUpdate(braced->unitId()).get(),
		primary, primary->getHexes())->unitId(), first->unitId());
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaults);
	EXPECT_EQ(larger->getCount(), 2);
}
