/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/MechanicsPresence.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsCreatureAbilitySuppression.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/ProjectedBonusView.h"
#include "../../lib/bonuses/Updaters.h"
#include "../mock/mock_UnitInfo.h"

namespace
{
class CountingView final : public ProjectedBonusView
{
public:
	const battle::Unit * subject = nullptr;
	battle::MechanicsPresence presence;
	bool admitted = true;
	mutable int queries = 0;
	const battle::Unit * evaluatedUnit() const override { return subject; }
	bool mayHaveBonusType(BonusType type) const override { return !admitted || presence.mayHave(type); }
	void onBattleFormChanged(const battle::CUnitState &) const override {}
	TConstBonusListPtr getAllBonuses(const CSelector &, const std::string &) const override
	{
		++queries;
		return std::make_shared<BonusList>();
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override { return getAllBonuses(selector, {}); }
	int32_t getTreeVersion() const override { return 0; }
};
class UnknownUpdater final : public IUpdater {};
}

TEST(MechanicsPresenceTest, AbsentGuardsDoNoMergedQueries)
{
	::testing::StrictMock<UnitInfoMock> info;
	CountingView view;
	battle::CUnitStateDetached unit(&info, &view);
	view.subject = &unit;
	for(int i = 0; i < 100; ++i)
	{
		EXPECT_FALSE(newHorizonsFrozen::isFrozen(unit));
		EXPECT_EQ(newHorizonsCreatureAbilitySuppression::suppressionLevel(unit), 0);
		EXPECT_FALSE(unit.isHypnotized());
		EXPECT_FALSE(unit.isInvincible());
	}
	EXPECT_EQ(view.queries, 0);
	view.admitted = false; // An unsupported provider retains the exact full query.
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(unit));
	EXPECT_GT(view.queries, 0);
}

TEST(MechanicsPresenceTest, ForeignBoundViewCannotTurnProvenAbsenceIntoPermission)
{
	::testing::StrictMock<UnitInfoMock> info;
	CountingView view;
	battle::CUnitStateDetached unit(&info, &view);
	EXPECT_THROW(newHorizonsFrozen::isFrozen(unit), std::logic_error);
	EXPECT_EQ(view.queries, 0);
}

TEST(MechanicsPresenceTest, OpaqueUpdaterCannotProveAbsenceAndCopiesStayIndependent)
{
	battle::MechanicsPresence base;
	Bonus bonus;
	bonus.type = BonusType::STACK_HEALTH;
	bonus.updater = std::make_shared<TimesStackSizeUpdater>();
	base.include(bonus);
	EXPECT_FALSE(base.mayHave(BonusType::PHYSICAL_AFFLICTION));
	auto branch = base;
	bonus.updater = std::make_shared<UnknownUpdater>();
	branch.include(bonus);
	EXPECT_TRUE(branch.mayHave(BonusType::PHYSICAL_AFFLICTION));
	EXPECT_FALSE(base.mayHave(BonusType::PHYSICAL_AFFLICTION));
}

TEST(MechanicsPresenceTest, NativePublicationKeepsInheritedConditionalTypesAndStaleIsUnknown)
{
	CBonusSystemNode parent(BonusNodeType::CREATURE), child(BonusNodeType::STACK_BATTLE);
	child.attachTo(parent);
	child.getAllBonuses(Selector::all);
	EXPECT_FALSE(child.cachedMayHaveBonusType(BonusType::HYPNOTIZED));
	auto marker = std::make_shared<Bonus>();
	marker->type = BonusType::HYPNOTIZED;
	marker->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::LUCK);
	parent.addNewBonus(marker);
	EXPECT_TRUE(child.cachedMayHaveBonusType(BonusType::HYPNOTIZED));
	EXPECT_TRUE(child.getAllBonuses(Selector::type()(BonusType::HYPNOTIZED))->empty());
	EXPECT_TRUE(child.cachedMayHaveBonusType(BonusType::HYPNOTIZED));
	parent.removeBonus(marker);
	child.getAllBonuses(Selector::all);
	EXPECT_FALSE(child.cachedMayHaveBonusType(BonusType::HYPNOTIZED));
	child.detachFrom(parent);
}
