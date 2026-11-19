/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../AI/BattleAI/StackWithBonuses.h"

/// Malformed/foreign provenance fixtures cannot use ordinary effect admission.
/// Each explicit fixture edit invalidates the owning layer. Borrowed mutable
/// record identities intentionally force the same live rescan as query aliases.
struct StackWithBonusesTestAccess
{
	static void appendAdded(StackWithBonuses & unit, std::shared_ptr<Bonus> record)
	{
		const StackWithBonuses::ProjectionMutation mutation(unit);
		unit.bonusesToAdd.push_back(std::move(record));
		unit.exposeProjectionAliases(false);
	}
	static void appendUpdated(StackWithBonuses & unit, std::shared_ptr<Bonus> record)
	{
		const StackWithBonuses::ProjectionMutation mutation(unit);
		unit.bonusesToUpdate.push_back(std::move(record));
		unit.exposeProjectionAliases(false);
	}
	static void replaceAdded(StackWithBonuses & unit, std::vector<std::shared_ptr<Bonus>> records)
	{
		const StackWithBonuses::ProjectionMutation mutation(unit);
		unit.bonusesToAdd = std::move(records);
		unit.exposeProjectionAliases(false);
	}
	static void clearAdded(StackWithBonuses & unit)
	{
		const StackWithBonuses::ProjectionMutation mutation(unit);
		unit.bonusesToAdd.clear();
	}
	static void removeIdentity(StackWithBonuses & unit, std::shared_ptr<Bonus> record)
	{
		const StackWithBonuses::ProjectionMutation mutation(unit);
		unit.bonusesToRemove.insert(std::move(record));
	}
	static void replaceRemoved(StackWithBonuses & unit, std::set<std::shared_ptr<Bonus>> records)
	{
		const StackWithBonuses::ProjectionMutation mutation(unit);
		unit.bonusesToRemove = std::move(records);
	}
	static bool queryInsideMutation(StackWithBonuses & unit, const Bonus & record)
	{
		const StackWithBonuses::ProjectionMutation mutation(unit);
		if(unit.mayHaveBonusType(record.type)) return false;
		unit.bonusesToAdd.push_back(std::make_shared<Bonus>(record));
		return unit.mayHaveBonusType(record.type);
	}
	static bool addedEmpty(const StackWithBonuses & unit) { return unit.bonusesToAdd.empty(); }
	static bool updatedEmpty(const StackWithBonuses & unit) { return unit.bonusesToUpdate.empty(); }
	static int treeVersion(const StackWithBonuses & unit) { return unit.treeVersionLocal; }
	static uint64_t projectionVersion(const StackWithBonuses & unit) { return unit.projectionVersion; }
	static bool cached(const StackWithBonuses & unit)
	{
		return unit.projectionPresence && unit.projectionPresence->version == unit.projectionVersion;
	}
	static bool aliasesExposed(const StackWithBonuses & unit)
	{
		return unit.projectionAliasesEscaped || (unit.projectionAliasLease && *unit.projectionAliasLease);
	}
	static bool cacheable(const StackWithBonuses & unit)
	{
		return unit.projectionPresence && unit.projectionPresence->cacheable;
	}
};
