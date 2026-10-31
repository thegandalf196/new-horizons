/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StackEffectVectorRules.h"
#include "NewHorizonsSwiftRebirth.h"
#include <stdexcept>
namespace stackEffectVectorRules
{
void validateRemovalRecords(const std::vector<Bonus> & records)
{
	for(const auto & entry : records)
		if(entry.type == BonusType::CONFUSION_PENDING) newHorizonsConfusionControl::validateMarker(entry);
}
void validateSwiftRecords(const battle::Unit & unit, const std::vector<Bonus> & records, bool adding)
{
	int swiftMarkers = 0;
	for(const auto & entry : records)
		if(newHorizonsSwiftRebirth::isLifecycleMarker(entry))
		{
			if(++swiftMarkers > 1)
				throw std::invalid_argument(adding ? "Duplicate Swift Rebirth ADD" : "Duplicate Swift Rebirth UPDATE");
			newHorizonsSwiftRebirth::validateTransition(newHorizonsSwiftRebirth::lifecycle(unit), entry, adding);
		}
}
}
