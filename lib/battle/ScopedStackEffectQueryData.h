/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "ScopedStackEffectQueries.h"
#include "LiveUnitEnvironmentContext.h"
#include "../bonuses/NativeBonusGraphAccess.h"
#include "../bonuses/BonusGraphQuery.h"
#include "../bonuses/ProjectedBonusView.h"
#include "../bonuses/ProjectedBonusRoster.h"
#include <map>

class CCreature;
namespace battle
{
struct ScopedStackEffectQueries::Data final : BonusGraphQuery
{
	struct Row;
	struct Context;
	struct View final : ProjectedBonusView
	{
		Data & data;
		Row & row;
		View(Data & data, Row & row);
		const Unit * evaluatedUnit() const override;
		TConstBonusListPtr getAllBonuses(const CSelector &, const std::string &) const override;
		TConstBonusListPtr getUnstackedBonuses(const CSelector &) const override;
		int32_t getTreeVersion() const override { return 0; }
		void onBattleFormChanged(const CUnitState &) const override;
		std::vector<Bonus> sourceLocalSpellEffects() const override;
	};
	struct Row
	{
		const CStack & source;
		View view;
		CUnitStateDetached state;
		CreatureID baseCreature;
		const CCreature * attached;
		bool initializing = true;
		Row(Data &, const CStack &);
	};
	struct Lists { BonusList local; BonusList exported; };
	NativeBonusGraphAccess access; // first acquired, last released
	const BattleInfo & battle;
	struct Alias { std::shared_ptr<Bonus> input; std::shared_ptr<Bonus> value; };
	mutable std::map<const Bonus *, Alias> aliases;
	mutable std::map<const CBonusSystemNode *, Lists> lists;
	std::map<const CBonusSystemNode *, TCNodesVector> parentChanges;
	std::unique_ptr<ProjectedBonusRoster> roster;
	std::unique_ptr<Context> context;
	std::unique_ptr<UnitEnvironmentQueries> environment;
	std::map<const CStack *, std::unique_ptr<Row>> rows;
	std::map<uint32_t, Row *> targets;

	Data(const BattleInfo &, const std::vector<const CStack *> &);
	~Data();
	std::shared_ptr<Bonus> copy(const std::shared_ptr<Bonus> &) const;
	Lists & storage(const CBonusSystemNode &) const;
	Row & target(uint32_t) const;
	const BonusList & local(const CBonusSystemNode &) const override;
	const TCNodesVector & parents(const CBonusSystemNode &) const override;
	const TNodesVector & propagatingParents(const CBonusSystemNode &) const override;
	const TNodesVector & children(const CBonusSystemNode &) const override;
	BonusEvaluationContext evaluation(const CBonusSystemNode &) const override;
	TConstBonusListPtr query(const Row &, const CSelector &, bool unstacked) const;
	void propagate(const CBonusSystemNode &, const std::shared_ptr<Bonus> &, const CBonusSystemNode &, bool add);
	void remove(const CBonusSystemNode &, const std::shared_ptr<Bonus> &);
	void removeMatching(const CBonusSystemNode &, const CSelector &);
	void changeForm(Row &);
	AccessibilityInfo accessibility(uint32_t) const;
};
}
