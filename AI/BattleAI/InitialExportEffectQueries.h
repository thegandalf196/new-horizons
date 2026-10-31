/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "InitialExportWorld.h"

class InitialExportBattleSubject;
/// Exact private export ledger, never a player-view/native importer. All borrows
/// remain read-only except state() and the named edits, and die with this scope.
/// Retains the existing world's actual native/library source-interval contract.
class InitialExportWorld::EffectQueries final
{
	friend class InitialExportWorld;
	friend class InitialExportBattleSubject;
	std::unique_ptr<InitialExportWorld> world;
	std::vector<Source> sources;
	explicit EffectQueries(std::unique_ptr<InitialExportWorld> world);
	Source target(uint32_t id) const;
public:
	~EffectQueries();
	const battle::Unit * find(uint32_t id) const;
	const battle::Unit & current(uint32_t id) const;
	battle::CUnitState & state(uint32_t id);
	const BonusList & exportedRecords(uint32_t id) const;
	void appendExported(uint32_t id, const std::shared_ptr<Bonus> & value);
	void removeExported(uint32_t id, const std::shared_ptr<Bonus> & value);
	void removeMatchingExports(uint32_t id, const CSelector & selector);
	void refreshMatchingExports(uint32_t id, const Bonus & value);
	AccessibilityInfo accessibility(uint32_t id) const;
};
