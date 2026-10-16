/*
 * HeroExpectedSpellFixture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "HeroQueueSelectorFixture.h"

namespace heroExpectedTest
{
class HeroExpectedSpellTest : public heroQueueTest::HeroQueueSelectorTest
{
protected:
	SpellID selectedSpell;
	void prepareExpected(const std::string & key);
	void expectMentalPressure(bool confusion);
	void expectSelectedSpell();
	std::shared_ptr<HypotheticBattle> detached();
	void prepareTransition(const std::shared_ptr<HypotheticBattle> & model,
		std::optional<HypotheticBattle::ProjectedSpellAllowance> & prepared, bool begin);
};
}
