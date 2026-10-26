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
std::vector<RecoveryOutput> recoveryOutputs(const newHorizonsNecromancy::NecromancyResult & result)
{
	std::vector<RecoveryOutput> outputs;
	if(result.recoveredSkeletons > 0)
		outputs.push_back({CreatureID(CreatureID::decode("core:skeleton")), result.recoveredSkeletons});
	if(result.recoveredSkeletonWarriors > 0)
		outputs.push_back({CreatureID(CreatureID::decode("core:skeletonWarrior")), result.recoveredSkeletonWarriors});
	return outputs;
}

void appendRecovery(MetaString & text, const newHorizonsNecromancy::NecromancyResult & result)
{
	const auto outputs = recoveryOutputs(result);
	if(outputs.empty())
		return;
	text.appendRawString("Grave Knowledge restored to army: ");
	bool first = true;
	for(const auto & output : outputs)
	{
		if(!first)
			text.appendRawString(" and ");
		text.appendNumber(output.count);
		text.appendRawString(" ");
		text.appendName(output.creature, output.count);
		first = false;
	}
	text.appendRawString(" from your own fallen troops.\n");
}

void appendFreshDelivery(MetaString & text, const newHorizonsNecromancy::NecromancyResult & result, const std::string & ossuaryTownName)
{
	const auto skeleton = result.skeletonCreature.hasValue()
		? result.skeletonCreature
		: CreatureID(CreatureID::decode("core:skeleton"));
	const auto zombie = CreatureID(CreatureID::decode("core:zombie"));
	const auto wight = CreatureID(CreatureID::decode("core:wight"));
	const auto boneDragon = CreatureID(CreatureID::decode("core:boneDragon"));
	const bool hasNecromancyOutput = result.skeletonsRaised > 0
		|| result.zombiesRaised > 0
		|| result.wightsRaised > 0
		|| result.boneDragonsRaised > 0;
	if(hasNecromancyOutput)
	{
		if(result.ossuaryTown != ObjectInstanceID::NONE)
		{
			text.appendRawString("Delivered to Ossuary: ");
			if(!ossuaryTownName.empty())
				text.appendRawString(ossuaryTownName);
			else
				text.appendRawString("a Necropolis town");
			text.appendRawString(" instead of the hero: ");
		}
		else
			text.appendRawString("Delivered to army: ");
		bool hasOutput = false;
		if(result.skeletonsRaised > 0)
		{
			text.appendNumber(result.skeletonsRaised);
			text.appendRawString(" ");
			text.appendName(skeleton, result.skeletonsRaised);
			hasOutput = true;
		}
		if(result.zombiesRaised > 0)
		{
			if(hasOutput)
				text.appendRawString(" and ");
			text.appendNumber(result.zombiesRaised);
			text.appendRawString(" ");
			text.appendName(zombie, result.zombiesRaised);
			hasOutput = true;
		}
		if(result.wightsRaised > 0)
		{
			if(hasOutput)
				text.appendRawString(" and ");
			text.appendNumber(result.wightsRaised);
			text.appendRawString(" ");
			text.appendName(wight, result.wightsRaised);
			hasOutput = true;
		}
		if(result.boneDragonsRaised > 0)
		{
			if(hasOutput)
				text.appendRawString(" and ");
			text.appendNumber(result.boneDragonsRaised);
			text.appendRawString(" ");
			text.appendName(boneDragon, result.boneDragonsRaised);
		}
		text.appendRawString("\n");
	}
}

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
