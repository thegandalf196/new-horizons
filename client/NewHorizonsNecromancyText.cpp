/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsNecromancyText.h"
#include "../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../lib/texts/MetaString.h"
namespace necromancyPresentation
{
void appendConversions(MetaString & text, const newHorizonsNecromancy::NecromancyResult & result)
{
	const auto baseSkeleton = CreatureID(CreatureID::decode("core:skeleton"));
	const auto zombie = CreatureID(CreatureID::decode("core:zombie"));
	const auto wight = CreatureID(CreatureID::decode("core:wight"));
	const auto boneDragon = CreatureID(CreatureID::decode("core:boneDragon"));
	if(result.lordOfDeadSkeletonsConsumed > 0 || result.boneDragonsRaised > 0)
	{
		text.appendRawString("Lord of the Dead: ");
		text.appendNumber(result.lordOfDeadSkeletonsConsumed);
		text.appendRawString(" base ");
		text.appendName(baseSkeleton, 1);
		text.appendRawString(" equivalents -> ");
		text.appendNumber(result.boneDragonsRaised);
		text.appendRawString(" ");
		text.appendName(boneDragon, result.boneDragonsRaised);
		text.appendRawString("\n");
	}

	const auto coreZombies = result.zombiesRaised - result.reclaimerZombiesRaised;
	if(result.darkConversionChosen && coreZombies > 0)
	{
		text.appendRawString("Converted: ");
		text.appendNumber(coreZombies);
		text.appendRawString(" ");
		text.appendName(zombie, coreZombies);
		text.appendRawString(" from Core casualties (three ");
		text.appendName(baseSkeleton, 3);
		text.appendRawString(" each)\n");
	}
	if(result.reclaimerZombiesRaised > 0)
	{
		text.appendRawString("Dark Reclaimer: ");
		text.appendNumber(result.reclaimerZombiesRaised);
		text.appendRawString(" ");
		text.appendName(zombie, result.reclaimerZombiesRaised);
		text.appendRawString(" from remaining Skeleton equivalents (three each)\n");
	}

	if(result.wightsRaised > 0)
	{
		text.appendRawString("Converted: ");
		text.appendNumber(result.wightsRaised);
		text.appendRawString(" ");
		text.appendName(wight, result.wightsRaised);
		text.appendRawString(" from Elite casualties (six ");
		text.appendName(baseSkeleton, 6);
		text.appendRawString(" per Wight)\n");
	}

}
}
