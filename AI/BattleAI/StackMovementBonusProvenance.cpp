/*
 * StackMovementBonusProvenance.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "MovementBonusProvenance.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"

namespace
{
bool invariantRecord(const Bonus & bonus)
{
	return movementBonusProvenance::invariantRecord(bonus);
}

bool invariantRecord(const std::shared_ptr<Bonus> & bonus)
{
	return bonus && invariantRecord(*bonus);
}

template<typename Records>
bool invariantRecords(const Records & records, size_t & remaining)
{
	if(records.size() > remaining)
		return false;
	remaining -= records.size();
	return std::ranges::all_of(records, [](const auto & record) { return invariantRecord(record); });
}
}

bool StackWithBonuses::usesCanonicalMovementQuery(const HypotheticBattle & branch) const
{
	if(typeid(*this) != typeid(StackWithBonuses) || owner != &branch || hasBattleFormState()
		|| damageScoringBonuses || damageScoringUnstackedBonuses)
		return false;
	const auto subject = owner->initialExportSubject();
	return subject && origBearer == subject->world().unitAt(subject->occurrence(id)).getBonusBearer();
}

bool StackWithBonuses::hasMovementInvariantBonusSources() const
{
	if(hasBattleFormState() || damageScoringBonuses || damageScoringUnstackedBonuses)
		return false;
	if(const auto subject = owner->initialExportSubject())
		return subject->world().hasMovementInvariantSources();
	std::set<const StackWithBonuses *> seen;
	size_t remaining = 8192;
	const IBonusBearer * source = this;
	while(const auto * projected = dynamic_cast<const StackWithBonuses *>(source))
	{
		if(typeid(*projected) != typeid(StackWithBonuses)
			|| seen.size() >= 512 || !seen.insert(projected).second
			|| !projected->originalUnit || projected->originalUnit->unitId() != projected->id
			|| projected->hasBattleFormState() || projected->damageScoringBonuses
			|| projected->damageScoringUnstackedBonuses)
			return false;
		if(!invariantRecords(projected->bonusesToAdd, remaining)
			|| !invariantRecords(projected->bonusesToUpdate, remaining)
			|| !invariantRecords(projected->bonusesToRemove, remaining))
			return false;
		// Captured copies cannot prove absence: inspect them AND continue to the
		// original bearer, where inactive native limited records still exist.
		if((projected->projectedEffects && !invariantRecords(*projected->projectedEffects, remaining))
			|| (projected->projectedUnstackedEffects && !invariantRecords(*projected->projectedUnstackedEffects, remaining))
			|| (projected->exactLocalSpellEffects && !invariantRecords(*projected->exactLocalSpellEffects, remaining)))
			return false;
		source = projected->origBearer;
	}
	return source && movementBonusProvenance::invariantSources(*source);
}
