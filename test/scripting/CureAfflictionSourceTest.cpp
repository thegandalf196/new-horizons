/*
 * CureAfflictionSourceTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../../luascript/StdInc.h"
#include "../../luascript/api/spells/Mechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../mock/mock_spells_Mechanics.h"

#include <stdexcept>

namespace
{
class CureSelectionMechanics : public spells::MechanicsMock
{
public:
	SpellID selection = SpellID::NONE;
	SpellID getCureAffliction() const override { return selection; }
};
}

TEST(CureAfflictionSource, UnselectedSourceIsEmptyWithoutResolvingNoneAsSpell)
{
	CureSelectionMechanics mechanics;
	EXPECT_EQ(scripting::api::MechanicsProxy::getCureAfflictionSource(mechanics), "");
}

TEST(CureAfflictionSource, SelectedPoisonKeepsRegisteredJsonKey)
{
	CureSelectionMechanics mechanics;
	mechanics.selection = SpellID::POISON;
	const auto * poison = mechanics.selection.toSpell();
	ASSERT_NE(poison, nullptr);
	EXPECT_EQ(scripting::api::MechanicsProxy::getCureAfflictionSource(mechanics), poison->getJsonKey());
}

TEST(CureAfflictionSource, InvalidNonSentinelSourceStillRejects)
{
	CureSelectionMechanics mechanics;
	mechanics.selection = SpellID(-2);
	EXPECT_THROW(scripting::api::MechanicsProxy::getCureAfflictionSource(mechanics), std::runtime_error);
}
