/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsSerpentsEmbrace.h"
#include "NewHorizonsParalyzingGaze.h"
#include "NewHorizonsConfusionControl.h"
#include <vector>
namespace stackEffectVectorRules
{
/// Same native Serpent -> Gaze -> Confusion loops, with lazy current-unit reads.
template<typename UnitLookup>
void validateIncomingRecords(const IBattleInfo & battle, uint32_t id,
	const std::vector<Bonus> & records, UnitLookup && lookup)
{
	for(const auto & entry : records) newHorizonsSerpentsEmbrace::validateApplicationWithLookup(battle, id, entry, lookup);
	for(const auto & entry : records) newHorizonsParalyzingGaze::validateApplicationWithLookup(battle, id, entry, lookup);
	for(const auto & entry : records)
		if(entry.type == BonusType::CONFUSION_PENDING) newHorizonsConfusionControl::validateMarker(entry);
}
DLL_LINKAGE void validateRemovalRecords(const std::vector<Bonus> & records);
DLL_LINKAGE void validateSwiftRecords(const battle::Unit & unit, const std::vector<Bonus> & records, bool adding);
}
