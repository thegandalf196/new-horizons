/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
#include "../bonuses/Bonus.h"
class CStack;
class BonusList;
class ProjectedBonusRoster;
class CBattleInfoCallback;
class HypotheticBattle;
namespace battle
{
class Unit;
class LiveUnitEnvironmentContext;
/// Opaque operations for numeric CURRENT creature bonuses. Native identities
/// remain borrowed under the caller's original source interval; no world owner,
/// Hero policy or public template-query capability is retained or exported.
class DLL_LINKAGE CommittedBonusOperations final
{
	friend class LiveUnitEnvironmentContext;
	// The AI adapter proves exact occurrence/provider/typed ancestry and the
	// complete current roster under its sourceBinding before calling this stage.
	friend class ::HypotheticBattle;
	struct Data;
	std::unique_ptr<Data> data;
	explicit CommittedBonusOperations(const CBattleInfoCallback & observer);
	bool empty() const;
	bool containsSource(const CStack & native) const;
	TConstBonusListPtr replaceCurrent(const CStack & native, const Unit & projected,
		const ProjectedBonusRoster & roster, const BonusList & nativeUnstackedBaseline) const;
public:
	~CommittedBonusOperations();
};
}
