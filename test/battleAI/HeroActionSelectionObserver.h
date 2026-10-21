/*
 * HeroActionSelectionObserver.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../lib/battle/BattleAction.h"
#include <cmath>
#include <limits>

namespace heroSelectionTest
{
/// Test-local streaming oracle: no candidate table, private pointer, log target
/// or state/provider retained. Check assertions AFTER the noexcept callbacks.
/// expectPerformedMatching is ONLY for the ordinary Combined Arms fixture;
/// mandatory/typed follow-up admission is tested separately.
class Observer final : public heroActionSelection::Observer
{
public:
	size_t count = 0;
	bool invalidOrdinal = false;
	bool nonfinite = false;
	bool hasBest = false;
	bool hasFlank = false;
	bool hasMagicArrow = false;
	float bestFlank = std::numeric_limits<float>::lowest();
	heroActionSelection::CandidateValue best{0, HeroCommand::NONE, SpellID::NONE, 0, 0};
	size_t selections = 0;
	std::optional<heroActionSelection::SelectionValue> selection;

	void candidate(heroActionSelection::CandidateValue value) noexcept override
	{
		invalidOrdinal |= value.ordinal != count++;
		hasMagicArrow |= value.spell == SpellID::MAGIC_ARROW;
		nonfinite |= !std::isfinite(value.value);
		if(!hasBest || best.value < value.value)
		{
			best = value;
			hasBest = true;
		}
		if(value.command == HeroCommand::FLANK)
		{
			if(!hasFlank || bestFlank < value.value)
				bestFlank = value.value;
			hasFlank = true;
		}
	}
	void selected(heroActionSelection::SelectionValue value) noexcept override
	{
		++selections;
		selection = value;
	}
	void expectPerformedMatching(const BattleAction & actual) const
	{
		ASSERT_FALSE(invalidOrdinal);
		ASSERT_FALSE(nonfinite);
		ASSERT_GT(count, 0u);
		ASSERT_TRUE(hasBest);
		ASSERT_EQ(selections, 1u);
		ASSERT_TRUE(selection);
		EXPECT_EQ(selection->ordinal, best.ordinal); // Strict first maximum.
		EXPECT_EQ(selection->disposition, heroActionSelection::Disposition::PERFORM);
		EXPECT_GT(best.value, selection->baseline);
		EXPECT_FALSE(vstd::isAlmostEqual(best.value, selection->baseline));
		if(best.command != HeroCommand::NONE)
		{
			EXPECT_EQ(actual.actionType, EActionType::HERO_COMMAND);
			EXPECT_EQ(actual.command, best.command);
		}
		else
		{
			EXPECT_EQ(actual.actionType, EActionType::HERO_SPELL);
			EXPECT_EQ(actual.spell, best.spell);
			EXPECT_EQ(actual.spellOvercharge, best.overcharge);
		}
	}
};
}
